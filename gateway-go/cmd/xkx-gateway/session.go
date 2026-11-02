package main

import (
	"bytes"
	cryptorand "crypto/rand"
	"encoding/hex"
	"encoding/json"
	"log"
	"strconv"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gorilla/websocket"
)

// outboundQueueEvent is a neutral lifecycle result emitted by the generic
// queue. ownerToken is opaque; the queue only propagates it and never
// interprets owner-specific state. Owners consume events through a
// single ordered inbox after queue operations release the generic queue lock.
type outboundQueueEvent struct {
	kind        uint8
	reliability string
	ownerToken  string
	sequence    uint64
}

type outboundQueueEventRecord struct {
	event outboundQueueEvent
}

const (
	outboundQueueEventEnqueued uint8 = iota + 1
	outboundQueueEventDequeued
	outboundQueueEventClosed
	outboundQueueEventCollapsed
	outboundQueueEventQueueRejected
	outboundQueueEventAbandoned
	outboundQueueEventExpired
	outboundQueueEventWriterStarted
	outboundQueueEventWriterFinished
	outboundQueueEventWriteError
	outboundQueueEventWritten
)

type websocketOutboundMessage struct {
	messageType int
	data        []byte
	class       outboundMessageClass
	enqueuedAt  time.Time
	ownerToken  string
}

type outboundMessageClass struct {
	protocolType string
	reliability  string
	priority     string
	collapseKey  string
	ttlMs        int64
}

type outboundClassificationFields struct {
	Reliability string   `json:"reliability"`
	Priority    string   `json:"priority"`
	CollapseKey string   `json:"collapse_key"`
	TTLMS       *float64 `json:"ttl_ms"`
}

type outboundClassificationEnvelope struct {
	Reliability string                       `json:"reliability"`
	Priority    string                       `json:"priority"`
	CollapseKey string                       `json:"collapse_key"`
	TTLMS       *float64                     `json:"ttl_ms"`
	Meta        outboundClassificationFields `json:"meta"`
}

func classifyOutboundMessage(data []byte) outboundMessageClass {
	classified := outboundMessageClass{reliability: "important", priority: "normal"}
	start := 0
	if !bytes.HasPrefix(data, []byte("\x1bXK")) {
		start = bytes.Index(data, []byte("\x1bXK"))
	}
	if start < 0 || len(data) < start+7 {
		return classified
	}
	switch string(data[start+3 : start+7]) {
	case "BACH":
		classified.protocolType = "BACH"
	case "MSGE":
		classified.protocolType = "MSGE"
	default:
		classified.protocolType = string(data[start+3 : start+7])
	}
	payload := data[start+7:]
	if len(data) >= start+9 && data[len(data)-2] == '\x1b' && data[len(data)-1] == '\n' {
		payload = data[start+7 : len(data)-2]
	} else if end := bytes.IndexByte(payload, '\x1b'); end >= 0 {
		payload = payload[:end]
	}
	if classified.protocolType == "BACH" {
		metaJSON, ok := findBACHTransportMeta(payload)
		if !ok {
			return classified
		}
		var meta outboundClassificationFields
		if err := json.Unmarshal(metaJSON, &meta); err == nil {
			applyOutboundClassificationFields(meta, &classified)
		}
		return classified
	}
	var raw outboundClassificationEnvelope
	if err := json.Unmarshal(payload, &raw); err != nil {
		return classified
	}
	if classified.protocolType == "MSGE" {
		applyOutboundClassificationFields(outboundClassificationFields{
			Reliability: raw.Reliability,
			Priority:    raw.Priority,
			CollapseKey: raw.CollapseKey,
			TTLMS:       raw.TTLMS,
		}, &classified)
	}
	applyOutboundClassificationFields(raw.Meta, &classified)
	return classified
}

func applyOutboundClassificationFields(values outboundClassificationFields, classified *outboundMessageClass) {
	if classified == nil {
		return
	}
	classified.reliability = nonBlankString(values.Reliability, classified.reliability)
	classified.priority = nonBlankString(values.Priority, classified.priority)
	classified.collapseKey = nonBlankString(values.CollapseKey, classified.collapseKey)
	if values.TTLMS != nil {
		classified.ttlMs = int64(*values.TTLMS)
	}
}

