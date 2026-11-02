package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"strconv"
	"time"

	"github.com/gorilla/websocket"
)

func newGatewayMux(ctx context.Context, cfg runtimeConfig, hub *driverHub, sessions *sessionRegistry, controller *driverDeliveryController, sem chan struct{}) http.Handler {
	if controller == nil {
		panic("gateway delivery controller is required")
	}
	ws := cfg.websocketRuntimeConfig()
	upgrader := websocket.Upgrader{CheckOrigin: gatewayCheckOrigin}
	mux := http.NewServeMux()
	mux.HandleFunc("/healthz", handleHealthz(cfg, hub))
	mux.HandleFunc("/metrics", handleMetrics(cfg, hub, sessions, controller))
	if cfg.DriverMode && cfg.DeliveryAuditEnabled {
		mux.HandleFunc("/diagnostics/delivery-slot", localOnly(handleDeliveryAudit(controller)))
		mux.HandleFunc("/diagnostics/driver-delivery-terminal", localOnly(handleDriverDeliveryTerminal(hub, controller)))
	}
	registerPprofHandlers(mux)
	wsHandler := func(w http.ResponseWriter, r *http.Request) {
		if !isWebSocketUpgradeRequest(r) {
			http.NotFound(w, r)
			return
		}
		select {
		case sem <- struct{}{}:
		default:
			gatewayMetrics.connectionsRejectedTotal.Add(1)
			http.Error(w, "gateway busy", http.StatusServiceUnavailable)
			return
		}
		conn, err := upgrader.Upgrade(w, r, nil)
		if err != nil {
			<-sem
			log.Printf("upgrade failed: %v", err)
			return
		}
		configureWebSocketRead(conn, ws)
		rawSlot := ""
		if cfg.DriverMode {
			rawSlot = r.Header.Get(deliveryAuditHeader)
		}
		var owner sessionOwnerRegistrationPort
		if cfg.DriverMode {
			owner = controller.registrationPort(rawSlot)
		}
		sessionID := sessions.addWithOwner(conn, r.RemoteAddr, owner)
		if sessionID == "" {
			_ = conn.Close()
			<-sem
			return
		}
		gatewayMetrics.connectionsTotal.Add(1)
		log.Printf("websocket connected: %s", sessionID)
		go func(remoteAddr string) {
			defer func() {
				/* Revoke the local session before attempting the best-effort
				 * driver notification.  A full/stopped driver queue must not
				 * retain the websocket, registry entry, or admission slot. */
				if cfg.DriverMode {
					sessions.removeWithOwner(sessionID, controller.registrationPort(""))
				} else {
					sessions.remove(sessionID)
				}
				_ = conn.Close()
				<-sem
				if cfg.DriverMode {
					disconCtx, cancel := context.WithTimeout(
						context.Background(), httpWriteTimeout)
					_ = hub.enqueueDriverFrame(disconCtx,
						driverGatewayFrame{Type: "discon", CID: sessionID})
					cancel()
				}
				log.Printf("websocket disconnected: %s", sessionID)
			}()
			if cfg.DriverMode {
				if !hub.waitConnected(3 * time.Second) {
					log.Printf("driver gateway not ready for %s", sessionID)
					return
				}
				remoteIP, remotePort := splitRemoteAddr(remoteAddr)
				if err := hub.enqueueDriverFrame(ctx, driverGatewayFrame{Type: "login", CID: sessionID, Data: map[string]any{"ip": remoteIP, "port": remotePort, "session_id": sessionID}}); err != nil {
					log.Printf("driver gateway login send failed for %s: %v", sessionID, err)
					return
				}
				for {
					_, message, err := readWebSocketMessage(conn, ws)
					if err != nil {
						return
					}
					gatewayMetrics.inboundMessagesTotal.Add(1)
					if envelope, envelopeErr := decodeCommandEnvelope(message); envelopeErr == nil {
						if err := hub.enqueueDriverFrame(ctx, driverGatewayFrame{
							Type: "data", CID: sessionID, Data: commandEnvelopeDriverData(envelope),
						}); err != nil {
							log.Printf("driver gateway data send failed for %s: %v", sessionID, err)
							return
						}
						continue
					} else if isCommandEnvelopeCandidate(message) {
						log.Printf("invalid command envelope from %s: %v", sessionID, envelopeErr)
						return
					}
					if err := hub.enqueueDriverFrame(ctx, driverGatewayFrame{Type: "data", CID: sessionID, Data: string(message)}); err != nil {
						log.Printf("driver gateway data send failed for %s: %v", sessionID, err)
						return
					}
				}
			} else {
				runDirectProxy(ctx, sessionID, conn, cfg, sessions)
			}
		}(r.RemoteAddr)
	}
	mux.HandleFunc("/ws", wsHandler)
	return mux
}

