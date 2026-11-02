package main

import (
	"log"

	"github.com/gorilla/websocket"
)

// sessionOwnerRegistrationPort is the neutral owner hook used by the
// registry. The registry coordinates publication and removal, but it does not
// know which transport owns an entry or how that owner stores its state.
type sessionOwnerRegistrationPort interface {
	reserve(sessionID string, entry sessionOwnerEntryPort) bool
	publish(sessionID string, entry sessionOwnerEntryPort) bool
	detach(sessionID string) func()
}

func (r *sessionRegistry) add(conn *websocket.Conn, remoteAddr string) string {
	return r.addWithOwner(conn, remoteAddr, nil)
}

// addWithOwner serializes the owner registration transaction with generic
// lookup/removal. The lock order is always registry -> owner; owner cleanup is
// returned as a closure and runs after the registry gate is released.
func (r *sessionRegistry) addWithOwner(
	conn *websocket.Conn,
	remoteAddr string,
	owner sessionOwnerRegistrationPort,
) string {
	sessionID := r.nextSessionID()
	entry := newSessionEntry(conn, remoteAddr, r.ws)

	r.registrationMu.Lock()
	if owner != nil && !owner.reserve(sessionID, entry) {
		r.registrationMu.Unlock()
		return ""
	}

	r.writePumpsActive.Add(1)
	gatewayMetrics.websocketWritePumpsActive.Add(1)
	entry.markPumpStarted()
	go func() {
		defer func() {
			r.writePumpsActive.Add(^uint64(0))
			gatewayMetrics.websocketWritePumpsActive.Add(^uint64(0))
		}()
		entry.writePump(sessionID)
	}()

	// Publish a pending entry before activating its owner. Generic lookup and
	// driver ingress both remain closed until the owner publish completes.
	r.mu.Lock()
	if _, exists := r.sessions[sessionID]; exists {
		r.mu.Unlock()
		cleanup := func() {}
		if owner != nil {
			cleanup = owner.detach(sessionID)
		}
		r.registrationMu.Unlock()
		entry.close()
		if entry.pumpDone() != nil {
			<-entry.pumpDone()
		}
		cleanup()
		return ""
	}
	r.sessions[sessionID] = entry
	r.mu.Unlock()

	if owner != nil && !owner.publish(sessionID, entry) {
		cleanup := owner.detach(sessionID)
		r.mu.Lock()
		if r.sessions[sessionID] == entry {
			delete(r.sessions, sessionID)
		}
		r.mu.Unlock()
		r.registrationMu.Unlock()
		entry.close()
		if entry.pumpDone() != nil {
			<-entry.pumpDone()
		}
		if cleanup != nil {
			cleanup()
		}
		return ""
	}

	r.registrationMu.Unlock()
	return sessionID
}

func (r *sessionRegistry) remove(sessionID string) {
	entry := r.detach(sessionID)
	if entry == nil {
		return
	}
	entry.notifySessionOwnerDetached(sessionID)
	if entry.pumpDone() == nil {
		entry.abandonQueued()
	}
}

// removeWithOwner revokes the owner and removes the generic entry under the
// same registry gate. The potentially blocking owner finalizer runs after the
// gate is released, so registry publication never waits on teardown callbacks.
func (r *sessionRegistry) removeWithOwner(
	sessionID string,
	owner sessionOwnerRegistrationPort,
) {
	if owner == nil {
		r.removeAndWait(sessionID)
		return
	}

	r.registrationMu.Lock()
	cleanup := owner.detach(sessionID)
	entry := r.detachLocked(sessionID)
	r.registrationMu.Unlock()

	if entry == nil && cleanup == nil {
		return
	}
	if cleanup != nil {
		cleanup()
	}
}

func (r *sessionRegistry) detach(sessionID string) *sessionEntry {
	r.registrationMu.Lock()
	defer r.registrationMu.Unlock()
	return r.detachLocked(sessionID)
}

func (r *sessionRegistry) detachLocked(sessionID string) *sessionEntry {
	r.mu.Lock()
	entry := r.sessions[sessionID]
	if entry != nil {
		// Close before publishing the removal so an enqueue that already holds
		// the entry can linearize before detach, while later lookups cannot
		// enqueue into a detached entry.
		entry.close()
	}
	delete(r.sessions, sessionID)
	r.mu.Unlock()
	return entry
}

