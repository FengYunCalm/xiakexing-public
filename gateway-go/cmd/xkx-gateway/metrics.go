package main

import (
	"fmt"
	"io"
	"math"
	"strconv"
	"sync"
	"sync/atomic"
	"time"
)

type gatewayMetricSet struct {
	connectionsTotal                       atomic.Uint64
	connectionsRejectedTotal               atomic.Uint64
	driverFramesInTotal                    atomic.Uint64
	driverOutputFramesInTotal              atomic.Uint64
	driverSysFramesInTotal                 atomic.Uint64
	driverIngressAckFramesInTotal          atomic.Uint64
	driverNonOutputFramesInTotal           atomic.Uint64
	driverFramesOutTotal                   atomic.Uint64
	driverDataFramesOutTotal               atomic.Uint64
	driverLoginFramesOutTotal              atomic.Uint64
	driverDisconFramesOutTotal             atomic.Uint64
	driverSysFramesOutTotal                atomic.Uint64
	driverWriteErrorsTotal                 atomic.Uint64
	driverFramesAdmittedTotal              atomic.Uint64
	driverFramesWrittenTotal               atomic.Uint64
	driverFramesWriteUnknownTotal          atomic.Uint64
	driverFramesAbandonedTotal             atomic.Uint64
	driverFramesQueueRejectedTotal         atomic.Uint64
	driverFramesSafeRetriesTotal           atomic.Uint64
	driverFramesAdmissionWaiting           atomic.Uint64
	driverFramesQueued                     atomic.Uint64
	driverFramesInflight                   atomic.Uint64
	driverFramesQueuePeak                  atomic.Uint64
	driverWritesActive                     atomic.Uint64
	driverFramesUnacked                    atomic.Uint64
	driverFramesUnackedPeak                atomic.Uint64
	driverFramesPhysicalWritesTotal        atomic.Uint64
	driverFramesReplayedTotal              atomic.Uint64
	driverIngressAckFramesTotal            atomic.Uint64
	driverIngressAckRejectedTotal          atomic.Uint64
	driverIngressLastAcked                 atomic.Uint64
	driverDecodeErrorsTotal                atomic.Uint64
	driverDecodeSubmittedTotal             atomic.Uint64
	driverDecodeCommittedTotal             atomic.Uint64
	driverDecodeInflight                   atomic.Uint64
	driverDeliveryReceivedTotal            atomic.Uint64
	driverDeliveryEnqueuedTotal            atomic.Uint64
	driverDeliveryWrittenTotal             atomic.Uint64
	driverDeliveryQueueRejectedTotal       atomic.Uint64
	driverDeliveryLateTotal                atomic.Uint64
	driverDeliveryClosedTotal              atomic.Uint64
	driverDeliveryWriteErrorsTotal         atomic.Uint64
	driverDeliveryCollapsedTotal           atomic.Uint64
	driverDeliveryCollapsedCriticalTotal   atomic.Uint64
	driverDeliveryCollapsedImportantTotal  atomic.Uint64
	driverDeliveryCollapsedStateTotal      atomic.Uint64
	driverDeliveryCollapsedEphemeralTotal  atomic.Uint64
	driverDeliveryExpiredTotal             atomic.Uint64
	driverDeliveryAbandonedTotal           atomic.Uint64
	driverDeliveryAdmissionRejectedTotal   atomic.Uint64
	driverDeliveryQueued                   atomic.Uint64
	driverDeliveryInflight                 atomic.Uint64
	driverDeliveryAdmissionFrozen          atomic.Uint64
	driverDeliveryAdmissionActive          atomic.Uint64
	driverDeliveryActiveWriters            atomic.Uint64
	websocketWritePumpsActive              atomic.Uint64
	inboundMessagesTotal                   atomic.Uint64
	websocketPingsSentTotal                atomic.Uint64
	websocketPongsReceivedTotal            atomic.Uint64
	websocketInboundDeadlineRefreshesTotal atomic.Uint64
	websocketReadTimeoutsTotal             atomic.Uint64
	websocketPeerClosesTotal               atomic.Uint64
	websocketReadErrorsTotal               atomic.Uint64
	websocketReadDeadlineErrorsTotal       atomic.Uint64
	outboundMessagesTotal                  atomic.Uint64
	outboundDropsTotal                     atomic.Uint64
	outboundCollapsedTotal                 atomic.Uint64
	outboundCriticalCollapsedTotal         atomic.Uint64
	outboundImportantCollapsedTotal        atomic.Uint64
	outboundStateCollapsedTotal            atomic.Uint64
	outboundEphemeralCollapsedTotal        atomic.Uint64
	outboundCriticalDropsTotal             atomic.Uint64
	outboundImportantDropsTotal            atomic.Uint64
	outboundStateDropsTotal                atomic.Uint64
	outboundEphemeralDropsTotal            atomic.Uint64
	outboundLateFramesTotal                atomic.Uint64
	outboundClosedTotal                    atomic.Uint64
	outboundQueueFullTotal                 atomic.Uint64
	outboundQueuePeak                      atomic.Uint64
	outboundQueueCriticalPeak              atomic.Uint64
	outboundQueueHighPeak                  atomic.Uint64
	outboundQueueNormalPeak                atomic.Uint64
	outboundQueueLowPeak                   atomic.Uint64
	outboundErrorsTotal                    atomic.Uint64
	outboundQueueDwellUsTotal              atomic.Uint64
	outboundQueueDwellSamples              atomic.Uint64
	outboundQueueDwellMaxUs                atomic.Uint64
	outboundWriteLatencyMsTotal            atomic.Uint64
	outboundWriteLatencySamples            atomic.Uint64
	driverWriteWait                        [4]driverTimingMetric
	driverWriteDuration                    [4]driverTimingMetric
}