func handleDeliveryAudit(controller *driverDeliveryController) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		slot, err := strconv.Atoi(r.URL.Query().Get("slot"))
		if err != nil {
			http.NotFound(w, r)
			return
		}
		snapshot, ok := controller.deliveryAuditSnapshot(slot)
		if !ok {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		_ = json.NewEncoder(w).Encode(snapshot)
	}
}

func handleDriverDeliveryTerminal(hub *driverHub, controller *driverDeliveryController) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		action := r.URL.Query().Get("action")
		if action == "" {
			action = "status"
		}
		var snapshot driverDeliveryTerminalSnapshot
		switch action {
		case "freeze":
			if r.Method != http.MethodPost {
				http.Error(w, "freeze requires POST", http.StatusMethodNotAllowed)
				return
			}
			snapshot = controller.freezeDriverDeliveryAdmission()
		case "status":
			if r.Method != http.MethodGet && r.Method != http.MethodPost {
				http.Error(w, "status requires GET or POST", http.StatusMethodNotAllowed)
				return
			}
			snapshot = controller.driverDeliveryTerminalSnapshot()
		default:
			http.Error(w, "unsupported terminal action", http.StatusBadRequest)
			return
		}

		if r.Method == http.MethodPost {
			requestTS := time.Now().UnixMicro()
			snapshot.DriverStatusRequestTS = requestTS
			snapshot.DriverStatusGenerationBefore = driverRuntimeStatusGeneration()
			err := hub.enqueueDriverFrame(r.Context(), driverGatewayFrame{
				Type: "sys", Action: "status", TS: requestTS,
			})
			snapshot.DriverStatusRequested = err == nil
			if err != nil {
				snapshot.Error = err.Error()
			}
		}
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		_ = json.NewEncoder(w).Encode(snapshot)
	}
}

func runWebsocket(ctx context.Context, cfg runtimeConfig, hub *driverHub, sessions *sessionRegistry, controller *driverDeliveryController) error {
	if controller == nil {
		return fmt.Errorf("gateway delivery controller is required")
	}
	sem := make(chan struct{}, cfg.MaxConnections)
	server := &http.Server{
		Addr:              net.JoinHostPort(cfg.ListenHost, strconv.Itoa(cfg.ListenPort)),
		Handler:           newGatewayMux(ctx, cfg, hub, sessions, controller, sem),
		ReadHeaderTimeout: httpReadHeaderTimeout,
		IdleTimeout:       httpIdleTimeout,
		WriteTimeout:      httpWriteTimeout,
	}
	go func() {
		<-ctx.Done()
		shutdownDone := make(chan struct{})
		go func() {
			closed := sessions.closeAll(controller.registrationPort(""))
			if closed > 0 {
				log.Printf("gateway shutdown closed %d websocket sessions", closed)
			}
			close(shutdownDone)
		}()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), httpWriteTimeout)
		defer cancel()
		if err := server.Shutdown(shutdownCtx); err != nil {
			log.Printf("gateway HTTP shutdown incomplete: %v", err)
		}
		select {
		case <-shutdownDone:
		case <-shutdownCtx.Done():
			log.Printf("gateway session cleanup exceeded shutdown deadline")
		}
	}()
	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		return err
	}
	return nil
}
