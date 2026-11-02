package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"strings"
	"unicode/utf8"
)

const (
	commandInputSchema      = "xk.input.v1"
	maxCommandPayloadBytes  = 512
	maxCommandEnvelopeBytes = 2048
)

type commandEnvelope struct {
	Schema           string
	CommandRequestID string
	Payload          string
}

// decodeCommandEnvelope is the only decoder for the authenticated typed input
// contract.  It deliberately uses token-level decoding so duplicate JSON keys
// cannot silently overwrite the first value.
func decodeCommandEnvelope(raw []byte) (commandEnvelope, error) {
	var envelope commandEnvelope
	if len(raw) > maxCommandEnvelopeBytes {
		return envelope, fmt.Errorf("command envelope exceeds %d bytes", maxCommandEnvelopeBytes)
	}
	if !utf8.Valid(raw) {
		return envelope, fmt.Errorf("command envelope is not valid UTF-8")
	}

	decoder := json.NewDecoder(bytes.NewReader(raw))
	first, err := decoder.Token()
	if err != nil {
		return envelope, fmt.Errorf("decode command envelope: %w", err)
	}
	delimiter, ok := first.(json.Delim)
	if !ok || delimiter != '{' {
		return envelope, fmt.Errorf("command envelope must be a JSON object")
	}

	seen := make(map[string]bool, 3)
	for decoder.More() {
		keyToken, err := decoder.Token()
		if err != nil {
			return envelope, fmt.Errorf("decode command envelope key: %w", err)
		}
		key, ok := keyToken.(string)
		if !ok {
			return envelope, fmt.Errorf("command envelope key is not a string")
		}
		if seen[key] {
			return envelope, fmt.Errorf("duplicate command envelope field %q", key)
		}
		seen[key] = true

		var value json.RawMessage
		if err := decoder.Decode(&value); err != nil {
			return envelope, fmt.Errorf("decode command envelope field %q: %w", key, err)
		}
		switch key {
		case "schema":
			decoded, err := decodeStrictJSONString(value)
			if err != nil {
				return envelope, fmt.Errorf("command envelope schema must be a valid string: %w", err)
			}
			envelope.Schema = decoded
		case "command_request_id":
			decoded, err := decodeStrictJSONString(value)
			if err != nil {
				return envelope, fmt.Errorf("command envelope command_request_id must be a valid string: %w", err)
			}
			envelope.CommandRequestID = decoded
		case "payload":
			decoded, err := decodeStrictJSONString(value)
			if err != nil {
				return envelope, fmt.Errorf("command envelope payload must be a valid string: %w", err)
			}
			envelope.Payload = decoded
		default:
			return envelope, fmt.Errorf("unknown command envelope field %q", key)
		}
	}

	last, err := decoder.Token()
	if err != nil {
		return envelope, fmt.Errorf("decode command envelope end: %w", err)
	}
	end, ok := last.(json.Delim)
	if !ok || end != '}' {
		return envelope, fmt.Errorf("command envelope object is not closed")
	}
	var trailing any
	if err := decoder.Decode(&trailing); err != io.EOF {
		if err == nil {
			return envelope, fmt.Errorf("trailing command envelope data")
		}
		return envelope, fmt.Errorf("trailing command envelope data: %w", err)
	}

	if !seen["schema"] || !seen["command_request_id"] || !seen["payload"] {
		return envelope, fmt.Errorf("command envelope requires schema, command_request_id, and payload")
	}
	if envelope.Schema != commandInputSchema {
		return envelope, fmt.Errorf("unsupported command envelope schema %q", envelope.Schema)
	}
	if err := validateCommandRequestID(envelope.CommandRequestID); err != nil {
		return envelope, err
	}
	if err := validateCommandPayload(envelope.Payload); err != nil {
		return envelope, err
	}
	if isCommandEnvelopeCandidate([]byte(strings.TrimSpace(envelope.Payload))) {
		return envelope, fmt.Errorf("nested command envelope is not allowed")
	}
	return envelope, nil
}

func validateCommandRequestID(value string) error {
	if len(value) < 1 || len(value) > 96 {
		return fmt.Errorf("command_request_id length must be 1..96 bytes")
	}
	for index := 0; index < len(value); index++ {
		if value[index] < 0x21 || value[index] > 0x7e {
			return fmt.Errorf("command_request_id must contain visible ASCII without whitespace")
		}
	}
	return nil
}