type driverTimingMetric struct {
	samples atomic.Uint64
	totalUs atomic.Uint64
	maxUs   atomic.Uint64
}

var gatewayMetrics gatewayMetricSet

func writeGatewayBuildMetrics(w io.Writer) {
	identity := currentGatewayBuildIdentity()
	fmt.Fprintln(w, "# HELP xkx_gateway_build_info Running Gateway build identity.")
	fmt.Fprintln(w, "# TYPE xkx_gateway_build_info gauge")
	fmt.Fprintf(
		w,
		"xkx_gateway_build_info{generation=%s,protocol_version=%s,source_commit=%s} 1\n",
		strconv.Quote(identity.Generation),
		strconv.Quote(identity.ProtocolVersion),
		strconv.Quote(identity.SourceCommit),
	)
}

func recordDriverAdmissionWaiting() {
	gatewayMetrics.driverFramesAdmissionWaiting.Add(1)
}

func recordDriverFrameAdmitted() {
	// Publish terminal-conservation ownership before releasing the request to
	// the writer, then release the admission waiter last.
	gatewayMetrics.driverFramesAdmittedTotal.Add(1)
	peak := gatewayMetrics.driverFramesQueued.Add(1)
	for {
		current := gatewayMetrics.driverFramesQueuePeak.Load()
		if peak <= current || gatewayMetrics.driverFramesQueuePeak.CompareAndSwap(current, peak) {
			break
		}
	}
	gatewayMetrics.driverFramesAdmissionWaiting.Add(^uint64(0))
}

func recordDriverAdmissionRejected() {
	gatewayMetrics.driverFramesQueueRejectedTotal.Add(1)
	gatewayMetrics.driverFramesAdmissionWaiting.Add(^uint64(0))
}

func recordDriverFrameDequeued() {
	gatewayMetrics.driverFramesInflight.Add(1)
	gatewayMetrics.driverFramesQueued.Add(^uint64(0))
}

func recordDriverFrameRetained(depth int) {
	gatewayMetrics.driverFramesInflight.Add(^uint64(0))
	gatewayMetrics.driverFramesUnacked.Add(1)
	value := uint64(depth)
	for {
		current := gatewayMetrics.driverFramesUnackedPeak.Load()
		if value <= current || gatewayMetrics.driverFramesUnackedPeak.CompareAndSwap(current, value) {
			break
		}
	}
}

func recordDriverPhysicalWrite(replay bool) {
	gatewayMetrics.driverFramesPhysicalWritesTotal.Add(1)
	if replay {
		gatewayMetrics.driverFramesReplayedTotal.Add(1)
	}
}

func recordDriverFrameAcknowledged() {
	gatewayMetrics.driverFramesWrittenTotal.Add(1)
	gatewayMetrics.driverFramesUnacked.Add(^uint64(0))
}

func recordDriverIngressAckFrame() {
	gatewayMetrics.driverIngressAckFramesTotal.Add(1)
}