func findBACHTransportMeta(payload []byte) ([]byte, bool) {
	payload = bytes.TrimSpace(payload)
	canonicalPrefix := []byte(`{"messages":[`)
	canonicalSeparator := []byte(`],"meta":`)
	if bytes.HasPrefix(payload, canonicalPrefix) && len(payload) > len(canonicalPrefix)+len(canonicalSeparator)+2 && payload[len(payload)-1] == '}' {
		separator := bytes.LastIndex(payload, canonicalSeparator)
		if separator >= len(canonicalPrefix) {
			meta := payload[separator+len(canonicalSeparator) : len(payload)-1]
			if len(meta) >= 2 && meta[0] == '{' && meta[len(meta)-1] == '}' && json.Valid(meta) {
				return meta, true
			}
		}
	}
	return findTopLevelJSONMember(payload, "meta")
}

func findTopLevelJSONMember(data []byte, member string) ([]byte, bool) {
	if member == "" || !json.Valid(data) {
		return nil, false
	}
	i := skipJSONSpace(data, 0)
	if i >= len(data) || data[i] != '{' {
		return nil, false
	}
	i++
	var found []byte
	for {
		i = skipJSONSpace(data, i)
		if i >= len(data) {
			return nil, false
		}
		if data[i] == '}' {
			i = skipJSONSpace(data, i+1)
			return found, found != nil && i == len(data)
		}
		if data[i] != '"' {
			return nil, false
		}
		keyStart := i
		keyEnd := skipJSONString(data, keyStart)
		if keyEnd <= keyStart {
			return nil, false
		}
		i = skipJSONSpace(data, keyEnd)
		if i >= len(data) || data[i] != ':' {
			return nil, false
		}
		valueStart := skipJSONSpace(data, i+1)
		valueEnd := skipJSONValue(data, valueStart)
		if valueEnd <= valueStart {
			return nil, false
		}
		if jsonObjectKeyMatches(data[keyStart:keyEnd], member) {
			found = data[valueStart:valueEnd]
		}
		i = skipJSONSpace(data, valueEnd)
		if i >= len(data) {
			return nil, false
		}
		switch data[i] {
		case ',':
			i++
		case '}':
			i = skipJSONSpace(data, i+1)
			return found, found != nil && i == len(data)
		default:
			return nil, false
		}
	}
}

func jsonObjectKeyMatches(encoded []byte, member string) bool {
	if len(encoded) < 2 || encoded[0] != '"' || encoded[len(encoded)-1] != '"' {
		return false
	}
	inner := encoded[1 : len(encoded)-1]
	if bytes.IndexByte(inner, '\\') < 0 {
		return bytes.Equal(inner, []byte(member))
	}
	var decoded string
	return json.Unmarshal(encoded, &decoded) == nil && decoded == member
}

func skipJSONSpace(data []byte, i int) int {
	for i < len(data) {
		switch data[i] {
		case ' ', '\t', '\r', '\n':
			i++
		default:
			return i
		}
	}
	return i
}

func skipJSONString(data []byte, start int) int {
	if start >= len(data) || data[start] != '"' {
		return -1
	}
	escaped := false
	for i := start + 1; i < len(data); i++ {
		if escaped {
			escaped = false
			continue
		}
		switch data[i] {
		case '\\':
			escaped = true
		case '"':
			return i + 1
		}
	}
	return -1
}

func skipJSONValue(data []byte, start int) int {
	if start >= len(data) {
		return -1
	}
	if data[start] == '"' {
		return skipJSONString(data, start)
	}
	if data[start] != '{' && data[start] != '[' {
		i := start
		for i < len(data) {
			switch data[i] {
			case ',', '}', ']', ' ', '\t', '\r', '\n':
				return i
			default:
				i++
			}
		}
		return i
	}

	depth := 1
	for i := start + 1; i < len(data); i++ {
		switch data[i] {
		case '"':
			i = skipJSONString(data, i) - 1
			if i < 0 {
				return -1
			}
		case '{', '[':
			depth++
		case '}', ']':
			depth--
			if depth == 0 {
				return i + 1
			}
		}
	}
	return -1
}

func applyOutboundClassification(values map[string]any, classified *outboundMessageClass) {
	if values == nil || classified == nil {
		return
	}
	classified.reliability = nonBlankString(values["reliability"], classified.reliability)
	classified.priority = nonBlankString(values["priority"], classified.priority)
	classified.collapseKey = nonBlankString(values["collapse_key"], classified.collapseKey)
	if value, ok := values["ttl_ms"].(float64); ok {
		classified.ttlMs = int64(value)
	}
}

func nonBlankString(value any, fallback string) string {
	if text, ok := value.(string); ok && text != "" {
		return text
	}
	return fallback
}

func (c outboundMessageClass) metricsReliability() string {
	switch c.reliability {
	case "critical", "important", "state", "ephemeral":
		return c.reliability
	default:
		return "important"
	}
}