func validateCommandPayload(value string) error {
	if value == "" {
		return fmt.Errorf("command envelope payload must not be empty")
	}
	if len([]byte(value)) > maxCommandPayloadBytes {
		return fmt.Errorf("command envelope payload exceeds %d UTF-8 bytes", maxCommandPayloadBytes)
	}
	if strings.IndexByte(value, 0) >= 0 {
		return fmt.Errorf("command envelope payload contains NUL")
	}
	if strings.ContainsAny(value, "\r\n") {
		return fmt.Errorf("command envelope payload must be one line")
	}
	return nil
}

func isCommandEnvelopeCandidate(raw []byte) bool {
	trimmed := bytes.TrimSpace(raw)
	return len(trimmed) > 0 && trimmed[0] == '{'
}

func decodeStrictJSONString(raw json.RawMessage) (string, error) {
	if len(raw) < 2 || raw[0] != '"' || raw[len(raw)-1] != '"' {
		return "", fmt.Errorf("value is not a JSON string")
	}

	var decoded strings.Builder
	for index := 1; index < len(raw)-1; {
		value := raw[index]
		switch {
		case value == '\\':
			if index+1 >= len(raw)-1 {
				return "", fmt.Errorf("incomplete escape")
			}
			escape := raw[index+1]
			switch escape {
			case '"', '\\', '/', 'b', 'f', 'n', 'r', 't':
				switch escape {
				case '"':
					decoded.WriteByte('"')
				case '\\':
					decoded.WriteByte('\\')
				case '/':
					decoded.WriteByte('/')
				case 'b':
					decoded.WriteByte('\b')
				case 'f':
					decoded.WriteByte('\f')
				case 'n':
					decoded.WriteByte('\n')
				case 'r':
					decoded.WriteByte('\r')
				case 't':
					decoded.WriteByte('\t')
				}
				index += 2
			case 'u':
				if index+6 >= len(raw) {
					return "", fmt.Errorf("incomplete unicode escape")
				}
				codeUnit, ok := parseJSONHex4(raw[index+2 : index+6])
				if !ok {
					return "", fmt.Errorf("invalid unicode escape")
				}
				index += 6
				if codeUnit >= 0xd800 && codeUnit <= 0xdbff {
					if index+6 >= len(raw) || raw[index] != '\\' || raw[index+1] != 'u' {
						return "", fmt.Errorf("high surrogate is not followed by a low surrogate")
					}
					low, ok := parseJSONHex4(raw[index+2 : index+6])
					if !ok || low < 0xdc00 || low > 0xdfff {
						return "", fmt.Errorf("invalid low surrogate")
					}
					codePoint := 0x10000 + ((int(codeUnit) - 0xd800) << 10) + (int(low) - 0xdc00)
					var encoded [utf8.UTFMax]byte
					n := utf8.EncodeRune(encoded[:], rune(codePoint))
					decoded.Write(encoded[:n])
					index += 6
				} else if codeUnit >= 0xdc00 && codeUnit <= 0xdfff {
					return "", fmt.Errorf("low surrogate is not paired")
				} else {
					decoded.WriteRune(rune(codeUnit))
				}
			default:
				return "", fmt.Errorf("invalid escape")
			}
		case value < 0x20:
			return "", fmt.Errorf("unescaped control character")
		case value == '"':
			return "", fmt.Errorf("unexpected quote")
		default:
			runeValue, size := utf8.DecodeRune(raw[index : len(raw)-1])
			if runeValue == utf8.RuneError && size == 1 {
				return "", fmt.Errorf("invalid UTF-8 sequence")
			}
			decoded.WriteRune(runeValue)
			index += size
		}
	}
	return decoded.String(), nil
}

func parseJSONHex4(raw []byte) (uint16, bool) {
	if len(raw) != 4 {
		return 0, false
	}
	var value uint16
	for _, digit := range raw {
		value <<= 4
		switch {
		case digit >= '0' && digit <= '9':
			value += uint16(digit - '0')
		case digit >= 'a' && digit <= 'f':
			value += uint16(digit-'a') + 10
		case digit >= 'A' && digit <= 'F':
			value += uint16(digit-'A') + 10
		default:
			return 0, false
		}
	}
	return value, true
}

func commandEnvelopeDriverData(envelope commandEnvelope) map[string]any {
	return map[string]any{
		"schema":             envelope.Schema,
		"command_request_id": envelope.CommandRequestID,
		"payload":            envelope.Payload,
	}
}

func typedCommandTransportPayload(raw []byte) ([]byte, bool, error) {
	_, err := decodeCommandEnvelope(raw)
	if err == nil {
		payload := append([]byte(nil), raw...)
		if len(payload) == 0 || payload[len(payload)-1] != '\n' {
			payload = append(payload, '\n')
		}
		return payload, true, nil
	}
	if isCommandEnvelopeCandidate(raw) {
		return nil, true, err
	}
	return raw, false, nil
}
