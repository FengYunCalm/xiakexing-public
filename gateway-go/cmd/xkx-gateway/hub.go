package main

import (
	"context"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"sync"
	"sync/atomic"
	"time"
)

type driverHub struct {
	mu                      sync.RWMutex
	writeMu                 sync.Mutex
	driverAdmissionMu       sync.RWMutex
	driverQueueAdmissionMu  sync.Mutex
	conn                    net.Conn
	ready                   bool
	lastInboundAt           time.Time
	readyCh                 chan struct{}
	driverQueue             chan driverFrameWriteRequest
	driverPublishNotify     chan struct{}
	driverWriterContextDone <-chan struct{}
	driverWriterDone        chan struct{}
	driverWriterStarted     bool
	driverWriterClosed      bool
	driverAdmissionSequence uint64
	driverIngressSequence   uint64
	driverPublishedSequence atomic.Uint64
	driverIngressPublished  atomic.Uint64
	driverIngressWritten    atomic.Uint64
	driverIngressID         string
	driverUnackedMu         sync.Mutex
	driverIngressWriteMu    sync.Mutex
	driverIngressWriteConn  net.Conn
	driverIngressWriteSeq   uint64
	driverEarlyAckMu        sync.Mutex
	driverEarlyAckConn      net.Conn
	driverEarlyAckSeq       uint64
	driverUnacked           []driverFrameWriteRequest
	driverUnackedHead       int
	driverIngressLastAcked  uint64
	driverAckWindow         chan struct{}
	driverAckNotify         chan struct{}
	driverConnectionNotify  chan struct{}
	driverConnectionGen     uint64
	driverReplayGeneration  atomic.Uint64
	writerOnce              sync.Once
}

const driverWriteTimeout = 2 * time.Second
const defaultDriverSendBuffer = 262144
const defaultDriverAckWindow = 512
const tcpWriteTimeout = 2 * time.Second
const websocketWriteTimeout = 5 * time.Second
const websocketPingInterval = 25 * time.Second
const websocketPongWait = 60 * time.Second
const websocketReadLimit = 1024 * 1024
const websocketSendQueueSize = 64
const httpReadHeaderTimeout = 5 * time.Second
const httpIdleTimeout = 75 * time.Second
const httpWriteTimeout = 10 * time.Second

type driverFrameWriteRequest struct {
	data            []byte
	frameType       string
	sequence        uint64
	ingressSequence uint64
	reliableIngress bool
	writeCount      uint32
}

type driverFrameWriteAttempt struct {
	conn         net.Conn
	bytesWritten int
	frameBytes   int
	err          error
}

type driverFrameWriteState uint8

const (
	driverFrameWritePending driverFrameWriteState = iota
	driverFrameWriteReady
)

func newDriverHub() *driverHub {
	return newDriverHubWithCapacities(defaultDriverSendBuffer, defaultDriverAckWindow)
}

func newDriverHubWithQueueCapacity(capacity int) *driverHub {
	return newDriverHubWithCapacities(capacity, defaultDriverAckWindow)
}

func newDriverHubFromConfig(cfg runtimeConfig) *driverHub {
	return newDriverHubWithCapacities(cfg.DriverSendBuffer, cfg.DriverAckWindow)
}

func newDriverHubWithCapacities(queueCapacity int, ackWindowCapacity int) *driverHub {
	if queueCapacity <= 0 {
		queueCapacity = defaultDriverSendBuffer
	}
	if ackWindowCapacity <= 0 {
		ackWindowCapacity = defaultDriverAckWindow
	}
	return &driverHub{
		readyCh:                make(chan struct{}),
		driverQueue:            make(chan driverFrameWriteRequest, queueCapacity),
		driverPublishNotify:    make(chan struct{}, 1),
		driverWriterDone:       make(chan struct{}),
		driverIngressID:        newDriverIngressID(),
		driverAckWindow:        make(chan struct{}, ackWindowCapacity),
		driverAckNotify:        make(chan struct{}, 1),
		driverConnectionNotify: make(chan struct{}, 1),
	}
}

