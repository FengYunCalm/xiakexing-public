package main

import (
	"errors"
	"net"
	"time"

	"github.com/gorilla/websocket"
)

func refreshWebSocketReadDeadline(conn *websocket.Conn, ws websocketRuntimeConfig) error {
	if err := conn.SetReadDeadline(time.Now().Add(ws.pongTimeout)); err != nil {
		gatewayMetrics.websocketReadDeadlineErrorsTotal.Add(1)
		return err
	}
	return nil
}

func configureWebSocketRead(conn *websocket.Conn, ws websocketRuntimeConfig) {
	conn.SetReadLimit(ws.maxMessageSize)
	_ = refreshWebSocketReadDeadline(conn, ws)
	conn.SetPongHandler(func(string) error {
		gatewayMetrics.websocketPongsReceivedTotal.Add(1)
		return refreshWebSocketReadDeadline(conn, ws)
	})
}

func readWebSocketMessage(
	conn *websocket.Conn,
	ws websocketRuntimeConfig,
) (int, []byte, error) {
	if err := refreshWebSocketReadDeadline(conn, ws); err != nil {
		return 0, nil, err
	}
	gatewayMetrics.websocketInboundDeadlineRefreshesTotal.Add(1)
	messageType, message, err := conn.ReadMessage()
	if err != nil {
		recordWebSocketReadTerminal(err)
		return 0, nil, err
	}
	return messageType, message, nil
}

func recordWebSocketReadTerminal(err error) {
	var networkError net.Error
	if errors.As(err, &networkError) && networkError.Timeout() {
		gatewayMetrics.websocketReadTimeoutsTotal.Add(1)
		return
	}
	var closeError *websocket.CloseError
	if errors.As(err, &closeError) {
		gatewayMetrics.websocketPeerClosesTotal.Add(1)
		return
	}
	gatewayMetrics.websocketReadErrorsTotal.Add(1)
}