func (c outboundMessageClass) metricsPriority() string {
	switch c.priority {
	case "critical", "high", "normal", "low":
		return c.priority
	default:
		return "normal"
	}
}

func (c outboundMessageClass) canDropWhenFull() bool {
	return c.reliability == "state" || c.reliability == "ephemeral"
}

func (c outboundMessageClass) canCollapse() bool {
	return c.collapseKey != "" && (c.canDropWhenFull() || c.priority == "low")
}

func (c outboundMessageClass) isExpired(enqueuedAt time.Time, now time.Time) bool {
	return c.reliability == "ephemeral" && c.ttlMs > 0 && now.Sub(enqueuedAt) > time.Duration(c.ttlMs)*time.Millisecond
}

type enqueueResult int

const (
	enqueueOK enqueueResult = iota
	enqueueClosed
	enqueueFull
	enqueueDropped
)

type enqueueOutcome struct {
	result enqueueResult
	class  outboundMessageClass
}

type sessionWriteResult struct {
	ok     bool
	reason string
}

const (
	sessionWriteReasonMissing         = "missing_session"
	sessionWriteReasonClosed          = "closed_session"
	sessionWriteReasonQueueFull       = "outbound_queue_overflow"
	sessionWriteReasonAdmissionFrozen = "driver_delivery_admission_frozen"
)

type sessionOwnerLease interface {
	sessionOwnerActive() bool
	sessionOwnerActivate()
	sessionOwnerRevoke()
	sessionOwnerDetached(string)
	sessionOwnerEquals(sessionOwnerLease) bool
}

type sessionOwnerState struct {
	ownerMu      sync.Mutex
	owner        sessionOwnerLease
	ownerRevoked atomic.Bool
}

type sessionQueueState struct {
	eventMu              sync.Mutex
	queueEventProducerMu sync.RWMutex
	queueEventOwner      sessionQueueEventHandle
}

type sessionQueueEventHandle interface {
	dispatch(outboundQueueEvent) func()
	clear()
	sequenceValue() uint64
	captureBarrier(chan struct{}) bool
}

type sessionOwnerEntryPort interface {
	bindSessionOwner(sessionOwnerLease) bool
	registerSessionQueueObserver(
		chan outboundQueueEvent,
		func(outboundQueueEvent),
		func(uint64, chan struct{}) bool,
	) (sessionQueueEventHandle, bool)
	activateSessionOwner()
	isSessionOwnerActive() bool
	clearSessionOwner(sessionOwnerLease) bool
	captureQueueEventBarrier(sessionQueueEventHandle, chan struct{}) (func(), bool)
	enqueueWithOwnerToken(int, []byte, string) enqueueOutcome
	queueStats() (int, int)
	closeWithReason(int, string)
	close()
	pumpStarted() bool
	pumpDone() <-chan struct{}
	abandonQueued()
}

type sessionEntry struct {
	conn *websocket.Conn
	send chan websocketOutboundMessage
	// sendWake is readiness notification only. The bounded send channel remains
	// queue storage, and every production enqueue/filter/dequeue touches it under mu.
	sendWake chan struct{}
	done     chan struct{}
	ws       websocketRuntimeConfig
	mu       sync.Mutex
	sessionOwnerState
	sessionQueueState
	closed          bool
	pumpStartedFlag bool
	closeCode       int
	closeText       string
	closeOnce       sync.Once
	pumpDoneCh      chan struct{}
	remoteIP        string
	remotePort      int
	priorityDepth   map[string]int
}

type sessionSnapshot struct {
	SessionID  string
	RemoteIP   string
	RemotePort int
}

type sessionRegistry struct {
	// registrationMu is the cross-owner commit gate. Generic lookup/removal
	// cannot observe an owner entry between registry publication and binding
	// publication.
	registrationMu   sync.RWMutex
	mu               sync.RWMutex
	sessions         map[string]*sessionEntry
	nextID           atomic.Uint64
	sessionIDPrefix  string
	ws               websocketRuntimeConfig
	writePumpsActive atomic.Uint64
}

func newSessionRegistry() *sessionRegistry {
	return newSessionRegistryWithWebSocketConfig(defaultWebSocketRuntimeConfig())
}

func newSessionRegistryWithWebSocketConfig(ws websocketRuntimeConfig) *sessionRegistry {
	return newSessionRegistryWithPrefix(ws, "gw")
}

