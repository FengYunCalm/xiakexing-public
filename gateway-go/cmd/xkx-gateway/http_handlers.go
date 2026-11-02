package main

import (
	"fmt"
	"net"
	"net/http"
	"net/http/pprof"
	"sort"
	"strings"
	"time"
)

func defaultWebSocketRuntimeConfig() websocketRuntimeConfig {
	return websocketRuntimeConfig{
		writeTimeout:   time.Duration(defaultWebSocketWriteTimeoutSeconds) * time.Second,
		pongTimeout:    time.Duration(defaultWebSocketPongTimeoutSeconds) * time.Second,
		pingInterval:   time.Duration(defaultWebSocketPingIntervalSeconds) * time.Second,
		maxMessageSize: int64(defaultWebSocketMaxMessageSize),
		sendBuffer:     defaultWebSocketSendBuffer,
	}
}

func (cfg runtimeConfig) websocketRuntimeConfig() websocketRuntimeConfig {
	ws := websocketRuntimeConfig{
		writeTimeout:   time.Duration(cfg.WebSocketWriteTimeoutSeconds) * time.Second,
		pongTimeout:    time.Duration(cfg.WebSocketPongTimeoutSeconds) * time.Second,
		pingInterval:   time.Duration(cfg.WebSocketPingIntervalSeconds) * time.Second,
		maxMessageSize: int64(cfg.WebSocketMaxMessageSize),
		sendBuffer:     cfg.WebSocketSendBuffer,
	}
	if ws.writeTimeout <= 0 {
		ws.writeTimeout = time.Duration(defaultWebSocketWriteTimeoutSeconds) * time.Second
	}
	if ws.pongTimeout <= 0 {
		ws.pongTimeout = time.Duration(defaultWebSocketPongTimeoutSeconds) * time.Second
	}
	if ws.pingInterval <= 0 || ws.pingInterval >= ws.pongTimeout {
		ws.pingInterval = (ws.pongTimeout * 9) / 10
	}
	if ws.pingInterval <= 0 {
		ws.pingInterval = time.Second
	}
	if ws.maxMessageSize <= 0 {
		ws.maxMessageSize = int64(defaultWebSocketMaxMessageSize)
	}
	if ws.sendBuffer <= 0 {
		ws.sendBuffer = defaultWebSocketSendBuffer
	}
	return ws
}

func handleHealthz(cfg runtimeConfig, hub *driverHub) http.HandlerFunc {
	return func(w http.ResponseWriter, _ *http.Request) {
		if cfg.DriverMode && !hub.healthy(driverPongTimeout(cfg)) {
			http.Error(w, "driver gateway unhealthy", http.StatusServiceUnavailable)
			return
		}
		w.Header().Set("Content-Type", "application/json; charset=utf-8")
		_, _ = w.Write(append(gatewayBuildIdentityJSON(), '\n'))
	}
}