func recordDriverIngressAckRejected() {
	gatewayMetrics.driverIngressAckRejectedTotal.Add(1)
}

func recordDriverPartialWriteUnknownRetained() {
	gatewayMetrics.driverFramesWriteUnknownTotal.Add(1)
}

func recordDriverRetainedAbandoned() {
	gatewayMetrics.driverFramesAbandonedTotal.Add(1)
	gatewayMetrics.driverFramesUnacked.Add(^uint64(0))
}

func recordDriverFrameWritten() {
	gatewayMetrics.driverFramesWrittenTotal.Add(1)
	gatewayMetrics.driverFramesInflight.Add(^uint64(0))
}

func recordDriverPartialWriteUnknown() {
	gatewayMetrics.driverFramesWriteUnknownTotal.Add(1)
	gatewayMetrics.driverFramesInflight.Add(^uint64(0))
}

func recordDriverSafeRetry() {
	gatewayMetrics.driverFramesSafeRetriesTotal.Add(1)
}

func recordDriverInflightAbandoned() {
	gatewayMetrics.driverFramesAbandonedTotal.Add(1)
	gatewayMetrics.driverFramesInflight.Add(^uint64(0))
}

func recordDriverQueuedAbandoned() {
	gatewayMetrics.driverFramesAbandonedTotal.Add(1)
	gatewayMetrics.driverFramesQueued.Add(^uint64(0))
}

func recordDriverDecodeSubmitted() {
	// Publish ownership before the cumulative counter so terminal snapshots can
	// never observe a newly submitted job as already drained.
	gatewayMetrics.driverDecodeInflight.Add(1)
	gatewayMetrics.driverDecodeSubmittedTotal.Add(1)
}

func recordDriverDecodeCommitted() {
	// Publish the terminal counter before releasing ownership for the same reason.
	gatewayMetrics.driverDecodeCommittedTotal.Add(1)
	gatewayMetrics.driverDecodeInflight.Add(^uint64(0))
}

func recordDriverDeliveryEnqueued() {
	gatewayMetrics.driverDeliveryEnqueuedTotal.Add(1)
	gatewayMetrics.driverDeliveryQueued.Add(1)
}

func recordDriverDeliveryDequeued() {
	gatewayMetrics.driverDeliveryQueued.Add(^uint64(0))
	gatewayMetrics.driverDeliveryInflight.Add(1)
}

func recordDriverDeliveryInflightTerminal(counter *atomic.Uint64) {
	gatewayMetrics.driverDeliveryInflight.Add(^uint64(0))
	counter.Add(1)
}

func recordDriverDeliveryQueuedTerminal(counter *atomic.Uint64) {
	gatewayMetrics.driverDeliveryQueued.Add(^uint64(0))
	counter.Add(1)
}

func recordDriverDeliveryQueuedCollapse(reliability string) {
	gatewayMetrics.driverDeliveryQueued.Add(^uint64(0))
	gatewayMetrics.driverDeliveryCollapsedTotal.Add(1)
	switch reliability {
	case "critical":
		gatewayMetrics.driverDeliveryCollapsedCriticalTotal.Add(1)
	case "state":
		gatewayMetrics.driverDeliveryCollapsedStateTotal.Add(1)
	case "ephemeral":
		gatewayMetrics.driverDeliveryCollapsedEphemeralTotal.Add(1)
	default:
		gatewayMetrics.driverDeliveryCollapsedImportantTotal.Add(1)
	}
}

func recordDriverDeliveryWriterStarted() {
	gatewayMetrics.driverDeliveryActiveWriters.Add(1)
}

func recordDriverDeliveryWriterFinished() {
	gatewayMetrics.driverDeliveryActiveWriters.Add(^uint64(0))
}

type driverRuntimeStatusSet struct {
	mu          sync.RWMutex
	values      map[string]float64
	updatedUnix int64
	sourceTS    int64
	generation  uint64
}

var driverRuntimeStatus driverRuntimeStatusSet