// Production session IDs cross the LPC boundary and enter persistent
// idempotency keys, so every Gateway process needs a prefix that cannot be
// reused after a restart. Tests use the deterministic constructor below.
func newProductionSessionRegistryWithWebSocketConfig(ws websocketRuntimeConfig) *sessionRegistry {
	return newSessionRegistryWithPrefix(ws, newGatewaySessionIDPrefix())
}

func newSessionRegistryWithPrefix(ws websocketRuntimeConfig, prefix string) *sessionRegistry {
	if prefix == "" {
		prefix = "gw"
	}
	return &sessionRegistry{
		sessions:        map[string]*sessionEntry{},
		sessionIDPrefix: prefix,
		ws:              ws,
	}
}

var sessionIDPrefixFallback atomic.Uint64

func newGatewaySessionIDPrefix() string {
	var nonce [16]byte
	if _, err := cryptorand.Read(nonce[:]); err == nil {
		return "gw-" + hex.EncodeToString(nonce[:])
	}
	sequence := sessionIDPrefixFallback.Add(1)
	return "gw-" + strconv.FormatInt(time.Now().UnixNano(), 16) + "-" +
		strconv.FormatUint(sequence, 16)
}

func newSessionEntry(conn *websocket.Conn, remoteAddr string, ws websocketRuntimeConfig) *sessionEntry {
	remoteIP, remotePort := splitRemoteAddr(remoteAddr)
	return &sessionEntry{
		conn:          conn,
		send:          make(chan websocketOutboundMessage, ws.sendBuffer),
		sendWake:      make(chan struct{}, 1),
		done:          make(chan struct{}),
		pumpDoneCh:    make(chan struct{}),
		ws:            ws,
		remoteIP:      remoteIP,
		remotePort:    remotePort,
		priorityDepth: map[string]int{},
	}
}

func (r *sessionRegistry) nextSessionID() string {
	id := r.nextID.Add(1)
	return r.sessionIDPrefix + "-" + strconv.FormatUint(id, 10)
}

func (e *sessionEntry) bindSessionOwner(owner sessionOwnerLease) bool {
	if e == nil || owner == nil {
		return false
	}
	e.ownerMu.Lock()
	defer e.ownerMu.Unlock()
	if e.owner != nil {
		return false
	}
	e.ownerRevoked.Store(false)
	e.owner = owner
	return true
}

func (e *sessionEntry) activateSessionOwner() {
	if e == nil {
		return
	}
	e.ownerMu.Lock()
	owner := e.owner
	e.ownerMu.Unlock()
	if owner != nil {
		owner.sessionOwnerActivate()
	}
}

func (e *sessionEntry) clearSessionOwner(token sessionOwnerLease) bool {
	if e == nil || token == nil {
		return false
	}
	e.ownerMu.Lock()
	defer e.ownerMu.Unlock()
	if e.owner == nil || !e.owner.sessionOwnerEquals(token) {
		return false
	}
	e.ownerRevoked.Store(true)
	e.owner = nil
	token.sessionOwnerRevoke()
	return true
}

func (e *sessionEntry) notifySessionOwnerDetached(sessionID string) {
	if e == nil {
		return
	}
	e.ownerMu.Lock()
	e.ownerRevoked.Store(true)
	owner := e.owner
	e.owner = nil
	e.ownerMu.Unlock()
	if owner != nil {
		owner.sessionOwnerRevoke()
		owner.sessionOwnerDetached(sessionID)
	}
}

func (e *sessionEntry) isSessionOwnerActive() bool {
	if e == nil {
		return false
	}
	e.ownerMu.Lock()
	defer e.ownerMu.Unlock()
	if e.ownerRevoked.Load() {
		return false
	}
	return e.owner == nil || e.owner.sessionOwnerActive()
}

func (e *sessionEntry) enqueue(messageType int, data []byte) enqueueOutcome {
	if e == nil {
		return enqueueOutcome{result: enqueueClosed}
	}
	message := make([]byte, len(data))
	copy(message, data)
	return e.enqueueOwned(messageType, message)
}

// enqueueOwned transfers exclusive ownership of data to the websocket queue.
// Callers must not mutate the slice after this call.
func (e *sessionEntry) enqueueOwned(messageType int, data []byte) enqueueOutcome {
	return e.enqueueOwnedWithOwnerToken(messageType, data, "")
}

func (e *sessionEntry) registerSessionQueueObserver(
	events chan outboundQueueEvent,
	overflow func(outboundQueueEvent),
	capture func(uint64, chan struct{}) bool,
) (sessionQueueEventHandle, bool) {
	if e == nil || events == nil || overflow == nil || capture == nil {
		return nil, false
	}
	owner := newSessionQueueEventOwner(e, events, overflow, capture)
	if !e.registerQueueEventOwner(owner) {
		return nil, false
	}
	return owner, true
}

