package main

import (
	"fmt"
	"log"
	"sync"
	"sync/atomic"
	"time"

	"github.com/gorilla/websocket"
)

// driverDeliveryController owns driver-only admission and terminal state. The
// session registry supplies only generic lifecycle removal and write-pump
// observations.
const driverDeliveryTerminalSchema = "xkx_driver_delivery_terminal_control_v1"

type driverDeliveryTerminalSnapshot struct {
	Schema                       string `json:"schema"`
	FreezeAcknowledged           bool   `json:"freeze_acknowledged"`
	AdmissionFrozen              bool   `json:"admission_frozen"`
	AdmissionActive              uint64 `json:"admission_active"`
	FreezeEpoch                  uint64 `json:"freeze_epoch"`
	Queued                       uint64 `json:"queued"`
	Inflight                     uint64 `json:"inflight"`
	DecodeSubmitted              uint64 `json:"decode_submitted_total"`
	DecodeCommitted              uint64 `json:"decode_committed_total"`
	DecodeInflight               uint64 `json:"decode_inflight"`
	ActiveWriters                uint64 `json:"active_writers"`
	WritePumpsActive             uint64 `json:"write_pumps_active"`
	DriverAdmissionWaiting       uint64 `json:"driver_admission_waiting"`
	DriverQueued                 uint64 `json:"driver_queued"`
	DriverInflight               uint64 `json:"driver_inflight"`
	DriverWritesActive           uint64 `json:"driver_writes_active"`
	DriverUnacked                uint64 `json:"driver_unacked"`
	SessionsActive               int    `json:"sessions_active"`
	DriverStatusRequestTS        int64  `json:"driver_status_request_ts"`
	DriverStatusGenerationBefore uint64 `json:"driver_status_generation_before"`
	DriverStatusRequested        bool   `json:"driver_status_requested"`
	Error                        string `json:"error,omitempty"`
}

type driverDeliverySessionSink interface {
	remove(sessionID string)
	writePumpCount() uint64
}

// driverSessionOwnerRegistrationPort adapts driver ownership to the neutral
// registry registration contract. The registry never receives this concrete
// type; only the driver coordinator creates it.
type driverSessionOwnerRegistrationPort struct {
	controller *driverDeliveryController
	rawSlot    string
	mu         sync.Mutex
	sessionID  string
	session    sessionOwnerEntryPort
}

func (p *driverSessionOwnerRegistrationPort) reserve(sessionID string, entry sessionOwnerEntryPort) bool {
	if p == nil || p.controller == nil || !p.controller.enabled || entry == nil {
		return false
	}
	session := entry
	if !p.controller.reserveSessionEntry(sessionID, session, p.rawSlot) {
		return false
	}
	p.mu.Lock()
	p.sessionID = sessionID
	p.session = session
	p.mu.Unlock()
	return true
}

func (p *driverSessionOwnerRegistrationPort) publish(sessionID string, entry sessionOwnerEntryPort) bool {
	if p == nil || p.controller == nil || entry == nil {
		return false
	}
	p.mu.Lock()
	registeredID := p.sessionID
	session := p.session
	p.mu.Unlock()
	if registeredID != sessionID || session == nil {
		return false
	}
	return p.controller.publishSessionEntry(sessionID, session)
}

func (p *driverSessionOwnerRegistrationPort) detach(sessionID string) func() {
	if p == nil || p.controller == nil {
		return nil
	}
	binding := p.controller.beginDetachSession(sessionID)
	return func() {
		p.controller.finishDetachSession(binding)
	}
}

// registrationPort is the only place where the driver coordinator is adapted
// to the generic registry owner port.
func (c *driverDeliveryController) registrationPort(rawSlot string) sessionOwnerRegistrationPort {
	if c == nil || !c.enabled {
		return nil
	}
	return &driverSessionOwnerRegistrationPort{controller: c, rawSlot: rawSlot}
}

// driverDeliverySessionAdapter is retained for tests that exercise the
// neutral session port without constructing a registry entry through the
// production registration path.
type driverDeliverySessionAdapter struct {
	entry *sessionEntry
}

func newDriverDeliverySessionAdapter(entry *sessionEntry) *driverDeliverySessionAdapter {
	if entry == nil {
		return nil
	}
	return &driverDeliverySessionAdapter{entry: entry}
}

func (s *driverDeliverySessionAdapter) bindSessionOwner(owner sessionOwnerLease) bool {
	if s == nil || s.entry == nil {
		return false
	}
	return s.entry.bindSessionOwner(owner)
}

func (s *driverDeliverySessionAdapter) registerSessionQueueObserver(
	events chan outboundQueueEvent,
	overflow func(outboundQueueEvent),
	capture func(uint64, chan struct{}) bool,
) (sessionQueueEventHandle, bool) {
	if s == nil || s.entry == nil || events == nil || overflow == nil || capture == nil {
		return nil, false
	}
	owner := newSessionQueueEventOwner(s.entry, events, overflow, capture)
	if !s.entry.registerQueueEventOwner(owner) {
		return nil, false
	}
	return owner, true
}

func (s *driverDeliverySessionAdapter) activateSessionOwner() {
	if s != nil && s.entry != nil {
		s.entry.activateSessionOwner()
	}
}

func (s *driverDeliverySessionAdapter) isSessionOwnerActive() bool {
	if s == nil || s.entry == nil {
		return false
	}
	return s.entry.isSessionOwnerActive()
}

func (s *driverDeliverySessionAdapter) clearSessionOwner(token sessionOwnerLease) bool {
	if s == nil || s.entry == nil {
		return false
	}
	return s.entry.clearSessionOwner(token)
}

func (s *driverDeliverySessionAdapter) captureQueueEventBarrier(
	owner sessionQueueEventHandle,
	ack chan struct{},
) (func(), bool) {
	if s == nil || s.entry == nil {
		return nil, false
	}
	return s.entry.captureQueueEventBarrier(owner, ack)
}

