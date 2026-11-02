package main

import (
	"context"
	"fmt"
	"log"
	"net"
	"strconv"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const directProxyReadPollInterval = 500 * time.Millisecond

func runDirectProxy(ctx context.Context, sessionID string, conn *websocket.Conn, cfg runtimeConfig, sessions *sessionRegistry) {
	if ctx == nil {
		ctx = context.Background()
	}
	ws := cfg.websocketRuntimeConfig()
	dialer := net.Dialer{}
	tcpConn, err := dialer.DialContext(ctx, "tcp", net.JoinHostPort(cfg.TargetHost, strconv.Itoa(cfg.TargetPort)))
	if err != nil {
		log.Printf("direct proxy dial failed: %v", err)
		_ = conn.Close()
		return
	}
	enableTCPNoDelay(tcpConn)
	closeTransport := func() {
		_ = tcpConn.Close()
		_ = conn.Close()
	}
	if _, err := tcpConn.Write([]byte(fmt.Sprintf("XKSESS %s\n", sessionID))); err != nil {
		log.Printf("gateway session sync failed for %s: %v", sessionID, err)
		closeTransport()
		return
	}
	adapter := newTCPToWebSocketTextAdapter()
	proxyCtx, cancel := context.WithCancel(ctx)
	defer cancel()
	var closeOnce sync.Once
	shutdown := func() {
		closeOnce.Do(closeTransport)
		cancel()
	}
	proxyDone := make(chan struct{})
	go func() {
		select {
		case <-ctx.Done():
			shutdown()
		case <-proxyDone:
		}
	}()
	defer close(proxyDone)

	var wg sync.WaitGroup
	wg.Add(2)
	go func() {
		defer wg.Done()
		defer shutdown()
		for {
			_, message, err := readWebSocketMessage(conn, ws)
			if err != nil {
				return
			}
			select {
			case <-proxyCtx.Done():
				return
			default:
			}
			gatewayMetrics.inboundMessagesTotal.Add(1)
			transportPayload, typed, envelopeErr := typedCommandTransportPayload(message)
			if envelopeErr != nil {
				log.Printf("invalid command envelope from %s: %v", sessionID, envelopeErr)
				return
			}
			if typed {
				message = transportPayload
			}
			_ = tcpConn.SetWriteDeadline(time.Now().Add(tcpWriteTimeout))
			if _, err := tcpConn.Write(message); err != nil {
				return
			}
			_ = tcpConn.SetWriteDeadline(time.Time{})
		}
	}()
	go func() {
		defer wg.Done()
		defer shutdown()
		buf := make([]byte, 4096)
		for {
			_ = tcpConn.SetReadDeadline(time.Now().Add(directProxyReadPollInterval))
			n, err := tcpConn.Read(buf)
			if n > 0 {
				message, decodeErr := adapter.feedUTF8(buf[:n], false)
				if decodeErr != nil {
					log.Printf("direct proxy invalid UTF-8 for %s: %v", sessionID, decodeErr)
					return
				}
				if message != "" && !sessions.writeText(sessionID, []byte(message)) {
					return
				}
			}
			if err != nil {
				if netErr, ok := err.(net.Error); ok && netErr.Timeout() {
					select {
					case <-proxyCtx.Done():
						return
					default:
					}
					continue
				}
				if finishErr := adapter.finishUTF8(); finishErr != nil {
					log.Printf("direct proxy truncated UTF-8 for %s: %v", sessionID, finishErr)
				}
				return
			}
		}
	}()
	wg.Wait()
}