func (e *sessionEntry) registerQueueEventOwner(owner sessionQueueEventHandle) bool {
	if e == nil || owner == nil {
		return false
	}
	e.eventMu.Lock()
	defer e.eventMu.Unlock()
	if e.queueEventOwner != nil {
		return false
	}
	e.queueEventOwner = owner
	return true
}

func (e *sessionEntry) captureQueueEventBarrier(
	owner sessionQueueEventHandle,
	ack chan struct{},
) (func(), bool) {
	if e == nil || owner == nil || ack == nil {
		return nil, false
	}
	// Freeze all queue-event producers while the owner captures and registers
	// its sequence barrier. The returned release keeps later writer events from
	// publishing until the barrier acknowledgement has been observed.
	e.queueEventProducerMu.Lock()
	released := atomic.Bool{}
	release := func() {
		if released.CompareAndSwap(false, true) {
			e.queueEventProducerMu.Unlock()
		}
	}
	e.eventMu.Lock()
	if e.queueEventOwner != owner || !owner.captureBarrier(ack) {
		e.eventMu.Unlock()
		release()
		return nil, false
	}
	e.eventMu.Unlock()
	return release, true
}

func (e *sessionEntry) dispatchQueueEventsLocked(events []outboundQueueEventRecord) []func() {
	callbacks := make([]func(), 0)
	for _, record := range events {
		if callback := e.dispatchQueueEventLocked(record.event); callback != nil {
			callbacks = append(callbacks, callback)
		}
	}
	return callbacks
}

func dispatchQueueCallbacks(callbacks []func()) {
	for _, callback := range callbacks {
		if callback != nil {
			callback()
		}
	}
}

func (e *sessionEntry) dispatchQueueEvent(event outboundQueueEvent) {
	if e == nil {
		return
	}
	e.queueEventProducerMu.RLock()
	defer e.queueEventProducerMu.RUnlock()
	e.dispatchQueueEventWithProducerLock(event)
}

func (e *sessionEntry) dispatchQueueEventWithProducerLock(event outboundQueueEvent) {
	e.eventMu.Lock()
	callback := e.dispatchQueueEventLocked(event)
	e.eventMu.Unlock()
	if callback != nil {
		callback()
	}
}

func (e *sessionEntry) dispatchQueueEventLocked(event outboundQueueEvent) func() {
	owner := e.queueEventOwner
	if event.ownerToken == "" || owner == nil {
		return nil
	}
	return owner.dispatch(event)
}

func (e *sessionEntry) enqueueGenericOwned(messageType int, data []byte) enqueueOutcome {
	if e == nil {
		return enqueueOutcome{result: enqueueClosed}
	}
	// Hold the owner lock across the enqueue linearization point. Controller
	// detach clears the owner under the same lock, so generic writers cannot
	// enter a binding after its owner has been revoked.
	e.ownerMu.Lock()
	defer e.ownerMu.Unlock()
	if e.ownerRevoked.Load() || (e.owner != nil && !e.owner.sessionOwnerActive()) {
		return enqueueOutcome{result: enqueueClosed}
	}
	return e.enqueueOwnedWithOwnerToken(messageType, data, "")
}

func (e *sessionEntry) enqueueWithOwnerToken(
	messageType int,
	data []byte,
	ownerToken string,
) enqueueOutcome {
	if e == nil {
		return enqueueOutcome{result: enqueueClosed}
	}
	// Driver-specific admission uses the same neutral owner lease as generic
	// enqueue, but remains behind the session entry's generic owner state.
	e.ownerMu.Lock()
	defer e.ownerMu.Unlock()
	if e.ownerRevoked.Load() || e.owner == nil || !e.owner.sessionOwnerActive() {
		return enqueueOutcome{result: enqueueClosed}
	}
	return e.enqueueOwnedWithOwnerToken(messageType, data, ownerToken)
}