func (s *driverDeliverySessionAdapter) enqueueWithOwnerToken(
	messageType int,
	data []byte,
	ownerToken string,
) enqueueOutcome {
	if s == nil || s.entry == nil {
		return enqueueOutcome{result: enqueueClosed}
	}
	return s.entry.enqueueWithOwnerToken(messageType, data, ownerToken)
}

func (s *driverDeliverySessionAdapter) queueStats() (int, int) {
	if s == nil || s.entry == nil {
		return 0, 0
	}
	return s.entry.queueStats()
}

func (s *driverDeliverySessionAdapter) closeWithReason(code int, reason string) {
	if s != nil && s.entry != nil {
		s.entry.closeWithReason(code, reason)
	}
}

func (s *driverDeliverySessionAdapter) close() {
	if s != nil && s.entry != nil {
		s.entry.close()
	}
}

func (s *driverDeliverySessionAdapter) pumpStarted() bool {
	if s == nil || s.entry == nil {
		return false
	}
	return s.entry.pumpStarted()
}

func (s *driverDeliverySessionAdapter) pumpDone() <-chan struct{} {
	if s == nil || s.entry == nil {
		return nil
	}
	return s.entry.pumpDone()
}

func (s *driverDeliverySessionAdapter) abandonQueued() {
	if s != nil && s.entry != nil {
		s.entry.abandonQueued()
	}
}

const driverDeliveryEventBufferSize = 256

type driverDeliveryItemState uint8

const (
	driverDeliveryItemPending driverDeliveryItemState = iota
	driverDeliveryItemQueued
	driverDeliveryItemInflight
	driverDeliveryItemTerminal
)

type driverDeliveryTerminalPlacement uint8

const (
	driverDeliveryTerminalUnknown driverDeliveryTerminalPlacement = iota
	driverDeliveryTerminalQueued
	driverDeliveryTerminalInflight
)

type driverDeliveryItem struct {
	buffer *driverFrameBuffer
	once   sync.Once
	state  driverDeliveryItemState
}

func (item *driverDeliveryItem) applyEvent(
	event *outboundQueueEvent,
) (driverDeliveryTerminalPlacement, bool) {
	if item == nil || event == nil {
		return driverDeliveryTerminalUnknown, false
	}
	switch event.kind {
	case outboundQueueEventEnqueued:
		if item.state != driverDeliveryItemPending {
			return driverDeliveryTerminalUnknown, false
		}
		item.state = driverDeliveryItemQueued
	case outboundQueueEventDequeued:
		if item.state != driverDeliveryItemQueued {
			return driverDeliveryTerminalUnknown, false
		}
		item.state = driverDeliveryItemInflight
	case outboundQueueEventWriterStarted:
		return driverDeliveryTerminalUnknown, true
	case outboundQueueEventWriterFinished:
		return driverDeliveryTerminalUnknown, true
	case outboundQueueEventClosed,
		outboundQueueEventCollapsed,
		outboundQueueEventQueueRejected,
		outboundQueueEventAbandoned,
		outboundQueueEventExpired,
		outboundQueueEventWriteError,
		outboundQueueEventWritten:
		if item.state == driverDeliveryItemTerminal {
			return driverDeliveryTerminalUnknown, false
		}
		placement := driverDeliveryTerminalUnknown
		if item.state == driverDeliveryItemQueued {
			placement = driverDeliveryTerminalQueued
		} else if item.state == driverDeliveryItemInflight {
			placement = driverDeliveryTerminalInflight
		}
		item.state = driverDeliveryItemTerminal
		return placement, true
	}
	return driverDeliveryTerminalUnknown, true
}

func (item *driverDeliveryItem) release() {
	if item == nil {
		return
	}
	item.once.Do(func() {
		if item.buffer != nil {
			item.buffer.release()
		}
	})
}

type deliveryItemRegistration struct {
	deliveryID string
	buffer     *driverFrameBuffer
	ack        chan bool
}

type driverDeliveryFlushWaiter struct {
	sequence uint64
	ack      chan struct{}
}

type sessionQueueEventOwner struct {
	entry    *sessionEntry
	events   chan outboundQueueEvent
	overflow func(outboundQueueEvent)
	capture  func(uint64, chan struct{}) bool
	sequence uint64
	inFlight uint64
	idle     chan struct{}
}

func newSessionQueueEventOwner(
	entry *sessionEntry,
	events chan outboundQueueEvent,
	overflow func(outboundQueueEvent),
	capture func(uint64, chan struct{}) bool,
) *sessionQueueEventOwner {
	return &sessionQueueEventOwner{
		entry:    entry,
		events:   events,
		overflow: overflow,
		capture:  capture,
	}
}

// dispatch is called while the entry event lock is held. It only reserves an
// owner sequence and channel/overflow slot; the returned callback runs after
// the generic queue lock has been released.
func (owner *sessionQueueEventOwner) dispatch(event outboundQueueEvent) func() {
	if owner == nil || event.ownerToken == "" {
		return nil
	}
	owner.sequence++
	event.sequence = owner.sequence
	select {
	case owner.events <- event:
		return nil
	default:
		if owner.overflow == nil {
			return nil
		}
		if owner.inFlight == 0 {
			owner.idle = make(chan struct{})
		}
		owner.inFlight++
		return func() {
			owner.overflow(event)
			owner.entry.eventMu.Lock()
			owner.inFlight--
			if owner.inFlight == 0 && owner.idle != nil {
				close(owner.idle)
				owner.idle = nil
			}
			owner.entry.eventMu.Unlock()
		}
	}
}

func (owner *sessionQueueEventOwner) sequenceValue() uint64 {
	if owner == nil {
		return 0
	}
	return owner.sequence
}

// captureBarrier is called while the entry event lock is held. The sequence
// and waiter are therefore one publication point: an overflow callback that
// already reserved this sequence must submit before the waiter can settle.
func (owner *sessionQueueEventOwner) captureBarrier(ack chan struct{}) bool {
	if owner == nil || owner.capture == nil || ack == nil {
		return false
	}
	return owner.capture(owner.sequence, ack)
}