func updateDriverRuntimeStatus(data any, sourceTS ...int64) {
	values, ok := data.(map[string]any)
	if !ok {
		return
	}

	numeric := make(map[string]float64, len(values))
	for key, value := range values {
		if key == "" {
			continue
		}
		switch typed := value.(type) {
		case float64:
			if !math.IsNaN(typed) && !math.IsInf(typed, 0) {
				numeric[key] = typed
			}
		case float32:
			asFloat := float64(typed)
			if !math.IsNaN(asFloat) && !math.IsInf(asFloat, 0) {
				numeric[key] = asFloat
			}
		case int:
			numeric[key] = float64(typed)
		case int64:
			numeric[key] = float64(typed)
		case uint64:
			numeric[key] = float64(typed)
		}
	}

	incomingSourceTS := int64(0)
	if len(sourceTS) > 0 {
		incomingSourceTS = sourceTS[0]
	}

	driverRuntimeStatus.mu.Lock()
	// Ping and explicit status replies share this store and may arrive out of order.
	if incomingSourceTS > 0 && driverRuntimeStatus.sourceTS > incomingSourceTS {
		driverRuntimeStatus.mu.Unlock()
		return
	}
	driverRuntimeStatus.values = numeric
	driverRuntimeStatus.updatedUnix = time.Now().Unix()
	driverRuntimeStatus.generation++
	driverRuntimeStatus.sourceTS = incomingSourceTS
	driverRuntimeStatus.mu.Unlock()
}

func driverRuntimeStatusSnapshot() (map[string]float64, int64) {
	driverRuntimeStatus.mu.RLock()
	defer driverRuntimeStatus.mu.RUnlock()

	copied := make(map[string]float64, len(driverRuntimeStatus.values))
	for key, value := range driverRuntimeStatus.values {
		copied[key] = value
	}
	copied["runtime_status_source_ts"] = float64(driverRuntimeStatus.sourceTS)
	copied["runtime_status_generation"] = float64(driverRuntimeStatus.generation)
	return copied, driverRuntimeStatus.updatedUnix
}

func driverRuntimeStatusGeneration() uint64 {
	driverRuntimeStatus.mu.RLock()
	defer driverRuntimeStatus.mu.RUnlock()
	return driverRuntimeStatus.generation
}

func resetDriverRuntimeStatusForTest() {
	driverRuntimeStatus.mu.Lock()
	driverRuntimeStatus.values = nil
	driverRuntimeStatus.updatedUnix = 0
	driverRuntimeStatus.sourceTS = 0
	driverRuntimeStatus.generation = 0
	driverRuntimeStatus.mu.Unlock()
}

func recordDriverFrameIn(frame driverGatewayFrame) {
	recordDriverFrameTypeIn(frame.Type)
}

func recordDriverFrameTypeIn(frameType string) {
	gatewayMetrics.driverFramesInTotal.Add(1)
	switch frameType {
	case "output":
		gatewayMetrics.driverOutputFramesInTotal.Add(1)
	case "sys":
		gatewayMetrics.driverSysFramesInTotal.Add(1)
	case "ingress_ack":
		gatewayMetrics.driverIngressAckFramesInTotal.Add(1)
	default:
		gatewayMetrics.driverNonOutputFramesInTotal.Add(1)
	}
}

func recordDriverFrameOut(frameType string) {
	gatewayMetrics.driverFramesOutTotal.Add(1)
	switch frameType {
	case "data":
		gatewayMetrics.driverDataFramesOutTotal.Add(1)
	case "login":
		gatewayMetrics.driverLoginFramesOutTotal.Add(1)
	case "discon":
		gatewayMetrics.driverDisconFramesOutTotal.Add(1)
	case "sys":
		gatewayMetrics.driverSysFramesOutTotal.Add(1)
	}
}

func driverFrameClass(frameType string) (int, string) {
	switch frameType {
	case "data":
		return 0, "data"
	case "login":
		return 1, "login"
	case "discon":
		return 2, "discon"
	default:
		return 3, "other"
	}
}

func recordDriverTiming(metric *driverTimingMetric, elapsed time.Duration) {
	if elapsed < 0 {
		return
	}
	elapsedUs := uint64(elapsed / time.Microsecond)
	metric.samples.Add(1)
	metric.totalUs.Add(elapsedUs)
	for {
		current := metric.maxUs.Load()
		if elapsedUs <= current || metric.maxUs.CompareAndSwap(current, elapsedUs) {
			return
		}
	}
}

func recordDriverWriteTiming(frameType string, wait time.Duration, write time.Duration) {
	index, _ := driverFrameClass(frameType)
	recordDriverTiming(&gatewayMetrics.driverWriteWait[index], wait)
	recordDriverTiming(&gatewayMetrics.driverWriteDuration[index], write)
}
