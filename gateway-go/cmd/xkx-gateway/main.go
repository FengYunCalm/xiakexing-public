package main

import (
	"context"
	"log"
	"net"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"
)

func main() {
	cfg, err := loadConfig()
	if err != nil {
		log.Fatalf("load gateway config failed: %v", err)
	}
	if !cfg.Enabled {
		log.Printf("gateway disabled by configuration")
		return
	}
	configureLogging(cfg)
	log.Printf("gateway build identity: %s", gatewayBuildIdentityJSON())
	ws := cfg.websocketRuntimeConfig()
	log.Printf("gateway config: listen=%s driver_mode=%t driver=%s driver_send_buffer=%d driver_ack_window=%d max_connections=%d ws_write_timeout=%s ws_pong_timeout=%s ws_ping_interval=%s ws_max_message_size=%d ws_send_buffer=%d",
		net.JoinHostPort(cfg.ListenHost, strconv.Itoa(cfg.ListenPort)),
		cfg.DriverMode,
		net.JoinHostPort(cfg.DriverHost, strconv.Itoa(cfg.DriverPort)),
		cfg.DriverSendBuffer,
		cfg.DriverAckWindow,
		cfg.MaxConnections,
		ws.writeTimeout,
		ws.pongTimeout,
		ws.pingInterval,
		ws.maxMessageSize,
		ws.sendBuffer,
	)
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()
	var deliveryAudit *deliveryAudit
	if cfg.DriverMode && cfg.DeliveryAuditEnabled {
		deliveryAudit = newDeliveryAudit(deliveryAuditSlotCount)
	}
	sessions := newProductionSessionRegistryWithWebSocketConfig(cfg.websocketRuntimeConfig())
	controller := newDriverDeliveryController(sessions, deliveryAudit, cfg.DriverMode)
	hub := newDriverHubFromConfig(cfg)
	driverDone := make(chan struct{})
	if cfg.DriverMode {
		hub.startDriverWriter(ctx)
		go func() {
			defer close(driverDone)
			if err := runDriverGateway(ctx, cfg, hub, sessions, controller); err != nil {
				log.Printf("driver gateway error: %v", err)
				cancel()
			}
		}()
	} else {
		close(driverDone)
	}
	if err := runWebsocket(ctx, cfg, hub, sessions, controller); err != nil {
		cancel()
		log.Printf("websocket server error: %v", err)
	}
	cancel()
	select {
	case <-driverDone:
	case <-time.After(httpWriteTimeout):
		log.Printf("driver gateway shutdown exceeded deadline")
	}
	if cfg.DriverMode {
		select {
		case <-hub.driverWriterDone:
		case <-time.After(httpWriteTimeout):
			log.Printf("driver writer shutdown exceeded deadline")
		}
	}
}