func (h *driverHub) driverAckWindowCapacity() int {
	if h == nil {
		return 0
	}
	return cap(h.driverAckWindow)
}

func (h *driverHub) driverAckWindowUsed() int {
	if h == nil {
		return 0
	}
	return len(h.driverAckWindow)
}

func newDriverIngressID() string {
	var raw [16]byte
	if _, err := rand.Read(raw[:]); err == nil {
		return fmt.Sprintf("%x", raw[:])
	}
	return fmt.Sprintf("fallback-%x", time.Now().UnixNano())
}

func (h *driverHub) ingressIdentity() string {
	return h.driverIngressID
}

func (h *driverHub) driverHelloFrame() driverGatewayFrame {
	return driverGatewayFrame{
		Type: "hello",
		Data: map[string]any{
			"agent":      "xkx-gateway-go",
			"version":    2,
			"ingress_id": h.driverIngressID,
		},
	}
}

func (h *driverHub) notifyDriverConnectionChange() {
	select {
	case h.driverConnectionNotify <- struct{}{}:
	default:
	}
}

func (h *driverHub) set(conn net.Conn) {
	h.setConnection(conn, true)
}

func (h *driverHub) setPending(conn net.Conn) {
	h.setConnection(conn, false)
}

func (h *driverHub) setConnection(conn net.Conn, ready bool) {
	h.mu.Lock()
	h.clearEarlyDriverAck()
	if h.conn != nil {
		_ = h.conn.Close()
	}
	if h.readyCh != nil {
		select {
		case <-h.readyCh:
		default:
			close(h.readyCh)
		}
	}
	h.conn = conn
	h.ready = ready
	h.driverConnectionGen++
	h.lastInboundAt = time.Now()
	h.readyCh = make(chan struct{})
	if ready {
		close(h.readyCh)
	}
	h.mu.Unlock()
	h.notifyDriverConnectionChange()
}

func (h *driverHub) markReady(conn net.Conn) bool {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.conn != conn {
		return false
	}
	if h.ready {
		return true
	}
	h.ready = true
	close(h.readyCh)
	h.notifyDriverConnectionChange()
	return true
}

func (h *driverHub) clear(conn net.Conn) {
	h.mu.Lock()
	if h.conn == conn {
		h.conn = nil
		h.ready = false
		h.lastInboundAt = time.Time{}
		h.clearEarlyDriverAck()
		select {
		case <-h.readyCh:
		default:
			close(h.readyCh)
		}
		h.readyCh = make(chan struct{})
	}
	h.mu.Unlock()
	h.notifyDriverConnectionChange()
}

func (h *driverHub) closeIfCurrent(conn net.Conn) bool {
	h.mu.Lock()
	if h.conn != conn {
		h.mu.Unlock()
		return false
	}
	h.conn = nil
	h.ready = false
	h.lastInboundAt = time.Time{}
	h.clearEarlyDriverAck()
	select {
	case <-h.readyCh:
	default:
		close(h.readyCh)
	}
	h.readyCh = make(chan struct{})
	h.mu.Unlock()
	_ = conn.Close()
	h.notifyDriverConnectionChange()
	return true
}

func (h *driverHub) writeJSON(v any) error {
	data, err := json.Marshal(v)
	if err != nil {
		return err
	}
	h.writeMu.Lock()
	defer h.writeMu.Unlock()
	h.mu.RLock()
	conn := h.conn
	h.mu.RUnlock()
	if conn == nil {
		return fmt.Errorf("driver not connected")
	}
	_ = conn.SetWriteDeadline(time.Now().Add(driverWriteTimeout))
	_, err = conn.Write(append(data, '\n'))
	_ = conn.SetWriteDeadline(time.Time{})
	return err
}