// removeAndWait is reserved for shutdown paths and deterministic tests. Driver
// output commit must use remove so one blocked WebSocket cannot stall every CID.
func (r *sessionRegistry) removeAndWait(sessionID string) {
	entry := r.detach(sessionID)
	if entry == nil {
		return
	}
	entry.notifySessionOwnerDetached(sessionID)
	entry.mu.Lock()
	pumpStarted := entry.pumpStartedFlag
	entry.mu.Unlock()
	if pumpStarted && entry.pumpDone() != nil {
		<-entry.pumpDone()
		return
	}
	entry.abandonQueued()
}

// closeAll is the shutdown-wide session terminal path. It revokes owner
// bindings while holding the same registry gate as normal detach, then closes
// every socket before waiting for write pumps. Closing the socket is essential:
// a hijacked WebSocket is no longer owned by net/http.Server.Shutdown.
func (r *sessionRegistry) closeAll(owner sessionOwnerRegistrationPort) int {
	if r == nil {
		return 0
	}
	type closingSession struct {
		entry   *sessionEntry
		cleanup func()
	}
	closing := make([]closingSession, 0)

	r.registrationMu.Lock()
	for sessionID := range r.sessions {
		var cleanup func()
		if owner != nil {
			cleanup = owner.detach(sessionID)
		}
		entry := r.detachLocked(sessionID)
		if entry != nil {
			closing = append(closing, closingSession{entry: entry, cleanup: cleanup})
		} else if cleanup != nil {
			closing = append(closing, closingSession{cleanup: cleanup})
		}
	}
	r.registrationMu.Unlock()

	for _, item := range closing {
		if item.cleanup != nil {
			item.cleanup()
		}
		if item.entry == nil {
			continue
		}
		if owner == nil {
			item.entry.notifySessionOwnerDetached("")
		}
		item.entry.mu.Lock()
		pumpStarted := item.entry.pumpStartedFlag
		item.entry.mu.Unlock()
		if pumpStarted && item.entry.pumpDone() != nil {
			<-item.entry.pumpDone()
		} else {
			item.entry.abandonQueued()
		}
	}
	return len(closing)
}

func (r *sessionRegistry) get(sessionID string) (*sessionEntry, bool) {
	r.registrationMu.RLock()
	defer r.registrationMu.RUnlock()
	r.mu.RLock()
	entry, ok := r.sessions[sessionID]
	r.mu.RUnlock()
	if !ok || entry == nil || !entry.isSessionOwnerActive() {
		return nil, false
	}
	return entry, true
}

func (r *sessionRegistry) writeText(sessionID string, message []byte) bool {
	return r.writeTextResult(sessionID, message).ok
}

func (r *sessionRegistry) writeTextResult(sessionID string, message []byte) sessionWriteResult {
	return r.enqueueOwned(sessionID, message)
}

func (r *sessionRegistry) writeTextResultOwned(sessionID string, message []byte) sessionWriteResult {
	return r.enqueueOwned(sessionID, message)
}

