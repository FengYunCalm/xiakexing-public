package main

import (
	"fmt"
	"unicode/utf8"
)

type tcpToWebSocketTextAdapter struct {
	pending           []byte
	iacPending        bool
	awaitingOption    bool
	inSubnegotiation  bool
	subnegotiationIAC bool
	xkPrefix          []byte
	inXKFrame         bool
	xkFrameEscape     bool
	utf8Pending       []byte
}

func newTCPToWebSocketTextAdapter() *tcpToWebSocketTextAdapter {
	return &tcpToWebSocketTextAdapter{pending: []byte{}}
}

func (a *tcpToWebSocketTextAdapter) flushXKPrefix(textBytes *[]byte) {
	if len(a.xkPrefix) == 0 {
		return
	}
	*textBytes = append(*textBytes, a.xkPrefix...)
	a.xkPrefix = a.xkPrefix[:0]
}

func (a *tcpToWebSocketTextAdapter) feed(data []byte, suppressXK bool) string {
	textBytes := make([]byte, 0, len(data))
	for _, b := range data {
		if suppressXK {
			if a.inXKFrame {
				if a.xkFrameEscape {
					a.xkFrameEscape = false
					if b == '\n' {
						a.inXKFrame = false
					}
					continue
				}
				if b == 0x1b {
					a.xkFrameEscape = true
				}
				continue
			}
			if len(a.xkPrefix) > 0 {
				a.xkPrefix = append(a.xkPrefix, b)
				switch len(a.xkPrefix) {
				case 1:
					if b != 0x1b {
						a.flushXKPrefix(&textBytes)
					}
				case 2:
					if b != 'X' {
						a.flushXKPrefix(&textBytes)
					}
				case 3:
					if b == 'K' {
						a.inXKFrame = true
						a.xkPrefix = a.xkPrefix[:0]
						continue
					}
					a.flushXKPrefix(&textBytes)
				}
				continue
			}
			if b == 0x1b {
				a.xkPrefix = append(a.xkPrefix, b)
				continue
			}
		}
		if a.inSubnegotiation {
			if a.subnegotiationIAC {
				a.subnegotiationIAC = false
				if b == 0xF0 {
					a.inSubnegotiation = false
				}
			} else if b == 0xFF {
				a.subnegotiationIAC = true
			}
			continue
		}
		if a.awaitingOption {
			a.awaitingOption = false
			continue
		}
		if a.iacPending {
			a.iacPending = false
			switch b {
			case 0xFD, 0xFE, 0xFB, 0xFC:
				a.awaitingOption = true
			case 0xFA:
				a.inSubnegotiation = true
			}
			continue
		}
		if b == 0xFF {
			a.iacPending = true
			continue
		}
		textBytes = append(textBytes, b)
	}
	if suppressXK && !a.inXKFrame {
		a.flushXKPrefix(&textBytes)
	}
	if len(textBytes) == 0 {
		return ""
	}
	a.pending = append(a.pending, textBytes...)
	result := string(a.pending)
	a.pending = a.pending[:0]
	return result
}

// feedUTF8 is the direct-proxy boundary: it never emits an incomplete UTF-8
// code point and treats a complete invalid byte sequence as a transport error.
func (a *tcpToWebSocketTextAdapter) feedUTF8(data []byte, suppressXK bool) (string, error) {
	raw := []byte(a.feed(data, suppressXK))
	input := make([]byte, 0, len(a.utf8Pending)+len(raw))
	input = append(input, a.utf8Pending...)
	input = append(input, raw...)
	a.utf8Pending = nil
	valid := make([]byte, 0, len(input))
	for len(input) > 0 {
		runeValue, size := utf8.DecodeRune(input)
		if runeValue == utf8.RuneError && size == 1 {
			if !utf8.FullRune(input) {
				a.utf8Pending = append([]byte(nil), input...)
				break
			}
			return string(valid), fmt.Errorf("invalid UTF-8 sequence from direct driver")
		}
		valid = append(valid, input[:size]...)
		input = input[size:]
	}
	return string(valid), nil
}

func (a *tcpToWebSocketTextAdapter) finishUTF8() error {
	if len(a.utf8Pending) != 0 {
		return fmt.Errorf("truncated UTF-8 sequence at direct driver EOF")
	}
	return nil
}