func (owner *sessionQueueEventOwner) clear() {
	if owner == nil || owner.entry == nil {
		return
	}
	entry := owner.entry
	entry.eventMu.Lock()
	if entry.queueEventOwner != owner {
		entry.eventMu.Unlock()
		return
	}
	entry.queueEventOwner = nil
	idle := owner.idle
	entry.eventMu.Unlock()
	if idle != nil {
		<-idle
	}
}

type driverDeliverySessionBinding struct {
	session     sessionOwnerEntryPort
	ownerLease  sessionOwnerLease
	ownerDetach func(string)
	observer    *driverDeliveryObserver
	events      chan outboundQueueEvent
	eventOwner  sessionQueueEventHandle
	published   bool

	items         map[string]*driverDeliveryItem
	registrations chan deliveryItemRegistration
	stop          chan struct{}
	stopOnce      sync.Once
	dispatchDone  chan struct{}
	finalized     chan struct{}
	finalizeOnce  sync.Once
	wake          chan struct{}

	inboxMu       sync.Mutex
	pendingEvents map[uint64]outboundQueueEvent
	nextEvent     uint64
	nextDispatch  uint64
	flushWaiters  []driverDeliveryFlushWaiter

	mu                sync.Mutex
	refs              uint64
	closed            bool
	done              chan struct{}
	dispatcherStarted atomic.Bool
}

func newDriverDeliverySessionBinding(
	session sessionOwnerEntryPort,
	observer *driverDeliveryObserver,
) *driverDeliverySessionBinding {
	binding := &driverDeliverySessionBinding{
		session:       session,
		observer:      observer,
		events:        make(chan outboundQueueEvent, driverDeliveryEventBufferSize),
		items:         map[string]*driverDeliveryItem{},
		stop:          make(chan struct{}),
		dispatchDone:  make(chan struct{}),
		finalized:     make(chan struct{}),
		wake:          make(chan struct{}, 1),
		pendingEvents: map[uint64]outboundQueueEvent{},
		nextEvent:     1,
		nextDispatch:  1,
		registrations: make(chan deliveryItemRegistration),
		done:          make(chan struct{}),
	}
	return binding
}

func (b *driverDeliverySessionBinding) startDispatcher() {
	if b == nil || !b.dispatcherStarted.CompareAndSwap(false, true) {
		return
	}
	go b.dispatchEvents()
}

func (b *driverDeliverySessionBinding) dispatchEvents() {
	defer close(b.dispatchDone)
	for {
		select {
		case registration := <-b.registrations:
			accepted := registration.deliveryID != "" && b.items[registration.deliveryID] == nil
			if accepted {
				b.items[registration.deliveryID] = &driverDeliveryItem{buffer: registration.buffer}
			}
			registration.ack <- accepted
		case event := <-b.events:
			b.submitEvent(event)
			b.drainReadyEvents()
		case <-b.wake:
			b.drainReadyEvents()
		case <-b.stop:
			b.drainChannel()
			for {
				b.drainReadyEvents()
				if b.pendingEventCount() == 0 {
					b.abandonRemainingDeliveryItems()
					b.drainReadyEvents()
					if b.pendingEventCount() == 0 && len(b.items) == 0 {
						return
					}
				}
				select {
				case registration := <-b.registrations:
					accepted := registration.deliveryID != "" && b.items[registration.deliveryID] == nil
					if accepted {
						b.items[registration.deliveryID] = &driverDeliveryItem{buffer: registration.buffer}
					}
					registration.ack <- accepted
				case event := <-b.events:
					b.submitEvent(event)
				case <-b.wake:
				}
			}
		}
	}
}

func (b *driverDeliverySessionBinding) submitEvent(event outboundQueueEvent) {
	if b == nil || event.ownerToken == "" {
		return
	}
	b.inboxMu.Lock()
	if event.sequence == 0 {
		event.sequence = b.nextEvent
	}
	if event.sequence >= b.nextEvent {
		b.nextEvent = event.sequence + 1
	}
	b.pendingEvents[event.sequence] = event
	b.inboxMu.Unlock()
	select {
	case b.wake <- struct{}{}:
	default:
	}
}

func (b *driverDeliverySessionBinding) pendingEventCount() int {
	b.inboxMu.Lock()
	defer b.inboxMu.Unlock()
	return len(b.pendingEvents)
}

func (b *driverDeliverySessionBinding) drainChannel() {
	for {
		select {
		case event := <-b.events:
			b.submitEvent(event)
		default:
			return
		}
	}
}

func (b *driverDeliverySessionBinding) addFlushWaiter(
	sequence uint64,
	ack chan struct{},
) {
	if b == nil || ack == nil {
		return
	}
	b.inboxMu.Lock()
	if sequence < b.nextDispatch {
		b.inboxMu.Unlock()
		close(ack)
		return
	}
	b.flushWaiters = append(b.flushWaiters, driverDeliveryFlushWaiter{
		sequence: sequence,
		ack:      ack,
	})
	b.inboxMu.Unlock()
	select {
	case b.wake <- struct{}{}:
	default:
	}
}

func (b *driverDeliverySessionBinding) drainReadyEvents() {
	for {
		b.inboxMu.Lock()
		event, ok := b.pendingEvents[b.nextDispatch]
		if !ok {
			ready := make([]chan struct{}, 0, len(b.flushWaiters))
			pending := b.flushWaiters[:0]
			for _, waiter := range b.flushWaiters {
				if waiter.sequence < b.nextDispatch {
					ready = append(ready, waiter.ack)
				} else {
					pending = append(pending, waiter)
				}
			}
			b.flushWaiters = pending
			b.inboxMu.Unlock()
			for _, ack := range ready {
				close(ack)
			}
			return
		}
		delete(b.pendingEvents, event.sequence)
		b.nextDispatch++
		b.inboxMu.Unlock()
		b.handleEvent(event)
	}
}

func (b *driverDeliverySessionBinding) flushEvents(timeout time.Duration) bool {
	if b == nil {
		return true
	}
	ack := make(chan struct{})
	var release func()
	captured := false
	if b.session != nil && b.eventOwner != nil {
		release, captured = b.session.captureQueueEventBarrier(b.eventOwner, ack)
	}
	if !captured {
		select {
		case <-b.dispatchDone:
			return true
		default:
			return false
		}
	}
	defer release()
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	select {
	case <-ack:
		return true
	case <-b.dispatchDone:
		return true
	case <-timer.C:
		return false
	}
}