// enqueueOwned is the generic session sink. Owner-specific lifecycle tokens are
// intentionally not accepted here; driver delivery uses its controller-held
// session binding and calls the entry directly.
func (r *sessionRegistry) enqueueOwned(sessionID string, message []byte) sessionWriteResult {
	// Keep registry publication and generic enqueue in one read-side critical
	// section. Driver detach uses the matching write gate before revoking and
	// removing an entry, while enqueueGenericOwned serializes with owner revoke.
	r.registrationMu.RLock()
	r.mu.RLock()
	entry, ok := r.sessions[sessionID]
	var outcome enqueueOutcome
	if ok && entry != nil {
		outcome = entry.enqueueGenericOwned(websocket.TextMessage, message)
	}
	r.mu.RUnlock()
	r.registrationMu.RUnlock()
	if !ok || entry == nil {
		gatewayMetrics.outboundLateFramesTotal.Add(1)
		return sessionWriteResult{reason: sessionWriteReasonMissing}
	}
	switch outcome.result {
	case enqueueOK:
		return sessionWriteResult{ok: true}
	case enqueueClosed:
		r.remove(sessionID)
		gatewayMetrics.outboundClosedTotal.Add(1)
		return sessionWriteResult{reason: sessionWriteReasonClosed}
	case enqueueFull:
		queueDepth, queueCapacity := entry.queueStats()
		entry.closeWithReason(websocket.CloseTryAgainLater, sessionWriteReasonQueueFull)
		r.remove(sessionID)
		gatewayMetrics.outboundDropsTotal.Add(1)
		recordOutboundDropByReliability(outcome.class.metricsReliability())
		gatewayMetrics.outboundQueueFullTotal.Add(1)
		log.Printf(
			"websocket outbound queue overflow for %s: message_bytes=%d queue_depth=%d queue_capacity=%d reliability=%s priority=%s",
			sessionID,
			len(message),
			queueDepth,
			queueCapacity,
			outcome.class.metricsReliability(),
			outcome.class.metricsPriority(),
		)
		return sessionWriteResult{reason: sessionWriteReasonQueueFull}
	case enqueueDropped:
		queueDepth, queueCapacity := entry.queueStats()
		log.Printf(
			"websocket outbound message dropped for %s: message_bytes=%d queue_depth=%d queue_capacity=%d reliability=%s priority=%s",
			sessionID,
			len(message),
			queueDepth,
			queueCapacity,
			outcome.class.metricsReliability(),
			outcome.class.metricsPriority(),
		)
		return sessionWriteResult{ok: true}
	default:
		gatewayMetrics.outboundClosedTotal.Add(1)
		return sessionWriteResult{reason: sessionWriteReasonClosed}
	}
}

func recordOutboundDropByReliability(reliability string) {
	switch reliability {
	case "critical":
		gatewayMetrics.outboundCriticalDropsTotal.Add(1)
	case "state":
		gatewayMetrics.outboundStateDropsTotal.Add(1)
	case "ephemeral":
		gatewayMetrics.outboundEphemeralDropsTotal.Add(1)
	default:
		gatewayMetrics.outboundImportantDropsTotal.Add(1)
	}
}

func (r *sessionRegistry) count() int {
	r.registrationMu.RLock()
	defer r.registrationMu.RUnlock()
	r.mu.RLock()
	defer r.mu.RUnlock()
	count := 0
	for _, entry := range r.sessions {
		if entry != nil && entry.isSessionOwnerActive() {
			count++
		}
	}
	return count
}

func (r *sessionRegistry) writePumpCount() uint64 {
	if r == nil {
		return 0
	}
	return r.writePumpsActive.Load()
}

func (r *sessionRegistry) queuedMessages() int {
	r.registrationMu.RLock()
	defer r.registrationMu.RUnlock()
	r.mu.RLock()
	defer r.mu.RUnlock()
	total := 0
	for _, entry := range r.sessions {
		if entry == nil || entry.send == nil {
			continue
		}
		total += len(entry.send)
	}
	return total
}

func (r *sessionRegistry) queuedMessagesByPriority() map[string]int {
	r.registrationMu.RLock()
	defer r.registrationMu.RUnlock()
	r.mu.RLock()
	defer r.mu.RUnlock()
	total := map[string]int{"critical": 0, "high": 0, "normal": 0, "low": 0}
	for _, entry := range r.sessions {
		if entry == nil {
			continue
		}
		entry.mu.Lock()
		for priority, depth := range entry.priorityDepth {
			total[priority] += depth
		}
		entry.mu.Unlock()
	}
	return total
}

func (r *sessionRegistry) snapshot() []sessionSnapshot {
	r.registrationMu.RLock()
	defer r.registrationMu.RUnlock()
	r.mu.RLock()
	snapshots := make([]sessionSnapshot, 0, len(r.sessions))
	for sessionID, entry := range r.sessions {
		if entry == nil || entry.conn == nil || !entry.isSessionOwnerActive() {
			continue
		}
		snapshots = append(snapshots, sessionSnapshot{
			SessionID:  sessionID,
			RemoteIP:   entry.remoteIP,
			RemotePort: entry.remotePort,
		})
	}
	r.mu.RUnlock()
	return snapshots
}