func handleMetrics(cfg runtimeConfig, hub *driverHub, sessions *sessionRegistry, controller *driverDeliveryController) http.HandlerFunc {
	return func(w http.ResponseWriter, _ *http.Request) {
		driverMode := 0
		if cfg.DriverMode {
			driverMode = 1
		}
		driverConnected := 0
		if cfg.DriverMode && hub.healthy(driverPongTimeout(cfg)) {
			driverConnected = 1
		}
		w.Header().Set("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
		writeGatewayBuildMetrics(w)
		fmt.Fprintf(w, "# HELP xkx_gateway_sessions_active Active WebSocket sessions.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_sessions_active gauge\n")
		fmt.Fprintf(w, "xkx_gateway_sessions_active %d\n", sessions.count())
		fmt.Fprintf(w, "# HELP xkx_gateway_driver_mode Whether driver gateway mode is enabled.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_driver_mode gauge\n")
		fmt.Fprintf(w, "xkx_gateway_driver_mode %d\n", driverMode)
		fmt.Fprintf(w, "# HELP xkx_gateway_driver_connected Whether the active driver connection is established.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_driver_connected gauge\n")
		fmt.Fprintf(w, "xkx_gateway_driver_connected %d\n", driverConnected)
		fmt.Fprintf(w, "# HELP xkx_gateway_connections_total Accepted WebSocket connections.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_connections_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_connections_total %d\n", gatewayMetrics.connectionsTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_connections_rejected_total Connections rejected by the gateway limit.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_connections_rejected_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_connections_rejected_total %d\n", gatewayMetrics.connectionsRejectedTotal.Load())
		if cfg.DriverMode {
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_frames_in_total Frames received from the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_in_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_in_total %d\n", gatewayMetrics.driverFramesInTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_output_frames_in_total Output frames received from the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_output_frames_in_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_output_frames_in_total %d\n", gatewayMetrics.driverOutputFramesInTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_sys_frames_in_total System frames received from the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_sys_frames_in_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_sys_frames_in_total %d\n", gatewayMetrics.driverSysFramesInTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_ingress_ack_frames_in_total Cumulative ingress ACK frames received from the driver.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_ingress_ack_frames_in_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_ingress_ack_frames_in_total %d\n", gatewayMetrics.driverIngressAckFramesInTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_non_output_frames_in_total Non-output non-system frames received from the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_non_output_frames_in_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_non_output_frames_in_total %d\n", gatewayMetrics.driverNonOutputFramesInTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_frames_out_total Frames sent to the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_out_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_out_total %d\n", gatewayMetrics.driverFramesOutTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_data_frames_out_total Data frames sent to the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_data_frames_out_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_data_frames_out_total %d\n", gatewayMetrics.driverDataFramesOutTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_login_frames_out_total Login frames sent to the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_login_frames_out_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_login_frames_out_total %d\n", gatewayMetrics.driverLoginFramesOutTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_discon_frames_out_total Disconnect frames sent to the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_discon_frames_out_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_discon_frames_out_total %d\n", gatewayMetrics.driverDisconFramesOutTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_sys_frames_out_total System frames sent to the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_sys_frames_out_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_sys_frames_out_total %d\n", gatewayMetrics.driverSysFramesOutTotal.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_write_errors_total Failed writes to the driver gateway connection.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_write_errors_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_write_errors_total %d\n", gatewayMetrics.driverWriteErrorsTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_admitted_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_admitted_total %d\n", gatewayMetrics.driverFramesAdmittedTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_written_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_written_total %d\n", gatewayMetrics.driverFramesWrittenTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_write_unknown_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_write_unknown_total %d\n", gatewayMetrics.driverFramesWriteUnknownTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_abandoned_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_abandoned_total %d\n", gatewayMetrics.driverFramesAbandonedTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_queue_rejected_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_queue_rejected_total %d\n", gatewayMetrics.driverFramesQueueRejectedTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_safe_retries_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_safe_retries_total %d\n", gatewayMetrics.driverFramesSafeRetriesTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_admission_waiting gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_admission_waiting %d\n", gatewayMetrics.driverFramesAdmissionWaiting.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_queued gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_queued %d\n", gatewayMetrics.driverFramesQueued.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_inflight gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_inflight %d\n", gatewayMetrics.driverFramesInflight.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_queue_peak gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_queue_peak %d\n", gatewayMetrics.driverFramesQueuePeak.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_writes_active gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_writes_active %d\n", gatewayMetrics.driverWritesActive.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_unacked gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_unacked %d\n", gatewayMetrics.driverFramesUnacked.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_unacked_peak gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_unacked_peak %d\n", gatewayMetrics.driverFramesUnackedPeak.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_ack_window_capacity Maximum reliable driver frames admitted without a cumulative ACK.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_ack_window_capacity gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_ack_window_capacity %d\n", hub.driverAckWindowCapacity())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_ack_window_used Reliable driver frames admitted and not yet cumulatively acknowledged.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_ack_window_used gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_ack_window_used %d\n", hub.driverAckWindowUsed())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_physical_writes_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_physical_writes_total %d\n", gatewayMetrics.driverFramesPhysicalWritesTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_frames_replayed_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_frames_replayed_total %d\n", gatewayMetrics.driverFramesReplayedTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_ingress_ack_frames_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_ingress_ack_frames_total %d\n", gatewayMetrics.driverIngressAckFramesTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_ingress_ack_rejected_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_ingress_ack_rejected_total %d\n", gatewayMetrics.driverIngressAckRejectedTotal.Load())
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_ingress_last_acked gauge\n")
			fmt.Fprintf(w, "xkx_gateway_driver_ingress_last_acked %d\n", gatewayMetrics.driverIngressLastAcked.Load())
			fmt.Fprintf(w, "# HELP xkx_gateway_driver_decode_errors_total Driver gateway frames rejected by JSON or output-envelope validation.\n")
			fmt.Fprintf(w, "# TYPE xkx_gateway_driver_decode_errors_total counter\n")
			fmt.Fprintf(w, "xkx_gateway_driver_decode_errors_total %d\n", gatewayMetrics.driverDecodeErrorsTotal.Load())
			writeDriverDeliveryMetrics(w, controller)
			writeDriverWriteTimingMetrics(w)
			writeDriverRuntimeStatusMetrics(w)
		}
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_write_pumps_active gauge\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_write_pumps_active %d\n", gatewayMetrics.websocketWritePumpsActive.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_messages_in_total Messages received from WebSocket clients.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_messages_in_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_in_total %d\n", gatewayMetrics.inboundMessagesTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_pings_sent_total Ping control frames written to WebSocket clients.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_pings_sent_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_pings_sent_total %d\n", gatewayMetrics.websocketPingsSentTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_pongs_received_total Pong control frames received from WebSocket clients.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_pongs_received_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_pongs_received_total %d\n", gatewayMetrics.websocketPongsReceivedTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_inbound_deadline_refreshes_total Valid client messages that renewed the WebSocket read deadline.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_inbound_deadline_refreshes_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_inbound_deadline_refreshes_total %d\n", gatewayMetrics.websocketInboundDeadlineRefreshesTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_read_timeouts_total WebSocket reads terminated by the configured liveness deadline.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_read_timeouts_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_read_timeouts_total %d\n", gatewayMetrics.websocketReadTimeoutsTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_peer_closes_total WebSocket reads terminated by a peer close frame or EOF.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_peer_closes_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_peer_closes_total %d\n", gatewayMetrics.websocketPeerClosesTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_read_errors_total WebSocket reads terminated by a non-timeout, non-close error.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_read_errors_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_read_errors_total %d\n", gatewayMetrics.websocketReadErrorsTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_read_deadline_errors_total Failures while installing or renewing a WebSocket read deadline.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_read_deadline_errors_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_read_deadline_errors_total %d\n", gatewayMetrics.websocketReadDeadlineErrorsTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_messages_out_total Text messages sent to WebSocket clients.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_messages_out_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_out_total %d\n", gatewayMetrics.outboundMessagesTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_messages_dropped_total Outbound messages dropped because the WebSocket send queue was full.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_messages_dropped_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_dropped_total %d\n", gatewayMetrics.outboundDropsTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_messages_collapsed_total Outbound state or ephemeral messages replaced by a newer frame with the same collapse key.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_messages_collapsed_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_collapsed_total %d\n", gatewayMetrics.outboundCollapsedTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_messages_collapsed_by_reliability_total Outbound collapsed messages grouped by the replaced frame reliability class.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_messages_collapsed_by_reliability_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_collapsed_by_reliability_total{reliability=\"critical\"} %d\n", gatewayMetrics.outboundCriticalCollapsedTotal.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_collapsed_by_reliability_total{reliability=\"important\"} %d\n", gatewayMetrics.outboundImportantCollapsedTotal.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_collapsed_by_reliability_total{reliability=\"state\"} %d\n", gatewayMetrics.outboundStateCollapsedTotal.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_collapsed_by_reliability_total{reliability=\"ephemeral\"} %d\n", gatewayMetrics.outboundEphemeralCollapsedTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_messages_dropped_by_reliability_total Outbound queue-full drops grouped by XK reliability class.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_messages_dropped_by_reliability_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_dropped_by_reliability_total{reliability=\"critical\"} %d\n", gatewayMetrics.outboundCriticalDropsTotal.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_dropped_by_reliability_total{reliability=\"important\"} %d\n", gatewayMetrics.outboundImportantDropsTotal.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_dropped_by_reliability_total{reliability=\"state\"} %d\n", gatewayMetrics.outboundStateDropsTotal.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_messages_dropped_by_reliability_total{reliability=\"ephemeral\"} %d\n", gatewayMetrics.outboundEphemeralDropsTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_late_frames_total Outbound frames received after the WebSocket session was already removed.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_late_frames_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_late_frames_total %d\n", gatewayMetrics.outboundLateFramesTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_closed_session_writes_total Outbound frames rejected because the session was already closed.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_closed_session_writes_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_closed_session_writes_total %d\n", gatewayMetrics.outboundClosedTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_queue_full_total Outbound frames rejected because the per-session send queue was full.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_queue_full_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_queue_full_total %d\n", gatewayMetrics.outboundQueueFullTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_send_queue_depth Current total queued outbound WebSocket messages.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_send_queue_depth gauge\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_depth %d\n", sessions.queuedMessages())
		priorityDepth := sessions.queuedMessagesByPriority()
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_send_queue_depth_by_priority Current queued outbound WebSocket messages grouped by XK priority.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_send_queue_depth_by_priority gauge\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_depth_by_priority{priority=\"critical\"} %d\n", priorityDepth["critical"])
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_depth_by_priority{priority=\"high\"} %d\n", priorityDepth["high"])
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_depth_by_priority{priority=\"normal\"} %d\n", priorityDepth["normal"])
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_depth_by_priority{priority=\"low\"} %d\n", priorityDepth["low"])
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_send_queue_peak Highest observed per-session outbound queue depth.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_send_queue_peak gauge\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_peak %d\n", gatewayMetrics.outboundQueuePeak.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_send_queue_peak_by_priority Highest observed per-session outbound queue depth grouped by XK priority.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_send_queue_peak_by_priority gauge\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_peak_by_priority{priority=\"critical\"} %d\n", gatewayMetrics.outboundQueueCriticalPeak.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_peak_by_priority{priority=\"high\"} %d\n", gatewayMetrics.outboundQueueHighPeak.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_peak_by_priority{priority=\"normal\"} %d\n", gatewayMetrics.outboundQueueNormalPeak.Load())
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_peak_by_priority{priority=\"low\"} %d\n", gatewayMetrics.outboundQueueLowPeak.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_write_errors_total WebSocket write or ping failures.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_write_errors_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_write_errors_total %d\n", gatewayMetrics.outboundErrorsTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_send_queue_dwell_ms_total Total milliseconds outbound WebSocket messages spent queued before write processing.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_send_queue_dwell_ms_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_dwell_ms_total %.3f\n", outboundQueueDwellMilliseconds(gatewayMetrics.outboundQueueDwellUsTotal.Load()))
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_send_queue_dwell_samples_total Count of outbound WebSocket messages measured at queue dequeue.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_send_queue_dwell_samples_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_dwell_samples_total %d\n", gatewayMetrics.outboundQueueDwellSamples.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_send_queue_dwell_max_ms Maximum observed outbound WebSocket queue dwell in milliseconds.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_send_queue_dwell_max_ms gauge\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_send_queue_dwell_max_ms %.3f\n", outboundQueueDwellMilliseconds(gatewayMetrics.outboundQueueDwellMaxUs.Load()))
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_write_latency_ms_total Total elapsed milliseconds spent writing outbound WebSocket messages.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_write_latency_ms_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_write_latency_ms_total %d\n", gatewayMetrics.outboundWriteLatencyMsTotal.Load())
		fmt.Fprintf(w, "# HELP xkx_gateway_websocket_write_latency_samples_total Count of measured outbound WebSocket writes.\n")
		fmt.Fprintf(w, "# TYPE xkx_gateway_websocket_write_latency_samples_total counter\n")
		fmt.Fprintf(w, "xkx_gateway_websocket_write_latency_samples_total %d\n", gatewayMetrics.outboundWriteLatencySamples.Load())
	}
}

func writeDriverDeliveryMetrics(w http.ResponseWriter, controller *driverDeliveryController) {
	fmt.Fprintf(w, "# HELP xkx_gateway_driver_delivery_received_total Valid driver output envelopes accepted for session routing.\n")
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_received_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_received_total %d\n", gatewayMetrics.driverDeliveryReceivedTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_enqueued_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_enqueued_total %d\n", gatewayMetrics.driverDeliveryEnqueuedTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_written_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_written_total %d\n", gatewayMetrics.driverDeliveryWrittenTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_queue_rejected_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_queue_rejected_total %d\n", gatewayMetrics.driverDeliveryQueueRejectedTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_late_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_late_total %d\n", gatewayMetrics.driverDeliveryLateTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_closed_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_closed_total %d\n", gatewayMetrics.driverDeliveryClosedTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_write_errors_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_write_errors_total %d\n", gatewayMetrics.driverDeliveryWriteErrorsTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_collapsed_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_collapsed_total %d\n", gatewayMetrics.driverDeliveryCollapsedTotal.Load())
	fmt.Fprintf(w, "# HELP xkx_gateway_driver_delivery_collapsed_by_reliability_total Driver delivery collapses grouped by the replaced frame reliability class.\n")
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_collapsed_by_reliability_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_collapsed_by_reliability_total{reliability=\"critical\"} %d\n", gatewayMetrics.driverDeliveryCollapsedCriticalTotal.Load())
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_collapsed_by_reliability_total{reliability=\"important\"} %d\n", gatewayMetrics.driverDeliveryCollapsedImportantTotal.Load())
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_collapsed_by_reliability_total{reliability=\"state\"} %d\n", gatewayMetrics.driverDeliveryCollapsedStateTotal.Load())
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_collapsed_by_reliability_total{reliability=\"ephemeral\"} %d\n", gatewayMetrics.driverDeliveryCollapsedEphemeralTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_expired_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_expired_total %d\n", gatewayMetrics.driverDeliveryExpiredTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_abandoned_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_abandoned_total %d\n", gatewayMetrics.driverDeliveryAbandonedTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_admission_rejected_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_admission_rejected_total %d\n", gatewayMetrics.driverDeliveryAdmissionRejectedTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_queued gauge\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_queued %d\n", gatewayMetrics.driverDeliveryQueued.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_inflight gauge\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_inflight %d\n", gatewayMetrics.driverDeliveryInflight.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_decode_submitted_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_decode_submitted_total %d\n", gatewayMetrics.driverDecodeSubmittedTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_decode_committed_total counter\n")
	fmt.Fprintf(w, "xkx_gateway_driver_decode_committed_total %d\n", gatewayMetrics.driverDecodeCommittedTotal.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_decode_inflight gauge\n")
	fmt.Fprintf(w, "xkx_gateway_driver_decode_inflight %d\n", gatewayMetrics.driverDecodeInflight.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_admission_frozen gauge\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_admission_frozen %d\n", gatewayMetrics.driverDeliveryAdmissionFrozen.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_admission_active gauge\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_admission_active %d\n", gatewayMetrics.driverDeliveryAdmissionActive.Load())
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_delivery_active_writers gauge\n")
	fmt.Fprintf(w, "xkx_gateway_driver_delivery_active_writers %d\n", gatewayMetrics.driverDeliveryActiveWriters.Load())
}

func writeDriverWriteTimingMetrics(w http.ResponseWriter) {
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_write_wait_us_total counter\n")
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_write_wait_samples_total counter\n")
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_write_wait_max_us gauge\n")
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_write_duration_us_total counter\n")
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_write_duration_samples_total counter\n")
	fmt.Fprintf(w, "# TYPE xkx_gateway_driver_write_duration_max_us gauge\n")
	for _, frameType := range []string{"data", "login", "discon", "other"} {
		index, _ := driverFrameClass(frameType)
		wait := &gatewayMetrics.driverWriteWait[index]
		write := &gatewayMetrics.driverWriteDuration[index]
		fmt.Fprintf(w, "xkx_gateway_driver_write_wait_us_total{frame_type=%q} %d\n", frameType, wait.totalUs.Load())
		fmt.Fprintf(w, "xkx_gateway_driver_write_wait_samples_total{frame_type=%q} %d\n", frameType, wait.samples.Load())
		fmt.Fprintf(w, "xkx_gateway_driver_write_wait_max_us{frame_type=%q} %d\n", frameType, wait.maxUs.Load())
		fmt.Fprintf(w, "xkx_gateway_driver_write_duration_us_total{frame_type=%q} %d\n", frameType, write.totalUs.Load())
		fmt.Fprintf(w, "xkx_gateway_driver_write_duration_samples_total{frame_type=%q} %d\n", frameType, write.samples.Load())
		fmt.Fprintf(w, "xkx_gateway_driver_write_duration_max_us{frame_type=%q} %d\n", frameType, write.maxUs.Load())
	}
}

func prometheusMetricSuffix(value string) string {
	var builder strings.Builder
	for _, ch := range value {
		if (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' {
			builder.WriteRune(ch)
		} else {
			builder.WriteByte('_')
		}
	}
	if builder.Len() == 0 {
		return "unknown"
	}
	return builder.String()
}

func writeDriverRuntimeStatusMetrics(w http.ResponseWriter) {
	values, updatedUnix := driverRuntimeStatusSnapshot()
	fmt.Fprintf(w, "# HELP xkx_driver_runtime_status_last_updated_unix Unix timestamp of the latest driver gateway runtime status frame.\n")
	fmt.Fprintf(w, "# TYPE xkx_driver_runtime_status_last_updated_unix gauge\n")
	fmt.Fprintf(w, "xkx_driver_runtime_status_last_updated_unix %d\n", updatedUnix)
	if len(values) == 0 {
		return
	}

	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	for _, key := range keys {
		name := "xkx_driver_" + prometheusMetricSuffix(key)
		fmt.Fprintf(w, "# TYPE %s gauge\n", name)
		fmt.Fprintf(w, "%s %.6f\n", name, values[key])
	}
}

func driverPongTimeout(cfg runtimeConfig) time.Duration {
	timeout := time.Duration(cfg.DriverPongTimeout) * time.Second
	if timeout <= 0 {
		interval := time.Duration(cfg.DriverPingInterval) * time.Second
		if interval <= 0 {
			return 0
		}
		return interval * 3
	}
	return timeout
}

func isLoopbackRequest(r *http.Request) bool {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}
	return isLoopbackHost(host)
}

func localOnly(handler http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !isLoopbackRequest(r) {
			http.NotFound(w, r)
			return
		}
		handler(w, r)
	}
}

func registerPprofHandlers(mux *http.ServeMux) {
	mux.HandleFunc("/debug/pprof/", localOnly(pprof.Index))
	mux.HandleFunc("/debug/pprof/cmdline", localOnly(pprof.Cmdline))
	mux.HandleFunc("/debug/pprof/profile", localOnly(pprof.Profile))
	mux.HandleFunc("/debug/pprof/symbol", localOnly(pprof.Symbol))
	mux.HandleFunc("/debug/pprof/trace", localOnly(pprof.Trace))
}

func isWebSocketUpgradeRequest(r *http.Request) bool {
	return strings.EqualFold(r.Header.Get("Upgrade"), "websocket") && strings.Contains(strings.ToLower(r.Header.Get("Connection")), "upgrade")
}
