package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"strconv"
	"sync"
	"time"
	"unicode/utf16"
	"unicode/utf8"
)

const (
	driverFrameMaxPooledBytes        = 256 * 1024
	driverGatewayDecodeWorkerLimit   = 16
	driverGatewayDecodeInFlightLimit = 32
	driverGatewayBufferedBytesLimit  = 64 * 1024 * 1024
)

var (
	driverFrameBufferSizes = [...]int{
		4 * 1024,
		8 * 1024,
		16 * 1024,
		32 * 1024,
		64 * 1024,
		128 * 1024,
		driverFrameMaxPooledBytes,
	}
	driverFrameBufferPools [len(driverFrameBufferSizes)]sync.Pool
)

// driverFrameBuffer keeps the framed driver allocation alive until its
// decoded output payload reaches a WebSocket terminal path. The large backing
// slice is pooled; the small lease object is deliberately not reused so a
// defensive duplicate release can never return another frame's live buffer.
type driverFrameBuffer struct {
	mu          sync.Mutex
	storage     []byte
	poolIndex   int
	poolValue   *[]byte
	releaseHook func()
	released    bool
}

func acquireDriverFrameBuffer(size int) *driverFrameBuffer {
	lease := &driverFrameBuffer{poolIndex: -1}
	if size <= 0 {
		return lease
	}
	for index, bucketSize := range driverFrameBufferSizes {
		if size > bucketSize {
			continue
		}
		lease.poolIndex = index
		pooled, _ := driverFrameBufferPools[index].Get().(*[]byte)
		if pooled == nil {
			storage := make([]byte, bucketSize)
			pooled = &storage
		}
		if cap(*pooled) != bucketSize {
			storage := make([]byte, bucketSize)
			pooled = &storage
		}
		lease.poolValue = pooled
		lease.storage = (*pooled)[:size]
		return lease
	}
	lease.storage = make([]byte, size)
	return lease
}