func encodeDriverFrame(v any) (driverFrameWriteRequest, error) {
	data, err := json.Marshal(v)
	if err != nil {
		return driverFrameWriteRequest{}, err
	}
	frameLen := uint32(len(data))
	frame := make([]byte, 4+len(data))
	frame[0] = byte(frameLen >> 24)
	frame[1] = byte(frameLen >> 16)
	frame[2] = byte(frameLen >> 8)
	frame[3] = byte(frameLen)
	copy(frame[4:], data)
	frameType := "other"
	if driverFrame, ok := v.(driverGatewayFrame); ok {
		frameType = driverFrame.Type
	}
	return driverFrameWriteRequest{data: frame, frameType: frameType}, nil
}

func (h *driverHub) beginDriverIngressWrite(conn net.Conn, sequence uint64) {
	if h == nil || conn == nil || sequence == 0 {
		return
	}
	h.driverIngressWriteMu.Lock()
	h.driverIngressWriteConn = conn
	h.driverIngressWriteSeq = sequence
	h.driverIngressWriteMu.Unlock()
}

func (h *driverHub) finishDriverIngressWrite(conn net.Conn, sequence uint64, complete bool) {
	if h == nil || conn == nil || sequence == 0 {
		return
	}
	h.driverIngressWriteMu.Lock()
	if h.driverIngressWriteConn == conn && h.driverIngressWriteSeq == sequence {
		h.driverIngressWriteConn = nil
		h.driverIngressWriteSeq = 0
	}
	h.driverIngressWriteMu.Unlock()
	if complete {
		for {
			written := h.driverIngressWritten.Load()
			if sequence <= written || h.driverIngressWritten.CompareAndSwap(written, sequence) {
				break
			}
		}
		h.drainEarlyDriverAck(conn)
	}
}

func (h *driverHub) clearEarlyDriverAck() {
	if h == nil {
		return
	}
	h.driverEarlyAckMu.Lock()
	h.driverEarlyAckConn = nil
	h.driverEarlyAckSeq = 0
	h.driverEarlyAckMu.Unlock()
}

func (h *driverHub) rememberEarlyDriverAck(conn net.Conn, sequence uint64) bool {
	if h == nil || conn == nil || sequence == 0 {
		return false
	}
	h.driverEarlyAckMu.Lock()
	defer h.driverEarlyAckMu.Unlock()
	if h.driverEarlyAckConn != nil && h.driverEarlyAckConn != conn {
		return false
	}
	h.driverEarlyAckConn = conn
	if sequence > h.driverEarlyAckSeq {
		h.driverEarlyAckSeq = sequence
	}
	return true
}

func (h *driverHub) drainEarlyDriverAck(conn net.Conn) {
	if h == nil || conn == nil {
		return
	}
	h.driverEarlyAckMu.Lock()
	sequence := uint64(0)
	if h.driverEarlyAckConn == conn &&
		h.driverEarlyAckSeq > 0 &&
		h.driverEarlyAckSeq <= h.driverIngressWritten.Load() {
		sequence = h.driverEarlyAckSeq
		h.driverEarlyAckConn = nil
		h.driverEarlyAckSeq = 0
	}
	h.driverEarlyAckMu.Unlock()
	if sequence > 0 {
		h.acknowledgeDriverIngress(conn, h.driverIngressID, sequence)
	}
}

func (h *driverHub) driverIngressWriteInProgress(conn net.Conn, sequence uint64) bool {
	if h == nil || conn == nil || sequence == 0 {
		return false
	}
	h.driverIngressWriteMu.Lock()
	inProgress := h.driverIngressWriteConn == conn && h.driverIngressWriteSeq == sequence
	h.driverIngressWriteMu.Unlock()
	return inProgress
}