func (e *sessionEntry) enqueueOwnedWithOwnerToken(
	messageType int,
	data []byte,
	ownerToken string,
) enqueueOutcome {
	if e == nil {
		return enqueueOutcome{result: enqueueClosed}
	}
	// Serialize event publication with writer-side dequeue publication. Owner
	// overflow handlers are invoked only after this generic lock is released.
	e.queueEventProducerMu.RLock()
	defer e.queueEventProducerMu.RUnlock()
	e.eventMu.Lock()
	message := data
	classified := classifyOutboundMessage(message)
	e.mu.Lock()
	if e.closed {
		e.mu.Unlock()
		overflows := e.dispatchQueueEventsLocked([]outboundQueueEventRecord{{
			event: outboundQueueEvent{kind: outboundQueueEventClosed, ownerToken: ownerToken},
		}})
		e.eventMu.Unlock()
		dispatchQueueCallbacks(overflows)
		return enqueueOutcome{result: enqueueClosed, class: classified}
	}
	queueEvents := make([]outboundQueueEventRecord, 0, 2)
	if classified.canCollapse() {
		_, collapseEvents := e.dropQueuedByCollapseKeyLocked(classified.collapseKey)
		queueEvents = append(queueEvents, collapseEvents...)
	}
	outbound := websocketOutboundMessage{
		messageType: messageType,
		data:        message,
		class:       classified,
		enqueuedAt:  time.Now(),
		ownerToken:  ownerToken,
	}
	select {
	case e.send <- outbound:
		queueEvents = append(queueEvents, outboundQueueEventRecord{
			event: outboundQueueEvent{kind: outboundQueueEventEnqueued, ownerToken: ownerToken},
		})
		depth := uint64(len(e.send))
		e.incrementPriorityDepthLocked(classified.metricsPriority())
		updateOutboundQueuePeak(depth)
		updateOutboundQueuePriorityPeak(classified.metricsPriority(), depth)
		e.signalSendWakeLocked()
		e.mu.Unlock()
		overflows := e.dispatchQueueEventsLocked(queueEvents)
		e.eventMu.Unlock()
		dispatchQueueCallbacks(overflows)
		return enqueueOutcome{result: enqueueOK, class: classified}
	default:
		queueEvents = append(queueEvents, outboundQueueEventRecord{
			event: outboundQueueEvent{
				kind:        outboundQueueEventQueueRejected,
				reliability: classified.metricsReliability(),
				ownerToken:  ownerToken,
			},
		})
		if classified.canDropWhenFull() {
			gatewayMetrics.outboundDropsTotal.Add(1)
			recordOutboundDropByReliability(classified.metricsReliability())
			gatewayMetrics.outboundQueueFullTotal.Add(1)
			e.mu.Unlock()
			overflows := e.dispatchQueueEventsLocked(queueEvents)
			e.eventMu.Unlock()
			dispatchQueueCallbacks(overflows)
			return enqueueOutcome{result: enqueueDropped, class: classified}
		}
		e.mu.Unlock()
		overflows := e.dispatchQueueEventsLocked(queueEvents)
		e.eventMu.Unlock()
		dispatchQueueCallbacks(overflows)
		return enqueueOutcome{result: enqueueFull, class: classified}
	}
}

func (e *sessionEntry) markPumpStarted() {
	if e == nil {
		return
	}
	e.mu.Lock()
	e.pumpStartedFlag = true
	e.mu.Unlock()
}

func (e *sessionEntry) pumpStarted() bool {
	if e == nil {
		return false
	}
	e.mu.Lock()
	started := e.pumpStartedFlag
	e.mu.Unlock()
	return started
}

func (e *sessionEntry) pumpDone() <-chan struct{} {
	if e == nil {
		return nil
	}
	return e.pumpDoneCh
}

func (e *sessionEntry) signalSendWakeLocked() {
	if e == nil || e.sendWake == nil || len(e.send) == 0 {
		return
	}
	select {
	case e.sendWake <- struct{}{}:
	default:
	}
}

func (e *sessionEntry) prepareWritePumpQueue() <-chan struct{} {
	if e == nil {
		return nil
	}
	e.mu.Lock()
	if e.sendWake == nil {
		e.sendWake = make(chan struct{}, 1)
	}
	e.signalSendWakeLocked()
	wake := e.sendWake
	e.mu.Unlock()
	return wake
}

func (e *sessionEntry) dequeueQueued() (websocketOutboundMessage, bool) {
	if e == nil || e.send == nil {
		return websocketOutboundMessage{}, false
	}
	e.mu.Lock()
	select {
	case message := <-e.send:
		e.decrementPriorityDepthLocked(message.class.metricsPriority())
		e.signalSendWakeLocked()
		e.mu.Unlock()
		return message, true
	default:
		e.mu.Unlock()
		return websocketOutboundMessage{}, false
	}
}

