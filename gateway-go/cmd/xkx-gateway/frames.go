package main

import "time"

type driverFrame struct {
	Op          string `json:"op"`
	SessionID   string `json:"session_id"`
	Type        string `json:"type"`
	PayloadJSON string `json:"payload_json"`
	Timestamp   int64  `json:"timestamp"`
}

type sessionInputFrame struct {
	Op        string `json:"op"`
	SessionID string `json:"session_id"`
	Payload   string `json:"payload"`
}

type sessionControlFrame struct {
	Op        string `json:"op"`
	SessionID string `json:"session_id"`
}

type driverGatewayFrame struct {
	Type       string      `json:"type"`
	CID        string      `json:"cid,omitempty"`
	Action     string      `json:"action,omitempty"`
	TS         int64       `json:"ts,omitempty"`
	IngressID  string      `json:"ingress_id,omitempty"`
	IngressSeq uint64      `json:"ingress_seq,omitempty"`
	Data       interface{} `json:"data,omitempty"`
}

const (
	defaultWebSocketWriteTimeoutSeconds = 10
	defaultWebSocketPongTimeoutSeconds  = 60
	defaultWebSocketPingIntervalSeconds = 54
	defaultWebSocketMaxMessageSize      = 1024 * 1024
	defaultWebSocketSendBuffer          = 256
)

type websocketRuntimeConfig struct {
	writeTimeout   time.Duration
	pongTimeout    time.Duration
	pingInterval   time.Duration
	maxMessageSize int64
	sendBuffer     int
}