func (h *driverHub) writeEncodedDriverFrame(request driverFrameWriteRequest, state driverFrameWriteState) driverFrameWriteAttempt {
	waitStartedAt := time.Now()
	h.writeMu.Lock()
	writeStartedAt := time.Now()
	gatewayMetrics.driverWritesActive.Add(1)
	defer func() {
		gatewayMetrics.driverWritesActive.Add(^uint64(0))
		h.writeMu.Unlock()
	}()
	h.mu.RLock()
	conn := h.conn
	ready := h.ready
	h.mu.RUnlock()
	attempt := driverFrameWriteAttempt{
		conn:       conn,
		frameBytes: len(request.data),
	}
	stateMatches := (state == driverFrameWritePending && !ready) ||
		(state == driverFrameWriteReady && ready)
	if conn == nil || !stateMatches {
		attempt.err = fmt.Errorf("driver gateway not connected")
		return attempt
	}
	if request.reliableIngress {
		h.beginDriverIngressWrite(conn, request.ingressSequence)
	}
	_ = conn.SetWriteDeadline(time.Now().Add(driverWriteTimeout))
	attempt.bytesWritten, attempt.err = conn.Write(request.data)
	_ = conn.SetWriteDeadline(time.Time{})
	if attempt.err == nil && attempt.bytesWritten != len(request.data) {
		attempt.err = io.ErrShortWrite
	}
	if request.reliableIngress {
		h.finishDriverIngressWrite(conn, request.ingressSequence,
			attempt.err == nil && attempt.bytesWritten == len(request.data))
	}
	recordDriverWriteTiming(request.frameType, writeStartedAt.Sub(waitStartedAt), time.Since(writeStartedAt))
	if attempt.err != nil {
		gatewayMetrics.driverWriteErrorsTotal.Add(1)
	}
	return attempt
}

func (h *driverHub) writePendingDriverFrame(v any) error {
	request, err := encodeDriverFrame(v)
	if err != nil {
		return err
	}
	return h.writeEncodedDriverFrame(request, driverFrameWritePending).err
}

func (h *driverHub) enqueueDriverFrame(ctx context.Context, v any) error {
	if ctx == nil {
		ctx = context.Background()
	}
	recordDriverAdmissionWaiting()
	h.driverAdmissionMu.RLock()
	if !h.driverWriterStarted || h.driverWriterClosed {
		h.driverAdmissionMu.RUnlock()
		recordDriverAdmissionRejected()
		return fmt.Errorf("driver writer not running")
	}
	writerContextDone := h.driverWriterContextDone
	frame, reliableIngress := v.(driverGatewayFrame)
	reliableIngress = reliableIngress && frame.Type == "data"
	if reliableIngress {
		select {
		case h.driverAckWindow <- struct{}{}:
		case <-ctx.Done():
			h.driverAdmissionMu.RUnlock()
			recordDriverAdmissionRejected()
			return ctx.Err()
		case <-writerContextDone:
			h.driverAdmissionMu.RUnlock()
			recordDriverAdmissionRejected()
			return fmt.Errorf("driver writer stopped")
		}
	}
	h.driverQueueAdmissionMu.Lock()
	queueSequence := h.driverAdmissionSequence + 1
	if reliableIngress {
		frame.IngressID = h.driverIngressID
		frame.IngressSeq = h.driverIngressSequence + 1
		v = frame
	}
	request, err := encodeDriverFrame(v)
	if err != nil {
		h.driverQueueAdmissionMu.Unlock()
		h.driverAdmissionMu.RUnlock()
		if reliableIngress {
			<-h.driverAckWindow
		}
		recordDriverAdmissionRejected()
		return err
	}
	request.sequence = queueSequence
	request.reliableIngress = reliableIngress
	if reliableIngress {
		request.ingressSequence = frame.IngressSeq
	}
	select {
	case h.driverQueue <- request:
		recordDriverFrameAdmitted()
		h.driverAdmissionSequence = request.sequence
		if reliableIngress {
			h.driverIngressSequence = request.ingressSequence
			h.driverIngressPublished.Store(request.ingressSequence)
		}
		h.driverPublishedSequence.Store(request.sequence)
		select {
		case h.driverPublishNotify <- struct{}{}:
		default:
		}
		h.driverQueueAdmissionMu.Unlock()
		h.driverAdmissionMu.RUnlock()
		return nil
	case <-ctx.Done():
		h.driverQueueAdmissionMu.Unlock()
		h.driverAdmissionMu.RUnlock()
		recordDriverAdmissionRejected()
		if reliableIngress {
			<-h.driverAckWindow
		}
		return ctx.Err()
	case <-writerContextDone:
		h.driverQueueAdmissionMu.Unlock()
		h.driverAdmissionMu.RUnlock()
		recordDriverAdmissionRejected()
		if reliableIngress {
			<-h.driverAckWindow
		}
		return fmt.Errorf("driver writer stopped")
	}
}