func (b *driverFrameBuffer) bytes() []byte {
	if b == nil {
		return nil
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.released {
		return nil
	}
	return b.storage
}

func (b *driverFrameBuffer) isReleased() bool {
	if b == nil {
		return true
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.released
}

func (b *driverFrameBuffer) release() {
	if b == nil {
		return
	}
	b.mu.Lock()
	if b.released {
		b.mu.Unlock()
		return
	}
	b.released = true
	b.storage = nil
	poolIndex := b.poolIndex
	poolValue := b.poolValue
	hook := b.releaseHook
	b.poolValue = nil
	b.poolIndex = -1
	b.releaseHook = nil
	b.mu.Unlock()
	if poolIndex >= 0 && poolIndex < len(driverFrameBufferPools) && poolValue != nil {
		*poolValue = (*poolValue)[:0]
		driverFrameBufferPools[poolIndex].Put(poolValue)
	}
	if hook != nil {
		hook()
	}
}

type driverGatewayInboundFrame struct {
	Type              string
	CID               string
	Action            string
	TS                int64
	IngressID         string
	IngressSeq        uint64
	ingressSeqPresent bool
	Data              json.RawMessage
	outputData        []byte
	dataIsString      bool
}

type driverGatewayDecodeJob struct {
	ordinal uint64
	buffer  *driverFrameBuffer
}

type driverGatewayDecodeResult struct {
	ordinal      uint64
	buffer       *driverFrameBuffer
	frame        driverGatewayInboundFrame
	message      []byte
	outerDecoded bool
	decodeErr    error
}

type driverGatewayDecodePipelineConfig struct {
	workers          int
	maxInFlight      int
	maxBufferedBytes int
	decode           func(driverGatewayDecodeJob) driverGatewayDecodeResult
	acquire          func(int) *driverFrameBuffer
}

type driverGatewayBufferBudget struct {
	mu       sync.Mutex
	changed  *sync.Cond
	limit    int
	reserved int
}

func newDriverGatewayBufferBudget(limit int) *driverGatewayBufferBudget {
	budget := &driverGatewayBufferBudget{limit: limit}
	budget.changed = sync.NewCond(&budget.mu)
	return budget
}

func (b *driverGatewayBufferBudget) acquire(size int) func() {
	b.mu.Lock()
	if size <= 0 || size > b.limit {
		b.mu.Unlock()
		return nil
	}
	for b.reserved+size > b.limit {
		b.changed.Wait()
	}
	b.reserved += size
	b.mu.Unlock()
	var once sync.Once
	return func() {
		once.Do(func() {
			b.mu.Lock()
			b.reserved -= size
			b.changed.Broadcast()
			b.mu.Unlock()
		})
	}
}

type driverGatewayReadTerminal struct {
	submitted uint64
	err       error
}

func decodeDriverOutputData(raw json.RawMessage) ([]byte, error) {
	raw = bytes.TrimSpace(raw)
	if len(raw) == 0 {
		return nil, nil
	}
	if raw[0] == '"' {
		return decodeJSONStringBytes(raw)
	}

	var value any
	if err := json.Unmarshal(raw, &value); err != nil {
		return nil, err
	}
	return json.Marshal(value)
}

func decodeJSONStringBytes(raw []byte) ([]byte, error) {
	decoded, end, err := decodeJSONStringBytesAt(raw, 0)
	if err != nil {
		return nil, err
	}
	if skipJSONSpace(raw, end) != len(raw) {
		return nil, fmt.Errorf("driver output data is not a JSON string")
	}
	return decoded, nil
}

func decodeJSONStringBytesAt(raw []byte, start int) ([]byte, int, error) {
	if start < 0 || start >= len(raw) || raw[start] != '"' {
		return nil, start, fmt.Errorf("driver output data is not a JSON string")
	}
	return decodeJSONStringBytesAtInto(raw, start, make([]byte, 0, len(raw)-start-1))
}

func decodeJSONStringBytesAtInPlace(raw []byte, start int) ([]byte, int, error) {
	if start < 0 || start >= len(raw) || raw[start] != '"' {
		return nil, start, fmt.Errorf("driver output data is not a JSON string")
	}
	write := start
	for read := start + 1; read < len(raw); read++ {
		value := raw[read]
		if value == '"' {
			decoded := raw[start:write]
			if !utf8.Valid(decoded) {
				return nil, start, fmt.Errorf("driver output data is not valid UTF-8")
			}
			return decoded, read + 1, nil
		}
		if value < 0x20 {
			return nil, start, fmt.Errorf("invalid unescaped byte in driver output data")
		}
		if value != '\\' {
			raw[write] = value
			write++
			continue
		}
		read++
		if read >= len(raw) {
			return nil, start, fmt.Errorf("truncated escape in driver output data")
		}
		switch raw[read] {
		case '"', '\\', '/':
			raw[write] = raw[read]
			write++
		case 'b':
			raw[write] = '\b'
			write++
		case 'f':
			raw[write] = '\f'
			write++
		case 'n':
			raw[write] = '\n'
			write++
		case 'r':
			raw[write] = '\r'
			write++
		case 't':
			raw[write] = '\t'
			write++
		case 'u':
			codeUnit, next, ok := decodeJSONHexQuad(raw, read+1)
			if !ok {
				return nil, start, fmt.Errorf("invalid unicode escape in driver output data")
			}
			read = next - 1
			r := rune(codeUnit)
			if utf16.IsSurrogate(r) {
				if r < 0xD800 || r > 0xDBFF || read+6 >= len(raw) || raw[read+1] != '\\' || raw[read+2] != 'u' {
					return nil, start, fmt.Errorf("unpaired unicode surrogate in driver output data")
				}
				low, lowNext, lowOK := decodeJSONHexQuad(raw, read+3)
				if !lowOK || low < 0xDC00 || low > 0xDFFF {
					return nil, start, fmt.Errorf("invalid unicode surrogate pair in driver output data")
				}
				r = utf16.DecodeRune(r, rune(low))
				read = lowNext - 1
			}
			write += utf8.EncodeRune(raw[write:], r)
		default:
			return nil, start, fmt.Errorf("invalid escape in driver output data")
		}
	}
	return nil, start, fmt.Errorf("unterminated JSON string in driver output data")
}

func decodeJSONStringBytesAtInto(raw []byte, start int, decoded []byte) ([]byte, int, error) {
	plainStart := start + 1
	for i := start + 1; i < len(raw); i++ {
		value := raw[i]
		if value == '"' {
			decoded = append(decoded, raw[plainStart:i]...)
			if !utf8.Valid(decoded) {
				return nil, start, fmt.Errorf("driver output data is not valid UTF-8")
			}
			return decoded, i + 1, nil
		}
		if value < 0x20 {
			return nil, start, fmt.Errorf("invalid unescaped byte in driver output data")
		}
		if value != '\\' {
			continue
		}
		decoded = append(decoded, raw[plainStart:i]...)
		i++
		if i >= len(raw) {
			return nil, start, fmt.Errorf("truncated escape in driver output data")
		}
		switch raw[i] {
		case '"', '\\', '/':
			decoded = append(decoded, raw[i])
		case 'b':
			decoded = append(decoded, '\b')
		case 'f':
			decoded = append(decoded, '\f')
		case 'n':
			decoded = append(decoded, '\n')
		case 'r':
			decoded = append(decoded, '\r')
		case 't':
			decoded = append(decoded, '\t')
		case 'u':
			codeUnit, next, ok := decodeJSONHexQuad(raw, i+1)
			if !ok {
				return nil, start, fmt.Errorf("invalid unicode escape in driver output data")
			}
			i = next - 1
			r := rune(codeUnit)
			if utf16.IsSurrogate(r) {
				if r < 0xD800 || r > 0xDBFF || i+6 >= len(raw) || raw[i+1] != '\\' || raw[i+2] != 'u' {
					return nil, start, fmt.Errorf("unpaired unicode surrogate in driver output data")
				}
				low, lowNext, lowOK := decodeJSONHexQuad(raw, i+3)
				if !lowOK || low < 0xDC00 || low > 0xDFFF {
					return nil, start, fmt.Errorf("invalid unicode surrogate pair in driver output data")
				}
				r = utf16.DecodeRune(r, rune(low))
				i = lowNext - 1
			}
			decoded = utf8.AppendRune(decoded, r)
		default:
			return nil, start, fmt.Errorf("invalid escape in driver output data")
		}
		plainStart = i + 1
	}
	return nil, start, fmt.Errorf("unterminated JSON string in driver output data")
}

func decodeJSONHexQuad(raw []byte, start int) (uint16, int, bool) {
	if start < 0 || start+4 > len(raw) {
		return 0, start, false
	}
	var value uint16
	for i := start; i < start+4; i++ {
		value <<= 4
		switch digit := raw[i]; {
		case digit >= '0' && digit <= '9':
			value |= uint16(digit - '0')
		case digit >= 'a' && digit <= 'f':
			value |= uint16(digit-'a') + 10
		case digit >= 'A' && digit <= 'F':
			value |= uint16(digit-'A') + 10
		default:
			return 0, start, false
		}
	}
	return value, start + 4, true
}

func decodeDriverGatewayInboundFrame(payload []byte) (driverGatewayInboundFrame, error) {
	var frame driverGatewayInboundFrame
	afterComma := false
	i := skipJSONSpace(payload, 0)
	if i >= len(payload) || payload[i] != '{' {
		return frame, fmt.Errorf("driver frame is not a JSON object")
	}
	i++
	for {
		i = skipJSONSpace(payload, i)
		if i >= len(payload) {
			return frame, fmt.Errorf("truncated driver frame object")
		}
		if payload[i] == '}' {
			if afterComma {
				return frame, fmt.Errorf("trailing comma in driver frame object")
			}
			i = skipJSONSpace(payload, i+1)
			if i != len(payload) {
				return frame, fmt.Errorf("trailing data after driver frame object")
			}
			return frame, nil
		}
		keyStart := i
		afterComma = false
		keyEnd := skipJSONString(payload, keyStart)
		if keyEnd <= keyStart {
			return frame, fmt.Errorf("invalid driver frame key")
		}
		keyBytes, err := decodeJSONStringBytes(payload[keyStart:keyEnd])
		if err != nil {
			return frame, fmt.Errorf("decode driver frame key: %w", err)
		}
		key := string(keyBytes)
		i = skipJSONSpace(payload, keyEnd)
		if i >= len(payload) || payload[i] != ':' {
			return frame, fmt.Errorf("driver frame key %q has no value", key)
		}
		valueStart := skipJSONSpace(payload, i+1)
		valueEnd := -1
		if key == "data" {
			frame.Data = nil
			frame.outputData = nil
			frame.dataIsString = false
		}
		if key == "data" && valueStart < len(payload) && payload[valueStart] == '"' {
			decoded, end, decodeErr := decodeJSONStringBytesAtInPlace(payload, valueStart)
			if decodeErr != nil {
				return frame, fmt.Errorf("decode driver frame data: %w", decodeErr)
			}
			valueEnd = end
			frame.Data = nil
			frame.outputData = decoded
			frame.dataIsString = true
		} else {
			valueEnd = skipJSONValue(payload, valueStart)
			if valueEnd <= valueStart || !json.Valid(payload[valueStart:valueEnd]) {
				return frame, fmt.Errorf("invalid driver frame value for %q", key)
			}
		}

		switch key {
		case "type", "cid", "action", "ingress_id":
			decoded, decodeErr := decodeJSONStringBytes(payload[valueStart:valueEnd])
			if decodeErr != nil {
				return frame, fmt.Errorf("decode driver frame %s: %w", key, decodeErr)
			}
			value := string(decoded)
			switch key {
			case "type":
				frame.Type = value
			case "cid":
				frame.CID = value
			case "action":
				frame.Action = value
			case "ingress_id":
				frame.IngressID = value
			}
		case "ts":
			if err := json.Unmarshal(payload[valueStart:valueEnd], &frame.TS); err != nil {
				return frame, fmt.Errorf("decode driver frame ts: %w", err)
			}
		case "ingress_seq":
			if err := json.Unmarshal(payload[valueStart:valueEnd], &frame.IngressSeq); err != nil {
				return frame, fmt.Errorf("decode driver frame ingress_seq: %w", err)
			}
			frame.ingressSeqPresent = true
		case "data":
			if !frame.dataIsString {
				frame.Data = payload[valueStart:valueEnd]
				frame.outputData = nil
			}
		default:
			// Unknown members remain accepted only after their JSON value is validated.
		}

		i = skipJSONSpace(payload, valueEnd)
		if i >= len(payload) {
			return frame, fmt.Errorf("truncated driver frame object")
		}
		switch payload[i] {
		case ',':
			i++
			afterComma = true
		case '}':
			i = skipJSONSpace(payload, i+1)
			if i != len(payload) {
				return frame, fmt.Errorf("trailing data after driver frame object")
			}
			return frame, nil
		default:
			return frame, fmt.Errorf("invalid driver frame object separator")
		}
	}
}

func decodeDriverGatewayJob(job driverGatewayDecodeJob) driverGatewayDecodeResult {
	result := driverGatewayDecodeResult{ordinal: job.ordinal, buffer: job.buffer}
	frame, err := decodeDriverGatewayInboundFrame(job.buffer.bytes())
	if err != nil {
		result.decodeErr = err
		return result
	}
	result.frame = frame
	result.outerDecoded = true
	if frame.Type == "ingress_ack" {
		if frame.IngressID == "" {
			result.decodeErr = fmt.Errorf("driver ingress ACK is missing ingress_id")
		} else if !frame.ingressSeqPresent {
			result.decodeErr = fmt.Errorf("driver ingress ACK is missing ingress_seq")
		}
		return result
	}
	if frame.Type != "output" {
		return result
	}
	if frame.CID == "" {
		result.decodeErr = fmt.Errorf("driver gateway output envelope is missing cid")
		return result
	}
	message := frame.outputData
	if !frame.dataIsString {
		message, err = decodeDriverOutputData(frame.Data)
		if err != nil {
			result.decodeErr = fmt.Errorf("driver gateway output data decode error: %w", err)
			return result
		}
	}
	if len(message) == 0 {
		result.decodeErr = fmt.Errorf("driver gateway output envelope has empty data")
		return result
	}
	result.message = message
	return result
}

func commitDriverGatewayDecodeResult(
	conn net.Conn,
	hub *driverHub,
	sessions *sessionRegistry,
	controller *driverDeliveryController,
	result driverGatewayDecodeResult,
) {
	if result.outerDecoded {
		hub.markInboundActivity(conn)
		recordDriverFrameTypeIn(result.frame.Type)
	}
	if result.decodeErr != nil {
		if result.outerDecoded && result.frame.Type == "output" && result.frame.CID != "" && controller != nil {
			if controller.terminateMalformedDriverOutput(result.frame.CID) {
				log.Printf("driver gateway malformed output terminated session %s: %v", result.frame.CID, result.decodeErr)
			}
		}
		result.buffer.release()
		gatewayMetrics.driverDecodeErrorsTotal.Add(1)
		log.Printf("driver gateway decode error: %v", result.decodeErr)
		return
	}
	frame := result.frame
	if frame.Type == "ingress_ack" {
		hub.acknowledgeDriverIngress(conn, frame.IngressID, frame.IngressSeq)
		result.buffer.release()
		return
	}
	if frame.Type == "sys" {
		if frame.Action == "pong" || frame.Action == "status" {
			var data any
			if len(frame.Data) > 0 && json.Unmarshal(frame.Data, &data) == nil {
				updateDriverRuntimeStatus(data, frame.TS)
			}
		}
		result.buffer.release()
		return
	}
	if frame.Type != "output" {
		result.buffer.release()
		return
	}
	if sessions == nil || controller == nil {
		result.buffer.release()
		log.Printf("driver gateway output has no session registry or delivery controller")
		return
	}
	if writeResult := controller.enqueueDriverOutput(frame.CID, result.message, result.buffer); !writeResult.ok && writeResult.reason != sessionWriteReasonMissing {
		log.Printf("driver gateway websocket enqueue failed for %s: %s", frame.CID, writeResult.reason)
	}
}

func normalizedDriverGatewayDecodePipelineConfig(config driverGatewayDecodePipelineConfig) driverGatewayDecodePipelineConfig {
	if config.workers <= 0 || config.workers > driverGatewayDecodeWorkerLimit {
		config.workers = driverGatewayDecodeWorkerLimit
	}
	if config.maxInFlight <= 0 || config.maxInFlight > driverGatewayDecodeInFlightLimit {
		config.maxInFlight = driverGatewayDecodeInFlightLimit
	}
	if config.maxInFlight < config.workers {
		config.maxInFlight = config.workers
	}
	if config.maxBufferedBytes <= 0 || config.maxBufferedBytes > driverGatewayBufferedBytesLimit {
		config.maxBufferedBytes = driverGatewayBufferedBytesLimit
	}
	if config.decode == nil {
		config.decode = decodeDriverGatewayJob
	}
	if config.acquire == nil {
		config.acquire = acquireDriverFrameBuffer
	}
	return config
}

func readDriverGatewayDecodeJobs(
	ctx context.Context,
	conn net.Conn,
	config driverGatewayDecodePipelineConfig,
	jobs chan<- driverGatewayDecodeJob,
	inFlight chan struct{},
	bufferBudget *driverGatewayBufferBudget,
	terminal chan<- driverGatewayReadTerminal,
) {
	if ctx == nil {
		ctx = context.Background()
	}
	defer close(jobs)
	var submitted uint64
	finish := func(err error) {
		terminal <- driverGatewayReadTerminal{submitted: submitted, err: err}
	}
	for {
		select {
		case inFlight <- struct{}{}:
		case <-ctx.Done():
			finish(ctx.Err())
			return
		}
		var head [4]byte
		if _, err := ioReadFull(conn, head[:]); err != nil {
			<-inFlight
			finish(err)
			return
		}
		frameLen := (uint32(head[0]) << 24) | (uint32(head[1]) << 16) | (uint32(head[2]) << 8) | uint32(head[3])
		if frameLen == 0 || frameLen > 16*1024*1024 {
			<-inFlight
			finish(fmt.Errorf("invalid driver frame length: %d", frameLen))
			return
		}
		releaseBudget := bufferBudget.acquire(int(frameLen))
		if releaseBudget == nil {
			<-inFlight
			finish(fmt.Errorf("driver frame length exceeds buffered byte budget: frame=%d budget=%d", frameLen, config.maxBufferedBytes))
			return
		}
		payloadBuffer := config.acquire(int(frameLen))
		if payloadBuffer == nil || len(payloadBuffer.bytes()) != int(frameLen) {
			if payloadBuffer != nil {
				payloadBuffer.releaseHook = releaseBudget
				payloadBuffer.release()
			} else {
				releaseBudget()
			}
			<-inFlight
			finish(fmt.Errorf("failed to acquire driver frame buffer: %d", frameLen))
			return
		}
		payloadBuffer.releaseHook = releaseBudget
		if _, err := ioReadFull(conn, payloadBuffer.bytes()); err != nil {
			payloadBuffer.release()
			<-inFlight
			finish(err)
			return
		}
		recordDriverDecodeSubmitted()
		select {
		case jobs <- driverGatewayDecodeJob{ordinal: submitted, buffer: payloadBuffer}:
			submitted++
		case <-ctx.Done():
			payloadBuffer.release()
			<-inFlight
			finish(ctx.Err())
			return
		}
	}
}

func handleDriverGatewayConnWithPipelineContext(
	ctx context.Context,
	conn net.Conn,
	hub *driverHub,
	sessions *sessionRegistry,
	controller *driverDeliveryController,
	config driverGatewayDecodePipelineConfig,
) error {
	if conn == nil || hub == nil || sessions == nil || controller == nil {
		return fmt.Errorf("driver gateway requires connection, hub, session registry, and delivery controller")
	}
	if ctx == nil {
		ctx = context.Background()
	}
	config = normalizedDriverGatewayDecodePipelineConfig(config)
	if controller == nil {
		return fmt.Errorf("driver gateway requires an injected delivery controller")
	}
	connectionWatchDone := make(chan struct{})
	go func() {
		select {
		case <-ctx.Done():
			hub.closeIfCurrent(conn)
		case <-connectionWatchDone:
		}
	}()
	defer func() {
		close(connectionWatchDone)
		hub.clear(conn)
		_ = conn.Close()
		log.Printf("driver gateway disconnected: %s", conn.RemoteAddr())
	}()

	jobs := make(chan driverGatewayDecodeJob, config.maxInFlight)
	results := make(chan driverGatewayDecodeResult, config.maxInFlight)
	inFlight := make(chan struct{}, config.maxInFlight)
	bufferBudget := newDriverGatewayBufferBudget(config.maxBufferedBytes)
	terminal := make(chan driverGatewayReadTerminal, 1)
	go readDriverGatewayDecodeJobs(ctx, conn, config, jobs, inFlight, bufferBudget, terminal)

	var workers sync.WaitGroup
	workers.Add(config.workers)
	for index := 0; index < config.workers; index++ {
		go func() {
			defer workers.Done()
			for job := range jobs {
				results <- config.decode(job)
			}
		}()
	}
	go func() {
		workers.Wait()
		close(results)
	}()

	nextOrdinal := uint64(0)
	pending := make(map[uint64]driverGatewayDecodeResult, config.maxInFlight)
	for result := range results {
		if _, duplicate := pending[result.ordinal]; duplicate || result.ordinal < nextOrdinal {
			result.buffer.release()
			<-inFlight
			recordDriverDecodeCommitted()
			continue
		}
		pending[result.ordinal] = result
		for {
			ready, ok := pending[nextOrdinal]
			if !ok {
				break
			}
			delete(pending, nextOrdinal)
			commitDriverGatewayDecodeResult(conn, hub, sessions, controller, ready)
			<-inFlight
			recordDriverDecodeCommitted()
			nextOrdinal++
		}
	}
	readTerminal := <-terminal
	if len(pending) != 0 || nextOrdinal != readTerminal.submitted || len(inFlight) != 0 {
		pendingCount := len(pending)
		inFlightCount := len(inFlight)
		for _, result := range pending {
			result.buffer.release()
			<-inFlight
			recordDriverDecodeCommitted()
		}
		return fmt.Errorf(
			"driver decode pipeline conservation failed: committed=%d submitted=%d pending=%d in_flight=%d",
			nextOrdinal,
			readTerminal.submitted,
			pendingCount,
			inFlightCount,
		)
	}
	return readTerminal.err
}

func handleDriverGatewayConnWithPipeline(
	conn net.Conn,
	hub *driverHub,
	sessions *sessionRegistry,
	controller *driverDeliveryController,
	config driverGatewayDecodePipelineConfig,
) error {
	return handleDriverGatewayConnWithPipelineContext(
		context.Background(), conn, hub, sessions, controller, config)
}

func handleDriverGatewayConnContext(ctx context.Context, conn net.Conn, hub *driverHub, sessions *sessionRegistry, controller *driverDeliveryController) error {
	return handleDriverGatewayConnWithPipelineContext(
		ctx,
		conn,
		hub,
		sessions,
		controller,
		driverGatewayDecodePipelineConfig{},
	)
}

func handleDriverGatewayConn(conn net.Conn, hub *driverHub, sessions *sessionRegistry, controller *driverDeliveryController) error {
	return handleDriverGatewayConnContext(context.Background(), conn, hub, sessions, controller)
}

func replayDriverSessions(hub *driverHub, sessions *sessionRegistry) error {
	snapshots := sessions.snapshot()
	if len(snapshots) == 0 {
		return nil
	}
	for _, session := range snapshots {
		if err := hub.writePendingDriverFrame(driverGatewayFrame{Type: "login", CID: session.SessionID, Data: map[string]any{
			"ip":         session.RemoteIP,
			"port":       session.RemotePort,
			"session_id": session.SessionID,
		}}); err != nil {
			return err
		}
	}
	log.Printf("driver gateway replayed %d active sessions", len(snapshots))
	return nil
}

func startDriverHeartbeat(ctx context.Context, hub *driverHub, conn net.Conn, interval time.Duration, pongTimeout time.Duration) func() {
	if interval <= 0 {
		return func() {}
	}
	if pongTimeout <= 0 {
		pongTimeout = interval * 3
	}
	stop := make(chan struct{})
	go func() {
		ticker := time.NewTicker(interval)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				hub.closeIfCurrent(conn)
				return
			case <-stop:
				return
			case <-ticker.C:
				if !hub.healthy(pongTimeout) {
					if hub.closeIfCurrent(conn) {
						log.Printf("driver heartbeat inbound timeout after %s", pongTimeout)
					}
					return
				}
				if err := hub.enqueueDriverFrame(ctx, driverGatewayFrame{Type: "sys", Action: "ping", TS: time.Now().UnixMicro()}); err != nil {
					log.Printf("driver heartbeat ping failed: %v", err)
					return
				}
			}
		}
	}()
	return func() {
		close(stop)
	}
}

