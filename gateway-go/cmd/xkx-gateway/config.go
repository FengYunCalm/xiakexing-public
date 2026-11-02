package main

import (
	"bufio"
	"fmt"
	"log"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

type runtimeConfig struct {
	Enabled                      bool
	ListenHost                   string
	ListenPort                   int
	TargetHost                   string
	TargetPort                   int
	DriverMode                   bool
	DriverHost                   string
	DriverPort                   int
	DriverPingInterval           int
	DriverPongTimeout            int
	DriverSendBuffer             int
	DriverAckWindow              int
	LogPath                      string
	MaxConnections               int
	WebSocketWriteTimeoutSeconds int
	WebSocketPongTimeoutSeconds  int
	WebSocketPingIntervalSeconds int
	WebSocketMaxMessageSize      int
	WebSocketSendBuffer          int
	DeliveryAuditEnabled         bool
}

var allowedGatewayKeys = map[string]struct{}{
	"enabled":                         {},
	"listen_host":                     {},
	"listen_port":                     {},
	"target_host":                     {},
	"target_port":                     {},
	"driver_mode":                     {},
	"driver_host":                     {},
	"driver_port":                     {},
	"driver_ping_interval":            {},
	"driver_pong_timeout":             {},
	"driver_send_buffer":              {},
	"driver_ack_window":               {},
	"max_connections":                 {},
	"websocket_write_timeout_seconds": {},
	"websocket_pong_timeout_seconds":  {},
	"websocket_ping_interval_seconds": {},
	"websocket_max_message_size":      {},
	"websocket_send_buffer":           {},
	"log_path":                        {},
	"delivery_audit_enabled":          {},
}

func parseKeyValueFile(path, section string, allowRoot bool) (map[string]string, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", path, err)
	}
	defer file.Close()

	values := map[string]string{}
	currentSection := ""
	scanner := bufio.NewScanner(file)
	lineNumber := 0
	for scanner.Scan() {
		lineNumber++
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
			continue
		}
		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			currentSection = strings.TrimSpace(line[1 : len(line)-1])
			continue
		}
		if currentSection != section && !(allowRoot && currentSection == "") {
			continue
		}
		separator := strings.Index(line, "=")
		if separator < 0 {
			return nil, fmt.Errorf("%s:%d: expected key = value", path, lineNumber)
		}
		key := strings.TrimSpace(line[:separator])
		value := strings.TrimSpace(line[separator+1:])
		if key == "" || value == "" {
			return nil, fmt.Errorf("%s:%d: key and value must be non-empty", path, lineNumber)
		}
		if _, ok := allowedGatewayKeys[key]; !ok {
			return nil, fmt.Errorf("%s:%d: unknown gateway key %q", path, lineNumber, key)
		}
		if _, exists := values[key]; exists {
			return nil, fmt.Errorf("%s:%d: duplicate gateway key %q", path, lineNumber, key)
		}
		values[key] = value
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("read %s: %w", path, err)
	}
	return values, nil
}

func loadRuntimeOverrides(path string) (map[string]string, error) {
	if path == "" {
		return map[string]string{}, nil
	}
	if !filepath.IsAbs(path) {
		return nil, fmt.Errorf("XK_GATEWAY_RUNTIME_CONFIG must be absolute: %s", path)
	}
	if _, err := os.Stat(path); os.IsNotExist(err) {
		return map[string]string{}, nil
	} else if err != nil {
		return nil, fmt.Errorf("stat runtime override %s: %w", path, err)
	}
	return parseKeyValueFile(path, "gateway", true)
}

func resolveConfigPath() (string, error) {
	configured := strings.TrimSpace(os.Getenv("XK_GATEWAY_CONFIG"))
	if configured == "" {
		return "", fmt.Errorf("XK_GATEWAY_CONFIG must specify an absolute gateway base config path")
	}
	if !filepath.IsAbs(configured) {
		return "", fmt.Errorf("XK_GATEWAY_CONFIG must be absolute: %s", configured)
	}
	return filepath.Clean(configured), nil
}