func (h *driverHub) startDriverWriter(ctx context.Context) {
	if ctx == nil {
		ctx = context.Background()
	}
	h.writerOnce.Do(func() {
		h.driverAdmissionMu.Lock()
		h.driverWriterContextDone = ctx.Done()
		h.driverWriterStarted = true
		h.driverAdmissionMu.Unlock()
		go h.runDriverWriter(ctx)
	})
}

func (h *driverHub) readyDriverConnection() (net.Conn, uint64) {
	h.mu.RLock()
	defer h.mu.RUnlock()
	if h.conn == nil || !h.ready {
		return nil, h.driverConnectionGen
	}
	return h.conn, h.driverConnectionGen
}

func (h *driverHub) retainDriverIngress(request driverFrameWriteRequest) {
	h.driverUnackedMu.Lock()
	h.driverUnacked = append(h.driverUnacked, request)
	depth := len(h.driverUnacked) - h.driverUnackedHead
	h.driverUnackedMu.Unlock()
	recordDriverFrameRetained(depth)
}

func (h *driverHub) nextDriverIngressAfter(sequence uint64) (driverFrameWriteRequest, bool) {
	h.driverUnackedMu.Lock()
	defer h.driverUnackedMu.Unlock()
	for _, request := range h.driverUnacked[h.driverUnackedHead:] {
		if request.ingressSequence > sequence {
			return request, true
		}
	}
	return driverFrameWriteRequest{}, false
}

func (h *driverHub) markDriverIngressWritten(sequence uint64) uint32 {
	h.driverUnackedMu.Lock()
	defer h.driverUnackedMu.Unlock()
	for index := h.driverUnackedHead; index < len(h.driverUnacked); index++ {
		if h.driverUnacked[index].ingressSequence != sequence {
			continue
		}
		prior := h.driverUnacked[index].writeCount
		h.driverUnacked[index].writeCount++
		return prior
	}
	return 0
}

func (h *driverHub) unackedDriverFrames() int {
	h.driverUnackedMu.Lock()
	defer h.driverUnackedMu.Unlock()
	return len(h.driverUnacked) - h.driverUnackedHead
}