func watchDriverConnectionContext(ctx context.Context, hub *driverHub, conn net.Conn) func() {
	if ctx == nil {
		ctx = context.Background()
	}
	stopped := make(chan struct{})
	var once sync.Once
	go func() {
		select {
		case <-ctx.Done():
			hub.closeIfCurrent(conn)
		case <-stopped:
		}
	}()
	return func() {
		once.Do(func() { close(stopped) })
	}
}

func runDriverGateway(ctx context.Context, cfg runtimeConfig, hub *driverHub, sessions *sessionRegistry, controller *driverDeliveryController) error {
	for {
		select {
		case <-ctx.Done():
			return nil
		default:
		}

		conn, err := (&net.Dialer{}).DialContext(ctx, "tcp", net.JoinHostPort(cfg.DriverHost, strconv.Itoa(cfg.DriverPort)))
		if err != nil {
			log.Printf("driver gateway dial failed: %v", err)
			select {
			case <-ctx.Done():
				return nil
			case <-time.After(2 * time.Second):
			}
			continue
		}
		enableTCPNoDelay(conn)
		log.Printf("driver gateway connected: %s", conn.RemoteAddr())
		stopConnectionWatch := watchDriverConnectionContext(ctx, hub, conn)
		hub.setPending(conn)
		if err := hub.writePendingDriverFrame(hub.driverHelloFrame()); err != nil {
			stopConnectionWatch()
			_ = conn.Close()
			hub.clear(conn)
			continue
		}
		if err := replayDriverSessions(hub, sessions); err != nil {
			log.Printf("driver gateway replay failed: %v", err)
			stopConnectionWatch()
			_ = conn.Close()
			hub.clear(conn)
			continue
		}
		if !hub.markReady(conn) {
			stopConnectionWatch()
			_ = conn.Close()
			hub.clear(conn)
			continue
		}
		stopHeartbeat := startDriverHeartbeat(
			ctx,
			hub,
			conn,
			time.Duration(cfg.DriverPingInterval)*time.Second,
			time.Duration(cfg.DriverPongTimeout)*time.Second,
		)
		if err := handleDriverGatewayConnContext(ctx, conn, hub, sessions, controller); err != nil {
			stopHeartbeat()
			stopConnectionWatch()
			log.Printf("driver gateway loop error: %v", err)
		} else {
			stopHeartbeat()
			stopConnectionWatch()
		}
	}
}