func (e *sessionEntry) dropQueuedByCollapseKeyLocked(collapseKey string) (int, []outboundQueueEventRecord) {
	if collapseKey == "" || e == nil || e.send == nil {
		return 0, nil
	}
	retained := make([]websocketOutboundMessage, 0, len(e.send))
	dropped := 0
	events := make([]outboundQueueEventRecord, 0, 1)
	for {
		select {
		case queued := <-e.send:
			if queued.class.collapseKey == collapseKey && queued.class.canCollapse() {
				dropped++
				e.decrementPriorityDepthLocked(queued.class.metricsPriority())
				reliability := queued.class.metricsReliability()
				recordOutboundCollapseByReliability(reliability)
				events = append(events, outboundQueueEventRecord{
					event: outboundQueueEvent{
						kind:        outboundQueueEventCollapsed,
						reliability: reliability,
						ownerToken:  queued.ownerToken,
					},
				})
			} else {
				retained = append(retained, queued)
			}
		default:
			for _, queued := range retained {
				e.send <- queued
			}
			return dropped, events
		}
	}
}

func recordOutboundCollapseByReliability(reliability string) {
	gatewayMetrics.outboundCollapsedTotal.Add(1)
	switch reliability {
	case "critical":
		gatewayMetrics.outboundCriticalCollapsedTotal.Add(1)
	case "state":
		gatewayMetrics.outboundStateCollapsedTotal.Add(1)
	case "ephemeral":
		gatewayMetrics.outboundEphemeralCollapsedTotal.Add(1)
	default:
		gatewayMetrics.outboundImportantCollapsedTotal.Add(1)
	}
}

func (e *sessionEntry) incrementPriorityDepthLocked(priority string) {
	if e.priorityDepth == nil {
		e.priorityDepth = map[string]int{}
	}
	e.priorityDepth[priority]++
}

func (e *sessionEntry) decrementPriorityDepthLocked(priority string) {
	if e.priorityDepth == nil || e.priorityDepth[priority] <= 0 {
		return
	}
	e.priorityDepth[priority]--
}

func (e *sessionEntry) markDequeued(message websocketOutboundMessage) {
	e.dispatchQueueEvent(outboundQueueEvent{
		kind:       outboundQueueEventDequeued,
		ownerToken: message.ownerToken,
	})
}

func (e *sessionEntry) abandonQueued() {
	if e == nil || e.send == nil {
		return
	}
	e.queueEventProducerMu.RLock()
	defer e.queueEventProducerMu.RUnlock()
	e.eventMu.Lock()
	e.mu.Lock()
	events := make([]outboundQueueEventRecord, 0)
	for {
		select {
		case queued := <-e.send:
			e.decrementPriorityDepthLocked(queued.class.metricsPriority())
			events = append(events, outboundQueueEventRecord{
				event: outboundQueueEvent{
					kind:       outboundQueueEventAbandoned,
					ownerToken: queued.ownerToken,
				},
			})
		default:
			e.mu.Unlock()
			overflows := e.dispatchQueueEventsLocked(events)
			e.eventMu.Unlock()
			dispatchQueueCallbacks(overflows)
			return
		}
	}
}

func updateOutboundQueuePeak(depth uint64) {
	updateAtomicPeak(&gatewayMetrics.outboundQueuePeak, depth)
}

func updateOutboundQueuePriorityPeak(priority string, depth uint64) {
	switch priority {
	case "critical":
		updateAtomicPeak(&gatewayMetrics.outboundQueueCriticalPeak, depth)
	case "high":
		updateAtomicPeak(&gatewayMetrics.outboundQueueHighPeak, depth)
	case "low":
		updateAtomicPeak(&gatewayMetrics.outboundQueueLowPeak, depth)
	default:
		updateAtomicPeak(&gatewayMetrics.outboundQueueNormalPeak, depth)
	}
}

func updateAtomicPeak(metric *atomic.Uint64, depth uint64) {
	for {
		current := metric.Load()
		if depth <= current {
			return
		}
		if metric.CompareAndSwap(current, depth) {
			return
		}
	}
}

func recordOutboundQueueDwell(enqueuedAt, dequeuedAt time.Time) {
	if enqueuedAt.IsZero() || dequeuedAt.Before(enqueuedAt) {
		return
	}
	dwellUs := uint64(dequeuedAt.Sub(enqueuedAt) / time.Microsecond)
	gatewayMetrics.outboundQueueDwellUsTotal.Add(dwellUs)
	gatewayMetrics.outboundQueueDwellSamples.Add(1)
	updateAtomicPeak(&gatewayMetrics.outboundQueueDwellMaxUs, dwellUs)
}

func outboundQueueDwellMilliseconds(microseconds uint64) float64 {
	return float64(microseconds) / 1000.0
}

func (e *sessionEntry) close() {
	e.closeWithReason(0, "")
}