func (h *driverHub) acknowledgeDriverIngress(conn net.Conn, ingressID string, sequence uint64) bool {
	h.mu.RLock()
	current := h.conn
	ready := h.ready
	if conn == nil || conn != current || !ready || ingressID != h.driverIngressID {
		h.mu.RUnlock()
		recordDriverIngressAckRejected()
		return false
	}
	if sequence > h.driverIngressWritten.Load() {
		published := h.driverIngressPublished.Load()
		if sequence > published ||
			!h.driverIngressWriteInProgress(conn, sequence) ||
			!h.rememberEarlyDriverAck(conn, sequence) {
			h.mu.RUnlock()
			recordDriverIngressAckRejected()
			return false
		}
		h.mu.RUnlock()
		/* Driver ACK arrived after the write attempt began but before the
		 * local writer committed the physical write. Keep it pending; the
		 * write completion path drains it and releases the window exactly
		 * once. */
		return true
	}

	h.driverUnackedMu.Lock()
	if sequence < h.driverIngressLastAcked {
		h.driverUnackedMu.Unlock()
		h.mu.RUnlock()
		recordDriverIngressAckRejected()
		return false
	}
	if sequence == h.driverIngressLastAcked {
		replayOutstanding := len(h.driverUnacked) > h.driverUnackedHead
		if replayOutstanding {
			h.driverReplayGeneration.Add(1)
		}
		h.driverUnackedMu.Unlock()
		h.mu.RUnlock()
		recordDriverIngressAckFrame()
		if replayOutstanding {
			select {
			case h.driverAckNotify <- struct{}{}:
			default:
			}
		}
		return true
	}
	want := sequence - h.driverIngressLastAcked
	available := len(h.driverUnacked) - h.driverUnackedHead
	if uint64(available) < want {
		h.driverUnackedMu.Unlock()
		h.mu.RUnlock()
		recordDriverIngressAckRejected()
		return false
	}
	for index := uint64(0); index < want; index++ {
		if h.driverUnacked[h.driverUnackedHead+int(index)].ingressSequence !=
			h.driverIngressLastAcked+index+1 {
			h.driverUnackedMu.Unlock()
			h.mu.RUnlock()
			recordDriverIngressAckRejected()
			return false
		}
	}
	h.driverUnackedHead += int(want)
	h.driverIngressLastAcked = sequence
	if h.driverUnackedHead >= 1024 && h.driverUnackedHead*2 >= len(h.driverUnacked) {
		copy(h.driverUnacked, h.driverUnacked[h.driverUnackedHead:])
		h.driverUnacked = h.driverUnacked[:len(h.driverUnacked)-h.driverUnackedHead]
		h.driverUnackedHead = 0
	}
	h.driverUnackedMu.Unlock()
	h.mu.RUnlock()

	for index := uint64(0); index < want; index++ {
		<-h.driverAckWindow
		recordDriverFrameOut("data")
		recordDriverFrameAcknowledged()
	}
	gatewayMetrics.driverIngressLastAcked.Store(sequence)
	recordDriverIngressAckFrame()
	select {
	case h.driverAckNotify <- struct{}{}:
	default:
	}
	return true
}

func (h *driverHub) abandonDriverIngress() {
	h.driverUnackedMu.Lock()
	count := len(h.driverUnacked) - h.driverUnackedHead
	h.driverUnacked = nil
	h.driverUnackedHead = 0
	h.driverUnackedMu.Unlock()
	for index := 0; index < count; index++ {
		<-h.driverAckWindow
		recordDriverRetainedAbandoned()
	}
}

