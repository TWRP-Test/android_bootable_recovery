package twrp

import (
	"fmt"
	"strconv"
	"strings"
)

// Remove only a matching pair of outer quotes.
func makeVarValue(value string) string {
	value = strings.TrimSpace(value)
	if len(value) >= 2 && value[0] == '"' && value[len(value)-1] == '"' {
		return value[1 : len(value)-1]
	}
	return value
}

func makeVarScalar(value string) string {
	return strings.TrimSpace(makeVarValue(value))
}

func makeBoolValue(value string) bool {
	return makeVarScalar(value) == "true"
}

// Preserve C escapes in existing literals; quote plain text only.
func makeCStringLiteral(value string) string {
	value = strings.TrimSpace(value)
	if value == "" || value == `""` {
		return ""
	}
	if len(value) >= 2 && value[0] == '"' && value[len(value)-1] == '"' {
		return value
	}

	var literal strings.Builder
	literal.WriteByte('"')
	for i := 0; i < len(value); i++ {
		b := value[i]
		switch {
		case b == '"' || b == '\\':
			literal.WriteByte('\\')
			literal.WriteByte(b)
		case b < 0x20 || b == 0x7f:
			// Three octal digits cannot consume a following digit, unlike \xNN.
			fmt.Fprintf(&literal, "\\%03o", b)
		default:
			literal.WriteByte(b)
		}
	}
	literal.WriteByte('"')
	return literal.String()
}

func makeIntLiteral(value string) (string, error) {
	value = makeVarScalar(value)
	if value == "" {
		return "", nil
	}
	number, err := strconv.ParseInt(value, 10, 32)
	if err != nil || number < 0 {
		return "", fmt.Errorf("expected a non-negative decimal integer, got %q", value)
	}
	return strconv.FormatInt(number, 10), nil
}

func makeUint64Literal(value string) (string, error) {
	value = makeVarScalar(value)
	if value == "" {
		return "", nil
	}
	number, err := strconv.ParseUint(value, 10, 64)
	if err != nil {
		return "", fmt.Errorf("expected an unsigned 64-bit decimal integer, got %q", value)
	}
	return strconv.FormatUint(number, 10) + "ULL", nil
}

func makeInt64Literal(value string) (string, error) {
	value = makeVarScalar(value)
	if value == "" {
		return "", nil
	}
	number, err := strconv.ParseInt(value, 10, 64)
	if err != nil {
		return "", fmt.Errorf("expected a signed 64-bit decimal integer, got %q", value)
	}
	// Avoid an out-of-range positive signed literal.
	if number == -1<<63 {
		return "(-9223372036854775807LL - 1)", nil
	}
	return strconv.FormatInt(number, 10) + "LL", nil
}