func loadConfig() (runtimeConfig, error) {
	configPath, err := resolveConfigPath()
	if err != nil {
		return runtimeConfig{}, err
	}
	cfg, err := parseKeyValueFile(configPath, "gateway", false)
	if err != nil {
		return runtimeConfig{}, err
	}
	if len(cfg) == 0 {
		return runtimeConfig{}, fmt.Errorf("%s: missing [gateway] configuration", configPath)
	}

	runtimeOverridePath := os.Getenv("XK_GATEWAY_RUNTIME_CONFIG")
	if os.Getenv("XK_GATEWAY_REQUIRE_RUNTIME_CONFIG") == "1" && runtimeOverridePath == "" {
		return runtimeConfig{}, fmt.Errorf("XK_GATEWAY_RUNTIME_CONFIG is required for this gateway profile")
	}
	runtimeCfg, err := loadRuntimeOverrides(runtimeOverridePath)
	if err != nil {
		return runtimeConfig{}, err
	}
	if os.Getenv("XK_GATEWAY_REQUIRE_RUNTIME_CONFIG") == "1" {
		if len(runtimeCfg) == 0 {
			return runtimeConfig{}, fmt.Errorf("required gateway runtime config is missing or empty: %s", runtimeOverridePath)
		}
		if strings.ToLower(runtimeCfg["driver_mode"]) != "true" {
			return runtimeConfig{}, fmt.Errorf("required driver runtime config must set driver_mode=true: %s", runtimeOverridePath)
		}
		for _, key := range []string{"target_port", "driver_port"} {
			if _, ok := runtimeCfg[key]; !ok {
				return runtimeConfig{}, fmt.Errorf("required driver runtime config is missing %s: %s", key, runtimeOverridePath)
			}
		}
	}

	pick := func(envKey, key, fallback string) (string, string) {
		if value := strings.TrimSpace(os.Getenv(envKey)); value != "" {
			return value, "environment " + envKey
		}
		if value, ok := runtimeCfg[key]; ok {
			return value, "runtime override " + key
		}
		if value, ok := cfg[key]; ok {
			return value, configPath + " [gateway]." + key
		}
		return fallback, "default"
	}
	positive := func(envKey, key string, fallback int) (int, error) {
		value, source := pick(envKey, key, strconv.Itoa(fallback))
		parsed, parseErr := strconv.Atoi(value)
		if parseErr != nil || parsed <= 0 {
			return 0, fmt.Errorf("invalid positive integer for %s from %s: %q", key, source, value)
		}
		return parsed, nil
	}
	port := func(envKey, key string, fallback int) (int, error) {
		parsed, parseErr := positive(envKey, key, fallback)
		if parseErr != nil {
			return 0, parseErr
		}
		if parsed > 65535 {
			return 0, fmt.Errorf("invalid port for %s: %d is outside 1..65535", key, parsed)
		}
		return parsed, nil
	}
	boolean := func(envKey, key string, fallback bool) (bool, error) {
		value, source := pick(envKey, key, strconv.FormatBool(fallback))
		switch strings.ToLower(value) {
		case "1", "true", "yes", "on":
			return true, nil
		case "0", "false", "no", "off":
			return false, nil
		default:
			return false, fmt.Errorf("invalid boolean for %s from %s: %q", key, source, value)
		}
	}

	enabled, err := boolean("XK_GATEWAY_ENABLED", "enabled", true)
	if err != nil {
		return runtimeConfig{}, err
	}
	listenPort, err := port("XK_GATEWAY_PORT", "listen_port", 8080)
	if err != nil {
		return runtimeConfig{}, err
	}
	targetPort, err := port("XK_GATEWAY_TARGET_PORT", "target_port", 3939)
	if err != nil {
		return runtimeConfig{}, err
	}
	driverPort, err := port("XK_GATEWAY_DRIVER_PORT", "driver_port", 6040)
	if err != nil {
		return runtimeConfig{}, err
	}
	driverPingInterval, err := positive("XK_GATEWAY_DRIVER_PING_INTERVAL", "driver_ping_interval", 15)
	if err != nil {
		return runtimeConfig{}, err
	}
	driverPongTimeout, err := positive("XK_GATEWAY_DRIVER_PONG_TIMEOUT", "driver_pong_timeout", 45)
	if err != nil {
		return runtimeConfig{}, err
	}
	driverSendBuffer, err := positive("XK_GATEWAY_DRIVER_SEND_BUFFER", "driver_send_buffer", defaultDriverSendBuffer)
	if err != nil {
		return runtimeConfig{}, err
	}
	driverAckWindow, err := positive("XK_GATEWAY_DRIVER_ACK_WINDOW", "driver_ack_window", defaultDriverAckWindow)
	if err != nil {
		return runtimeConfig{}, err
	}
	maxConnections, err := positive("XK_GATEWAY_MAX_CONNECTIONS", "max_connections", 128)
	if err != nil {
		return runtimeConfig{}, err
	}
	writeTimeout, err := positive("XK_GATEWAY_WEBSOCKET_WRITE_TIMEOUT_SECONDS", "websocket_write_timeout_seconds", defaultWebSocketWriteTimeoutSeconds)
	if err != nil {
		return runtimeConfig{}, err
	}
	pongTimeout, err := positive("XK_GATEWAY_WEBSOCKET_PONG_TIMEOUT_SECONDS", "websocket_pong_timeout_seconds", defaultWebSocketPongTimeoutSeconds)
	if err != nil {
		return runtimeConfig{}, err
	}
	pingInterval, err := positive("XK_GATEWAY_WEBSOCKET_PING_INTERVAL_SECONDS", "websocket_ping_interval_seconds", defaultWebSocketPingIntervalSeconds)
	if err != nil {
		return runtimeConfig{}, err
	}
	maxMessageSize, err := positive("XK_GATEWAY_WEBSOCKET_MAX_MESSAGE_SIZE", "websocket_max_message_size", defaultWebSocketMaxMessageSize)
	if err != nil {
		return runtimeConfig{}, err
	}
	if maxMessageSize < maxCommandEnvelopeBytes {
		return runtimeConfig{}, fmt.Errorf(
			"websocket_max_message_size must be at least %d bytes for command envelopes: %d",
			maxCommandEnvelopeBytes,
			maxMessageSize,
		)
	}
	sendBuffer, err := positive("XK_GATEWAY_WEBSOCKET_SEND_BUFFER", "websocket_send_buffer", defaultWebSocketSendBuffer)
	if err != nil {
		return runtimeConfig{}, err
	}

	driverMode, err := boolean("XK_GATEWAY_DRIVER_MODE", "driver_mode", false)
	if err != nil {
		return runtimeConfig{}, err
	}
	if os.Getenv("XK_GATEWAY_REQUIRE_RUNTIME_CONFIG") == "1" && !driverMode {
		return runtimeConfig{}, fmt.Errorf(
			"required gateway runtime config must leave effective driver_mode=true")
	}
	deliveryAuditEnabled, err := boolean("XK_GATEWAY_DELIVERY_AUDIT_ENABLED", "delivery_audit_enabled", false)
	if err != nil {
		return runtimeConfig{}, err
	}

	listenHost, _ := pick("XK_GATEWAY_HOST", "listen_host", "127.0.0.1")
	targetHost, _ := pick("XK_GATEWAY_TARGET_HOST", "target_host", "127.0.0.1")
	driverHost, _ := pick("XK_GATEWAY_DRIVER_HOST", "driver_host", "127.0.0.1")
	for _, h := range []struct {
		host   string
		source string
	}{
		{listenHost, "listen_host"},
		{targetHost, "target_host"},
		{driverHost, "driver_host"},
	} {
		ip := net.ParseIP(h.host)
		if h.host != "localhost" && (ip == nil || !ip.IsLoopback()) {
			return runtimeConfig{}, fmt.Errorf(
				"非 loopback 绑定 %q（%s）：Gateway 对外只经 Nginx 反代 /ws，监听与后端目标必须留在 loopback",
				h.host, h.source)
		}
	}
	logPath, logSource := pick("XK_GATEWAY_LOG", "log_path", "server/data/log/gateway-go.log")
	if !filepath.IsAbs(logPath) {
		return runtimeConfig{}, fmt.Errorf("log_path from %s must be absolute: %s", logSource, logPath)
	}
	return runtimeConfig{
		Enabled:                      enabled,
		ListenHost:                   listenHost,
		ListenPort:                   listenPort,
		TargetHost:                   targetHost,
		TargetPort:                   targetPort,
		DriverMode:                   driverMode,
		DriverHost:                   driverHost,
		DriverPort:                   driverPort,
		DriverPingInterval:           driverPingInterval,
		DriverPongTimeout:            driverPongTimeout,
		DriverSendBuffer:             driverSendBuffer,
		DriverAckWindow:              driverAckWindow,
		LogPath:                      logPath,
		MaxConnections:               maxConnections,
		WebSocketWriteTimeoutSeconds: writeTimeout,
		WebSocketPongTimeoutSeconds:  pongTimeout,
		WebSocketPingIntervalSeconds: pingInterval,
		WebSocketMaxMessageSize:      maxMessageSize,
		WebSocketSendBuffer:          sendBuffer,
		DeliveryAuditEnabled:         deliveryAuditEnabled,
	}, nil
}

func configureLogging(cfg runtimeConfig) {
	_ = os.MkdirAll(filepath.Dir(cfg.LogPath), 0o755)
	file, err := os.OpenFile(cfg.LogPath, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		log.Fatalf("open log failed: %v", err)
	}
	log.SetOutput(file)
	log.SetFlags(log.LstdFlags | log.Lmicroseconds)
}