func (h *driverHub) runDriverWriter(ctx context.Context) {
	var current *driverFrameWriteRequest
	var activeConnectionGeneration uint64
	var activeReplayGeneration uint64
	var sentThrough uint64
	defer func() {
		h.driverAdmissionMu.Lock()
		h.driverWriterClosed = true
		if current != nil {
			recordDriverInflightAbandoned()
		}
		h.abandonDriverIngress()
		for {
			select {
			case request := <-h.driverQueue:
				if request.reliableIngress {
					<-h.driverAckWindow
				}
				recordDriverQueuedAbandoned()
			default:
				close(h.driverWriterDone)
				h.driverAdmissionMu.Unlock()
				return
			}
		}
	}()

	for {
		if !h.waitConnectedContext(ctx) {
			return
		}
		conn, generation := h.readyDriverConnection()
		if conn == nil {
			continue
		}
		if generation != activeConnectionGeneration {
			activeConnectionGeneration = generation
			sentThrough = 0
		}
		replayGeneration := h.driverReplayGeneration.Load()
		if replayGeneration != activeReplayGeneration {
			activeReplayGeneration = replayGeneration
			sentThrough = 0
		}

		if request, ok := h.nextDriverIngressAfter(sentThrough); ok {
			attempt := h.writeEncodedDriverFrame(request, driverFrameWriteReady)
			if attempt.err == nil && attempt.bytesWritten == attempt.frameBytes {
				recordDriverPhysicalWrite(request.writeCount > 0)
				h.markDriverIngressWritten(request.ingressSequence)
				sentThrough = request.ingressSequence
				continue
			}
			if attempt.conn != nil {
				h.closeIfCurrent(attempt.conn)
			}
			if attempt.bytesWritten == 0 {
				recordDriverSafeRetry()
			} else {
				recordDriverPartialWriteUnknownRetained()
			}
			activeConnectionGeneration = 0
			continue
		}

		if current == nil {
			select {
			case request := <-h.driverQueue:
				for h.driverPublishedSequence.Load() < request.sequence {
					<-h.driverPublishNotify
				}
				recordDriverFrameDequeued()
				if request.reliableIngress {
					h.retainDriverIngress(request)
					continue
				}
				current = &request
			case <-h.driverAckNotify:
				continue
			case <-h.driverConnectionNotify:
				continue
			case <-ctx.Done():
				return
			}
		}

		attempt := h.writeEncodedDriverFrame(*current, driverFrameWriteReady)
		if attempt.err == nil && attempt.bytesWritten == attempt.frameBytes {
			recordDriverPhysicalWrite(false)
			recordDriverFrameOut(current.frameType)
			recordDriverFrameWritten()
			current = nil
			continue
		}

		if attempt.conn != nil {
			h.closeIfCurrent(attempt.conn)
		}
		if attempt.bytesWritten == 0 {
			recordDriverSafeRetry()
			continue
		}

		recordDriverPartialWriteUnknown()
		current = nil
	}
}

func enableTCPNoDelay(conn net.Conn) {
	tcpConn, ok := conn.(*net.TCPConn)
	if !ok || tcpConn == nil {
		return
	}
	_ = tcpConn.SetNoDelay(true)
}

func (h *driverHub) connected() bool {
	h.mu.RLock()
	connected := h.conn != nil && h.ready
	h.mu.RUnlock()
	return connected
}

func (h *driverHub) markInboundActivity(conn net.Conn) {
	h.mu.Lock()
	if h.conn == conn {
		h.lastInboundAt = time.Now()
	}
	h.mu.Unlock()
}

func (h *driverHub) healthy(timeout time.Duration) bool {
	h.mu.RLock()
	conn := h.conn
	ready := h.ready
	lastInboundAt := h.lastInboundAt
	h.mu.RUnlock()
	if conn == nil || !ready {
		return false
	}
	if timeout <= 0 {
		return true
	}
	return !lastInboundAt.IsZero() && time.Since(lastInboundAt) <= timeout
}

func (h *driverHub) waitConnected(timeout time.Duration) bool {
	deadline := time.Now().Add(timeout)
	for {
		if h.connected() {
			return true
		}
		remaining := time.Until(deadline)
		if remaining <= 0 {
			return h.connected()
		}
		h.mu.RLock()
		readyCh := h.readyCh
		h.mu.RUnlock()
		if readyCh == nil {
			return h.connected()
		}
		timer := time.NewTimer(remaining)
		select {
		case <-readyCh:
			if !timer.Stop() {
				select {
				case <-timer.C:
				default:
				}
			}
		case <-timer.C:
			return h.connected()
		}
	}
}

func (h *driverHub) waitConnectedContext(ctx context.Context) bool {
	for {
		h.mu.RLock()
		connected := h.conn != nil && h.ready
		readyCh := h.readyCh
		h.mu.RUnlock()
		if connected {
			return true
		}
		select {
		case <-ctx.Done():
			return false
		case <-readyCh:
		}
	}
}

func ioReadFull(conn net.Conn, buf []byte) (int, error) {
	return io.ReadFull(conn, buf)
}