func (b *driverDeliverySessionBinding) applyDeliveryEvent(
	event *outboundQueueEvent,
) (driverDeliveryTerminalPlacement, bool) {
	if b == nil || event == nil || event.ownerToken == "" {
		return driverDeliveryTerminalUnknown, false
	}
	item := b.deliveryItem(event.ownerToken)
	if item == nil {
		// Queue rejection and a closed entry release their item in the
		// synchronous enqueue result; their diagnostic event still counts.
		if event.kind != outboundQueueEventQueueRejected && event.kind != outboundQueueEventClosed {
			return driverDeliveryTerminalUnknown, false
		}
	} else {
		placement, ok := item.applyEvent(event)
		if !ok {
			return driverDeliveryTerminalUnknown, false
		}
		if isDriverDeliveryTerminalEvent(event.kind) {
			// Resource ownership ends at the neutral queue terminal, before any
			// diagnostic observer is allowed to block.
			b.releaseDeliveryItem(event.ownerToken)
		}
		return placement, true
	}
	if isDriverDeliveryTerminalEvent(event.kind) {
		// Rejected and closed items have no ledger entry, but remain observable.
		return driverDeliveryTerminalUnknown, true
	}
	return driverDeliveryTerminalUnknown, true
}

func (b *driverDeliverySessionBinding) recordDeliveryEvent(event outboundQueueEvent, placement driverDeliveryTerminalPlacement) {
	if b == nil || b.observer == nil {
		return
	}
	observer := b.observer
	observer.onOutboundQueueEvent(event, placement)
	if observer.eventHook != nil {
		// The test/diagnostic hook may stall; it must not block ordered metric
		// accounting or item teardown.
		go observer.notifyEventHook()
	}
}

func (b *driverDeliverySessionBinding) handleEvent(event outboundQueueEvent) {
	placement, ok := b.applyDeliveryEvent(&event)
	if !ok {
		return
	}
	b.recordDeliveryEvent(event, placement)
}

func (b *driverDeliverySessionBinding) handleEventOverflow(event outboundQueueEvent) {
	// Overflow submission joins the same ordered inbox as channel delivery. It
	// never mutates the owner ledger from the generic session call path.
	b.submitEvent(event)
}

func isDriverDeliveryTerminalEvent(kind uint8) bool {
	switch kind {
	case outboundQueueEventClosed,
		outboundQueueEventCollapsed,
		outboundQueueEventQueueRejected,
		outboundQueueEventAbandoned,
		outboundQueueEventExpired,
		outboundQueueEventWriteError,
		outboundQueueEventWritten:
		return true
	default:
		return false
	}
}

func (b *driverDeliverySessionBinding) registerDeliveryItem(deliveryID string, buffer *driverFrameBuffer) bool {
	if b == nil || deliveryID == "" {
		return false
	}
	registration := deliveryItemRegistration{
		deliveryID: deliveryID,
		buffer:     buffer,
		ack:        make(chan bool, 1),
	}
	select {
	case b.registrations <- registration:
	case <-b.stop:
		return false
	}
	select {
	case accepted := <-registration.ack:
		return accepted
	case <-b.stop:
		return false
	}
}

func (b *driverDeliverySessionBinding) deliveryItem(deliveryID string) *driverDeliveryItem {
	if b == nil || deliveryID == "" {
		return nil
	}
	return b.items[deliveryID]
}

func (b *driverDeliverySessionBinding) releaseDeliveryItem(deliveryID string) {
	if b == nil || deliveryID == "" {
		return
	}
	item := b.items[deliveryID]
	delete(b.items, deliveryID)
	if item != nil {
		item.release()
	}
}

func (b *driverDeliverySessionBinding) abandonRemainingDeliveryItems() {
	if b == nil {
		return
	}
	for deliveryID := range b.items {
		b.submitEvent(outboundQueueEvent{
			kind:       outboundQueueEventAbandoned,
			ownerToken: deliveryID,
		})
	}
}

func (b *driverDeliverySessionBinding) stopEvents() {
	if b == nil {
		return
	}
	b.stopOnce.Do(func() {
		if b.eventOwner != nil {
			b.eventOwner.clear()
		}
		if !b.dispatcherStarted.Load() {
			close(b.dispatchDone)
			return
		}
		close(b.stop)
		// Finalization must not remove the closing binding while the ordered
		// dispatcher can still mutate its item ledger. Owner observers are
		// asynchronous, so this wait does not inherit observer backpressure.
		<-b.dispatchDone
	})
}