func (e *sessionEntry) closeWithReason(code int, text string) {
	if e == nil {
		return
	}
	e.closeOnce.Do(func() {
		e.mu.Lock()
		e.closed = true
		e.closeCode = code
		e.closeText = text
		close(e.done)
		conn := e.conn
		e.mu.Unlock()
		// Closing the underlying socket is part of the session terminal state:
		// it wakes both WebSocket readers and direct-proxy transport goroutines.
		if conn != nil {
			_ = conn.Close()
		}
	})
}

func (e *sessionEntry) closeFramePayload() []byte {
	if e == nil {
		return nil
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.closeCode == 0 {
		return nil
	}
	return websocket.FormatCloseMessage(e.closeCode, e.closeText)
}

func (e *sessionEntry) queueStats() (int, int) {
	if e == nil || e.send == nil {
		return 0, 0
	}
	return len(e.send), cap(e.send)
}

func (e *sessionEntry) writePump(sessionID string) {
	e.markPumpStarted()
	ticker := time.NewTicker(e.ws.pingInterval)
	sendWake := e.prepareWritePumpQueue()
	defer func() {
		ticker.Stop()
		e.abandonQueued()
		if e.conn != nil {
			_ = e.conn.Close()
		}
		if e.pumpDoneCh != nil {
			close(e.pumpDoneCh)
		}
	}()
	for {
		select {
		case <-sendWake:
			e.queueEventProducerMu.RLock()
			message, ok := e.dequeueQueued()
			if !ok {
				e.queueEventProducerMu.RUnlock()
				continue
			}
			e.dispatchQueueEventWithProducerLock(outboundQueueEvent{
				kind:       outboundQueueEventDequeued,
				ownerToken: message.ownerToken,
			})
			recordOutboundQueueDwell(message.enqueuedAt, time.Now())
			if message.class.isExpired(message.enqueuedAt, time.Now()) {
				gatewayMetrics.outboundDropsTotal.Add(1)
				recordOutboundDropByReliability(message.class.metricsReliability())
				e.dispatchQueueEventWithProducerLock(outboundQueueEvent{
					kind:       outboundQueueEventExpired,
					ownerToken: message.ownerToken,
				})
				e.queueEventProducerMu.RUnlock()
				continue
			}
			_ = e.conn.SetWriteDeadline(time.Now().Add(e.ws.writeTimeout))
			started := time.Now()
			e.dispatchQueueEventWithProducerLock(outboundQueueEvent{
				kind:       outboundQueueEventWriterStarted,
				ownerToken: message.ownerToken,
			})
			writeErr := e.conn.WriteMessage(message.messageType, message.data)
			e.dispatchQueueEventWithProducerLock(outboundQueueEvent{
				kind:       outboundQueueEventWriterFinished,
				ownerToken: message.ownerToken,
			})
			if writeErr != nil {
				gatewayMetrics.outboundErrorsTotal.Add(1)
				e.dispatchQueueEventWithProducerLock(outboundQueueEvent{
					kind:       outboundQueueEventWriteError,
					ownerToken: message.ownerToken,
				})
				e.closeWithReason(websocket.CloseInternalServerErr, "websocket_write_failed")
				log.Printf("websocket write failed for %s: %v", sessionID, writeErr)
				e.queueEventProducerMu.RUnlock()
				return
			}
			gatewayMetrics.outboundWriteLatencyMsTotal.Add(uint64(time.Since(started).Milliseconds()))
			gatewayMetrics.outboundWriteLatencySamples.Add(1)
			if message.messageType == websocket.TextMessage {
				gatewayMetrics.outboundMessagesTotal.Add(1)
			}
			e.dispatchQueueEventWithProducerLock(outboundQueueEvent{
				kind:       outboundQueueEventWritten,
				ownerToken: message.ownerToken,
			})
			e.queueEventProducerMu.RUnlock()
		case <-ticker.C:
			_ = e.conn.SetWriteDeadline(time.Now().Add(e.ws.writeTimeout))
			if err := e.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				gatewayMetrics.outboundErrorsTotal.Add(1)
				e.closeWithReason(websocket.CloseInternalServerErr, "websocket_ping_failed")
				log.Printf("websocket ping failed for %s: %v", sessionID, err)
				return
			}
			gatewayMetrics.websocketPingsSentTotal.Add(1)
		case <-e.done:
			_ = e.conn.SetWriteDeadline(time.Now().Add(e.ws.writeTimeout))
			_ = e.conn.WriteMessage(websocket.CloseMessage, e.closeFramePayload())
			return
		}
	}
}
