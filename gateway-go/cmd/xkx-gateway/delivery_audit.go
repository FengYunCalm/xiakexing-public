package main

import (
	"strconv"
	"sync"
)

const (
	deliveryAuditHeader    = "X-XKX-Delivery-Audit-Slot"
	deliveryAuditSlotCount = 300
)

// deliveryAudit is an opt-in, fixed-size diagnostic. It deliberately stores no
// session identity, account, command, or payload data.
type deliveryAudit struct {
	mu    sync.Mutex
	slots []*deliveryAuditSlot
}

type deliveryAuditSlot struct {
	mu sync.Mutex

	claimed             bool
	driverOutputFrames  uint64
	enqueuedFrames      uint64
	writtenFrames       uint64
	writeErrors         uint64
	expiredDrops        uint64
	queueRejectedFrames uint64
	collapsedFrames     uint64
}

type deliveryAuditSnapshot struct {
	Slot                int    `json:"slot"`
	DriverOutputFrames  uint64 `json:"driver_output_frames"`
	EnqueuedFrames      uint64 `json:"enqueued_frames"`
	WrittenFrames       uint64 `json:"written_frames"`
	WriteErrors         uint64 `json:"write_errors"`
	ExpiredDrops        uint64 `json:"expired_drops"`
	QueueRejectedFrames uint64 `json:"queue_rejected_frames"`
	CollapsedFrames     uint64 `json:"collapsed_frames"`
}

func newDeliveryAudit(slotCount int) *deliveryAudit {
	if slotCount <= 0 {
		return nil
	}
	return &deliveryAudit{slots: make([]*deliveryAuditSlot, slotCount)}
}

func (a *deliveryAudit) claim(rawSlot string) *deliveryAuditSlot {
	if a == nil {
		return nil
	}
	slot, err := strconv.Atoi(rawSlot)
	if err != nil || slot < 1 || slot > len(a.slots) {
		return nil
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	index := slot - 1
	if a.slots[index] != nil {
		return nil
	}
	entry := &deliveryAuditSlot{claimed: true}
	a.slots[index] = entry
	return entry
}

func (a *deliveryAudit) snapshot(slot int) (deliveryAuditSnapshot, bool) {
	if a == nil || slot < 1 || slot > len(a.slots) {
		return deliveryAuditSnapshot{}, false
	}
	a.mu.Lock()
	entry := a.slots[slot-1]
	a.mu.Unlock()
	if entry == nil {
		return deliveryAuditSnapshot{}, false
	}
	entry.mu.Lock()
	defer entry.mu.Unlock()
	return deliveryAuditSnapshot{
		Slot:                slot,
		DriverOutputFrames:  entry.driverOutputFrames,
		EnqueuedFrames:      entry.enqueuedFrames,
		WrittenFrames:       entry.writtenFrames,
		WriteErrors:         entry.writeErrors,
		ExpiredDrops:        entry.expiredDrops,
		QueueRejectedFrames: entry.queueRejectedFrames,
		CollapsedFrames:     entry.collapsedFrames,
	}, true
}

func (s *deliveryAuditSlot) recordDriverOutput() { s.record(func() { s.driverOutputFrames++ }) }
func (s *deliveryAuditSlot) recordEnqueued()     { s.record(func() { s.enqueuedFrames++ }) }
func (s *deliveryAuditSlot) recordWritten()      { s.record(func() { s.writtenFrames++ }) }
func (s *deliveryAuditSlot) recordWriteError()   { s.record(func() { s.writeErrors++ }) }
func (s *deliveryAuditSlot) recordExpiredDrop()  { s.record(func() { s.expiredDrops++ }) }
func (s *deliveryAuditSlot) recordCollapsed()    { s.record(func() { s.collapsedFrames++ }) }
func (s *deliveryAuditSlot) recordQueueRejected() {
	s.record(func() { s.queueRejectedFrames++ })
}

func (s *deliveryAuditSlot) record(update func()) {
	if s == nil || update == nil {
		return
	}
	s.mu.Lock()
	update()
	s.mu.Unlock()
}