func (b *driverDeliverySessionBinding) isPublished() bool {
	if b == nil {
		return false
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.published && !b.closed
}

func (b *driverDeliverySessionBinding) sessionOwnerActive() bool {
	return b.isPublished()
}

func (b *driverDeliverySessionBinding) sessionOwnerActivate() {
	if b == nil {
		return
	}
	b.mu.Lock()
	if !b.closed {
		b.published = true
	}
	b.mu.Unlock()
}

func (b *driverDeliverySessionBinding) sessionOwnerRevoke() {
	if b == nil {
		return
	}
	b.mu.Lock()
	b.published = false
	b.mu.Unlock()
}

func (b *driverDeliverySessionBinding) sessionOwnerDetached(sessionID string) {
	if b == nil || b.ownerDetach == nil {
		return
	}
	b.ownerDetach(sessionID)
}

func (b *driverDeliverySessionBinding) sessionOwnerEquals(other sessionOwnerLease) bool {
	candidate, ok := other.(*driverDeliverySessionBinding)
	return ok && candidate == b
}

func (b *driverDeliverySessionBinding) acquire() bool {
	if b == nil {
		return false
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.closed {
		return false
	}
	b.refs++
	return true
}

func (b *driverDeliverySessionBinding) release() {
	if b == nil {
		return
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.refs > 0 {
		b.refs--
	}
	if b.closed && b.refs == 0 {
		select {
		case <-b.done:
		default:
			close(b.done)
		}
	}
}

func (b *driverDeliverySessionBinding) close() {
	if b == nil {
		return
	}
	b.mu.Lock()
	b.closed = true
	b.published = false
	if b.refs == 0 {
		select {
		case <-b.done:
		default:
			close(b.done)
		}
	}
	b.mu.Unlock()
}

func (b *driverDeliverySessionBinding) waitForRefs() {
	if b == nil {
		return
	}
	<-b.done
}

type driverDeliveryController struct {
	sink            driverDeliverySessionSink
	audit           *deliveryAudit
	bindings        map[string]*driverDeliverySessionBinding
	closingBindings map[string]*driverDeliverySessionBinding
	nextDeliveryID  atomic.Uint64
	lifecycleMu     sync.Mutex
	mu              sync.Mutex

	admissionFrozen bool
	admissionActive uint64
	freezeEpoch     uint64
	enabled         bool
	admissionIdle   chan struct{}
}

func newDriverDeliveryController(sink driverDeliverySessionSink, audit *deliveryAudit, enabled bool) *driverDeliveryController {
	return &driverDeliveryController{
		sink:            sink,
		audit:           audit,
		bindings:        map[string]*driverDeliverySessionBinding{},
		closingBindings: map[string]*driverDeliverySessionBinding{},
		enabled:         enabled,
		admissionIdle:   closedSignal(),
	}
}

func (c *driverDeliveryController) newSessionObserver(rawSlot string) *driverDeliveryObserver {
	if c == nil {
		return nil
	}
	var slot *deliveryAuditSlot
	if c.audit != nil {
		slot = c.audit.claim(rawSlot)
	}
	return &driverDeliveryObserver{audit: slot}
}

func (c *driverDeliveryController) reserveSessionEntry(
	sessionID string,
	session sessionOwnerEntryPort,
	rawSlot string,
) bool {
	if c == nil || sessionID == "" || session == nil {
		return false
	}
	c.lifecycleMu.Lock()
	defer c.lifecycleMu.Unlock()
	c.mu.Lock()
	defer c.mu.Unlock()
	if _, exists := c.bindings[sessionID]; exists {
		return false
	}
	observer := c.newSessionObserver(rawSlot)
	binding := newDriverDeliverySessionBinding(session, observer)
	binding.ownerDetach = func(id string) {
		binding := c.beginDetachSession(id)
		go c.finishDetachSession(binding)
	}
	if !session.bindSessionOwner(binding) {
		binding.stopEvents()
		return false
	}
	binding.ownerLease = binding
	eventOwner, registered := session.registerSessionQueueObserver(
		binding.events,
		binding.handleEventOverflow,
		func(sequence uint64, ack chan struct{}) bool {
			binding.addFlushWaiter(sequence, ack)
			return true
		},
	)
	if !registered {
		session.clearSessionOwner(binding)
		binding.stopEvents()
		return false
	}
	binding.eventOwner = eventOwner
	c.bindings[sessionID] = binding
	binding.startDispatcher()
	return true
}

func (c *driverDeliveryController) publishSessionEntry(
	sessionID string,
	session sessionOwnerEntryPort,
) bool {
	if c == nil || sessionID == "" || session == nil {
		return false
	}
	c.lifecycleMu.Lock()
	defer c.lifecycleMu.Unlock()
	c.mu.Lock()
	defer c.mu.Unlock()
	binding, ok := c.bindings[sessionID]
	if !ok || binding == nil || binding.session != session {
		return false
	}
	binding.mu.Lock()
	closed := binding.closed
	if !closed {
		binding.published = true
	}
	binding.mu.Unlock()
	if closed {
		return false
	}
	session.activateSessionOwner()
	return true
}

// attachSessionEntry is retained for tests that construct an already-published
// session directly. Production registration uses reserve + publish around the
// registry publication and write-pump accounting.
func (c *driverDeliveryController) attachSessionEntry(sessionID string, entry *sessionEntry, rawSlot string) bool {
	session := newDriverDeliverySessionAdapter(entry)
	return c.reserveSessionEntry(sessionID, session, rawSlot) &&
		c.publishSessionEntry(sessionID, session)
}

func (c *driverDeliveryController) revokeSessionBinding(
	sessionID string,
	expected *driverDeliverySessionBinding,
) *driverDeliverySessionBinding {
	if c == nil {
		return nil
	}
	c.lifecycleMu.Lock()
	defer c.lifecycleMu.Unlock()
	c.mu.Lock()
	binding := c.bindings[sessionID]
	if binding == nil || (expected != nil && binding != expected) {
		c.mu.Unlock()
		return nil
	}
	delete(c.bindings, sessionID)
	c.closingBindings[sessionID] = binding
	c.mu.Unlock()
	binding.close()
	if binding.session != nil {
		binding.session.clearSessionOwner(binding.ownerLease)
	}
	return binding
}

func (c *driverDeliveryController) beginDetachSession(sessionID string) *driverDeliverySessionBinding {
	return c.revokeSessionBinding(sessionID, nil)
}

func (c *driverDeliveryController) finishDetachSession(binding *driverDeliverySessionBinding) {
	if binding == nil {
		return
	}
	// Every revoke path may race with an owner callback, registry teardown, or
	// an enqueue error. The binding itself owns the finalizer so ref draining,
	// pump join, ordered-ledger stop, and closing-map removal happen once.
	binding.finalizeOnce.Do(func() {
		binding.waitForRefs()
		if session := binding.session; session != nil {
			session.close()
			if session.pumpStarted() && session.pumpDone() != nil {
				<-session.pumpDone()
			} else {
				session.abandonQueued()
			}
		}
		binding.stopEvents()
		close(binding.finalized)
		c.removeClosingBinding(binding)
	})
}

func (b *driverDeliverySessionBinding) waitFinalized(timeout time.Duration) bool {
	if b == nil {
		return true
	}
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	select {
	case <-b.finalized:
		return true
	case <-timer.C:
		return false
	}
}

func (c *driverDeliveryController) removeClosingBinding(binding *driverDeliverySessionBinding) {
	if c == nil || binding == nil {
		return
	}
	c.mu.Lock()
	for sessionID, current := range c.closingBindings {
		if current == binding {
			delete(c.closingBindings, sessionID)
			break
		}
	}
	c.mu.Unlock()
}

func (c *driverDeliveryController) detachSessionEntry(sessionID string) {
	c.finishDetachSession(c.beginDetachSession(sessionID))
}

func (c *driverDeliveryController) unbindSession(
	sessionID string,
	binding *driverDeliverySessionBinding,
) bool {
	return c.revokeSessionBinding(sessionID, binding) != nil
}

func (c *driverDeliveryController) deliveryAuditSnapshot(slot int) (deliveryAuditSnapshot, bool) {
	if c == nil || c.audit == nil {
		return deliveryAuditSnapshot{}, false
	}
	return c.audit.snapshot(slot)
}

func releaseDriverFrameBuffer(buffer *driverFrameBuffer) {
	if buffer != nil {
		buffer.release()
	}
}

// terminateMalformedDriverOutput revokes only the currently bound session for a
// decoded output whose payload cannot be delivered. The decode worker has not
// registered an outbound item, so this path must not manufacture queue or
// written events; normal owner teardown remains responsible for the binding's
// queued ledger and buffer lifecycle.
func (c *driverDeliveryController) terminateMalformedDriverOutput(sessionID string) bool {
	if c == nil || sessionID == "" {
		return false
	}
	binding := c.revokeSessionBinding(sessionID, nil)
	if binding == nil {
		return false
	}
	if binding.observer != nil {
		binding.observer.onClosed()
	}
	gatewayMetrics.outboundClosedTotal.Add(1)
	if c.sink != nil {
		c.sink.remove(sessionID)
	}
	go c.finishDetachSession(binding)
	return true
}

func (c *driverDeliveryController) enqueueDriverOutput(
	sessionID string,
	message []byte,
	buffer *driverFrameBuffer,
) sessionWriteResult {
	if c == nil || c.sink == nil {
		releaseDriverFrameBuffer(buffer)
		return sessionWriteResult{reason: sessionWriteReasonAdmissionFrozen}
	}
	ownedMessage := message
	gatewayMetrics.driverDeliveryReceivedTotal.Add(1)
	var binding *driverDeliverySessionBinding
	c.mu.Lock()
	candidate, ok := c.bindings[sessionID]
	valid := ok && candidate != nil && candidate.session != nil && candidate.observer != nil && candidate.session.isSessionOwnerActive() && candidate.isPublished() && candidate.acquire()
	if valid {
		binding = candidate
	}
	c.mu.Unlock()
	if !valid {
		releaseDriverFrameBuffer(buffer)
		gatewayMetrics.driverDeliveryLateTotal.Add(1)
		return sessionWriteResult{reason: sessionWriteReasonMissing}
	}

	c.mu.Lock()
	admitted := c.beginDriverDeliveryAdmissionLocked()
	c.mu.Unlock()
	if !admitted {
		binding.observer.onDriverOutput()
		binding.observer.onClosed()
		binding.release()
		releaseDriverFrameBuffer(buffer)
		gatewayMetrics.outboundClosedTotal.Add(1)
		gatewayMetrics.driverDeliveryAdmissionRejectedTotal.Add(1)
		return sessionWriteResult{reason: sessionWriteReasonAdmissionFrozen}
	}

	binding.observer.onDriverOutput()
	deliveryID := fmt.Sprintf("%s-%d", sessionID, c.nextDeliveryID.Add(1))
	if !binding.registerDeliveryItem(deliveryID, buffer) {
		binding.release()
		c.endDriverDeliveryAdmission()
		releaseDriverFrameBuffer(buffer)
		return sessionWriteResult{reason: sessionWriteReasonClosed}
	}
	outcome := binding.session.enqueueWithOwnerToken(
		websocket.TextMessage,
		ownedMessage,
		deliveryID,
	)
	result := sessionWriteResult{}
	removeSession := false
	switch outcome.result {
	case enqueueOK:
		result.ok = true
	case enqueueClosed:
		removeSession = true
		gatewayMetrics.outboundClosedTotal.Add(1)
		result.reason = sessionWriteReasonClosed
	case enqueueFull:
		queueDepth, queueCapacity := binding.session.queueStats()
		binding.session.closeWithReason(websocket.CloseTryAgainLater, sessionWriteReasonQueueFull)
		removeSession = true
		gatewayMetrics.outboundDropsTotal.Add(1)
		recordOutboundDropByReliability(outcome.class.metricsReliability())
		gatewayMetrics.outboundQueueFullTotal.Add(1)
		log.Printf(
			"websocket outbound queue overflow for %s: message_bytes=%d queue_depth=%d queue_capacity=%d reliability=%s priority=%s",
			sessionID,
			len(ownedMessage),
			queueDepth,
			queueCapacity,
			outcome.class.metricsReliability(),
			outcome.class.metricsPriority(),
		)
		result.reason = sessionWriteReasonQueueFull
	case enqueueDropped:
		result.ok = true
	default:
		removeSession = true
		gatewayMetrics.outboundClosedTotal.Add(1)
		result.reason = sessionWriteReasonClosed
	}
	c.endDriverDeliveryAdmission()
	binding.release()
	if removeSession && c.unbindSession(sessionID, binding) {
		// Revoke and remove the registry entry on the decode path, but defer
		// reference draining and pump/ledger finalization to the owner cleanup.
		c.sink.remove(sessionID)
		go c.finishDetachSession(binding)
	}
	return result
}

type driverDeliveryObserver struct {
	audit     *deliveryAuditSlot
	eventHook func()
}

func (o *driverDeliveryObserver) onDriverOutput() {
	if o != nil {
		o.audit.recordDriverOutput()
	}
}

func (o *driverDeliveryObserver) notifyEventHook() {
	if o != nil && o.eventHook != nil {
		o.eventHook()
	}
}

func (o *driverDeliveryObserver) onOutboundQueueEvent(
	event outboundQueueEvent,
	placement driverDeliveryTerminalPlacement,
) {
	switch event.kind {
	case outboundQueueEventEnqueued:
		o.onEnqueued()
	case outboundQueueEventClosed:
		o.onClosed()
	case outboundQueueEventDequeued:
		o.onDequeued()
	case outboundQueueEventCollapsed:
		o.onCollapsed(event, placement)
	case outboundQueueEventQueueRejected:
		o.onQueueRejected()
	case outboundQueueEventAbandoned:
		o.onAbandoned(placement)
	case outboundQueueEventExpired:
		o.onExpired(placement)
	case outboundQueueEventWriterStarted:
		o.onWriterStarted()
	case outboundQueueEventWriterFinished:
		o.onWriterFinished()
	case outboundQueueEventWriteError:
		o.onWriteError(placement)
	case outboundQueueEventWritten:
		o.onWritten(placement)
	}
}

func (o *driverDeliveryObserver) onEnqueued() {
	if o != nil {
		o.audit.recordEnqueued()
	}
	recordDriverDeliveryEnqueued()
}

func (o *driverDeliveryObserver) onDequeued() { recordDriverDeliveryDequeued() }

func (o *driverDeliveryObserver) onCollapsed(
	event outboundQueueEvent,
	placement driverDeliveryTerminalPlacement,
) {
	if o != nil {
		o.audit.recordCollapsed()
	}
	if placement == driverDeliveryTerminalQueued {
		recordDriverDeliveryQueuedCollapse(event.reliability)
		return
	}
	gatewayMetrics.driverDeliveryCollapsedTotal.Add(1)
}

func (o *driverDeliveryObserver) onQueueRejected() {
	if o != nil {
		o.audit.recordQueueRejected()
	}
	gatewayMetrics.driverDeliveryQueueRejectedTotal.Add(1)
}

func (o *driverDeliveryObserver) onAbandoned(placement driverDeliveryTerminalPlacement) {
	if placement == driverDeliveryTerminalQueued {
		recordDriverDeliveryQueuedTerminal(&gatewayMetrics.driverDeliveryAbandonedTotal)
	} else if placement == driverDeliveryTerminalInflight {
		recordDriverDeliveryInflightTerminal(&gatewayMetrics.driverDeliveryAbandonedTotal)
	} else {
		gatewayMetrics.driverDeliveryAbandonedTotal.Add(1)
	}
}

func (o *driverDeliveryObserver) onExpired(placement driverDeliveryTerminalPlacement) {
	if o != nil {
		o.audit.recordExpiredDrop()
	}
	if placement == driverDeliveryTerminalQueued {
		recordDriverDeliveryQueuedTerminal(&gatewayMetrics.driverDeliveryExpiredTotal)
	} else if placement == driverDeliveryTerminalInflight {
		recordDriverDeliveryInflightTerminal(&gatewayMetrics.driverDeliveryExpiredTotal)
	} else {
		gatewayMetrics.driverDeliveryExpiredTotal.Add(1)
	}
}

func (o *driverDeliveryObserver) onWriterStarted()  { recordDriverDeliveryWriterStarted() }
func (o *driverDeliveryObserver) onWriterFinished() { recordDriverDeliveryWriterFinished() }

func (o *driverDeliveryObserver) onWriteError(placement driverDeliveryTerminalPlacement) {
	if o != nil {
		o.audit.recordWriteError()
	}
	if placement == driverDeliveryTerminalQueued {
		recordDriverDeliveryQueuedTerminal(&gatewayMetrics.driverDeliveryWriteErrorsTotal)
	} else if placement == driverDeliveryTerminalInflight {
		recordDriverDeliveryInflightTerminal(&gatewayMetrics.driverDeliveryWriteErrorsTotal)
	} else {
		gatewayMetrics.driverDeliveryWriteErrorsTotal.Add(1)
	}
}

func (o *driverDeliveryObserver) onWritten(placement driverDeliveryTerminalPlacement) {
	if o != nil {
		o.audit.recordWritten()
	}
	if placement == driverDeliveryTerminalQueued {
		recordDriverDeliveryQueuedTerminal(&gatewayMetrics.driverDeliveryWrittenTotal)
	} else if placement == driverDeliveryTerminalInflight {
		recordDriverDeliveryInflightTerminal(&gatewayMetrics.driverDeliveryWrittenTotal)
	} else {
		gatewayMetrics.driverDeliveryWrittenTotal.Add(1)
	}
}

func (o *driverDeliveryObserver) onClosed() {
	gatewayMetrics.driverDeliveryClosedTotal.Add(1)
}

func (c *driverDeliveryController) beginDriverDeliveryAdmission() bool {
	if c == nil {
		return false
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.beginDriverDeliveryAdmissionLocked()
}

func (c *driverDeliveryController) beginDriverDeliveryAdmissionLocked() bool {
	if !c.enabled || c.admissionFrozen {
		return false
	}
	if c.admissionActive == 0 {
		c.admissionIdle = make(chan struct{})
	}
	c.admissionActive++
	gatewayMetrics.driverDeliveryAdmissionActive.Add(1)
	return true
}

func (c *driverDeliveryController) endDriverDeliveryAdmission() {
	if c == nil {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	c.endDriverDeliveryAdmissionLocked()
}

func (c *driverDeliveryController) endDriverDeliveryAdmissionLocked() {
	if c.admissionActive > 0 {
		c.admissionActive--
		gatewayMetrics.driverDeliveryAdmissionActive.Add(^uint64(0))
		if c.admissionActive == 0 {
			close(c.admissionIdle)
		}
	}
}

func (c *driverDeliveryController) freezeDriverDeliveryAdmission() driverDeliveryTerminalSnapshot {
	if c == nil {
		return driverDeliveryTerminalSnapshot{Schema: driverDeliveryTerminalSchema, Error: "driver delivery controller unavailable"}
	}
	if c.sink == nil {
		return driverDeliveryTerminalSnapshot{Schema: driverDeliveryTerminalSchema, Error: "driver delivery session sink unavailable"}
	}
	c.mu.Lock()
	if !c.admissionFrozen {
		c.admissionFrozen = true
		c.freezeEpoch++
		gatewayMetrics.driverDeliveryAdmissionFrozen.Store(1)
	}
	c.mu.Unlock()
	if !c.waitForAdmissionIdle(time.Second) {
		return driverDeliveryTerminalSnapshot{
			Schema: driverDeliveryTerminalSchema,
			Error:  "driver delivery admissions did not quiesce",
		}
	}
	return c.driverDeliveryTerminalSnapshot()
}

func (c *driverDeliveryController) driverDeliveryTerminalSnapshot() driverDeliveryTerminalSnapshot {
	return c.readDriverDeliveryTerminalSnapshot()
}

func (c *driverDeliveryController) waitForAdmissionIdle(timeout time.Duration) bool {
	if c == nil {
		return true
	}
	timer := time.NewTimer(timeout)
	defer timer.Stop()
	for {
		c.mu.Lock()
		if c.admissionActive == 0 {
			c.mu.Unlock()
			return true
		}
		idle := c.admissionIdle
		c.mu.Unlock()
		select {
		case <-idle:
		case <-timer.C:
			return false
		}
	}
}

func closedSignal() chan struct{} {
	closed := make(chan struct{})
	close(closed)
	return closed
}

func (c *driverDeliveryController) flushActiveDeliveryEvents(timeout time.Duration) bool {
	if c == nil {
		return true
	}
	c.lifecycleMu.Lock()
	defer c.lifecycleMu.Unlock()
	return c.flushActiveDeliveryEventsLocked(timeout)
}

func (c *driverDeliveryController) flushActiveDeliveryEventsLocked(timeout time.Duration) bool {
	if c == nil {
		return true
	}
	c.mu.Lock()
	bindings := make([]*driverDeliverySessionBinding, 0, len(c.bindings)+len(c.closingBindings))
	closing := make(map[*driverDeliverySessionBinding]struct{}, len(c.closingBindings))
	seen := make(map[*driverDeliverySessionBinding]struct{}, len(c.bindings)+len(c.closingBindings))
	for _, binding := range c.bindings {
		if binding != nil && binding.isPublished() {
			bindings = append(bindings, binding)
			seen[binding] = struct{}{}
		}
	}
	for _, binding := range c.closingBindings {
		if binding != nil {
			if _, exists := seen[binding]; !exists {
				bindings = append(bindings, binding)
			}
			closing[binding] = struct{}{}
		}
	}
	c.mu.Unlock()
	deadline := time.Now().Add(timeout)
	for _, binding := range bindings {
		remaining := time.Until(deadline)
		if remaining <= 0 || !binding.flushEvents(remaining) {
			return false
		}
		if _, isClosing := closing[binding]; isClosing {
			remaining = time.Until(deadline)
			if remaining <= 0 || !binding.waitFinalized(remaining) {
				return false
			}
		}
	}
	return true
}

func (c *driverDeliveryController) readDriverDeliveryTerminalSnapshot() driverDeliveryTerminalSnapshot {
	if c == nil {
		return driverDeliveryTerminalSnapshot{Schema: driverDeliveryTerminalSchema, Error: "driver delivery controller unavailable"}
	}
	if c.sink == nil {
		return driverDeliveryTerminalSnapshot{Schema: driverDeliveryTerminalSchema, Error: "driver delivery session sink unavailable"}
	}
	c.lifecycleMu.Lock()
	defer c.lifecycleMu.Unlock()
	if !c.waitForAdmissionIdle(time.Second) {
		return driverDeliveryTerminalSnapshot{
			Schema: driverDeliveryTerminalSchema,
			Error:  "driver delivery admissions did not quiesce",
		}
	}
	if !c.flushActiveDeliveryEventsLocked(time.Second) {
		return driverDeliveryTerminalSnapshot{
			Schema: driverDeliveryTerminalSchema,
			Error:  "driver delivery event dispatcher did not quiesce",
		}
	}
	c.mu.Lock()
	frozen := c.admissionFrozen
	active := c.admissionActive
	epoch := c.freezeEpoch
	c.mu.Unlock()

	queued := gatewayMetrics.driverDeliveryQueued.Load()
	inflight := gatewayMetrics.driverDeliveryInflight.Load()
	decodeSubmitted := gatewayMetrics.driverDecodeSubmittedTotal.Load()
	decodeCommitted := gatewayMetrics.driverDecodeCommittedTotal.Load()
	decodeInflight := gatewayMetrics.driverDecodeInflight.Load()
	writers := gatewayMetrics.driverDeliveryActiveWriters.Load()
	pumps := c.sink.writePumpCount()
	driverWaiting := gatewayMetrics.driverFramesAdmissionWaiting.Load()
	driverQueued := gatewayMetrics.driverFramesQueued.Load()
	driverInflight := gatewayMetrics.driverFramesInflight.Load()
	driverWritesActive := gatewayMetrics.driverWritesActive.Load()
	driverUnacked := gatewayMetrics.driverFramesUnacked.Load()
	c.mu.Lock()
	sessions := 0
	for _, binding := range c.bindings {
		if binding != nil && binding.isPublished() {
			sessions++
		}
	}
	c.mu.Unlock()
	return driverDeliveryTerminalSnapshot{
		Schema:                 driverDeliveryTerminalSchema,
		FreezeAcknowledged:     frozen,
		AdmissionFrozen:        frozen,
		AdmissionActive:        active,
		FreezeEpoch:            epoch,
		Queued:                 queued,
		Inflight:               inflight,
		DecodeSubmitted:        decodeSubmitted,
		DecodeCommitted:        decodeCommitted,
		DecodeInflight:         decodeInflight,
		ActiveWriters:          writers,
		WritePumpsActive:       pumps,
		DriverAdmissionWaiting: driverWaiting,
		DriverQueued:           driverQueued,
		DriverInflight:         driverInflight,
		DriverWritesActive:     driverWritesActive,
		DriverUnacked:          driverUnacked,
		SessionsActive:         sessions,
	}
}
