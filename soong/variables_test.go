package twrp

import "testing"

func TestMakeVarValue(t *testing.T) {
	for _, test := range []struct{ input, want string }{
		{"", ""}, {"  en  ", "en"}, {`"en"`, "en"},
		{` "left" `, "left"}, {`""`, ""},
		{`a"b`, `a"b`}, {`"a\"b"`, `a\"b`},
	} {
		if got := makeVarValue(test.input); got != test.want {
			t.Errorf("makeVarValue(%q) = %q, want %q", test.input, got, test.want)
		}
	}
}

func TestMakeCStringLiteral(t *testing.T) {
	for _, test := range []struct{ input, want string }{
		{"", ""}, {"  ", ""}, {`""`, ""},
		{"/sys/class/leds/brightness", `"/sys/class/leds/brightness"`},
		{` "/sdcard" `, `"/sdcard"`},
		{`"first\nsecond"`, `"first\nsecond"`},
		{`"first\x0atest2"`, `"first\x0atest2"`},
		{`"a\"b"`, `"a\"b"`},
		{`"a" "b"`, `"a" "b"`},
		{`a"b`, `"a\"b"`},
		{`a\b`, `"a\\b"`},
		{"one.ko two.ko;third.ko", `"one.ko two.ko;third.ko"`},
		{"first\nsecond", `"first\012second"`},
		{"\x012", `"\0012"`},
		{"\x00\x7f", `"\000\177"`},
		{"恢复", `"恢复"`},
	} {
		if got := makeCStringLiteral(test.input); got != test.want {
			t.Errorf("makeCStringLiteral(%q) = %q, want %q", test.input, got, test.want)
		}
	}
}

func TestMakeIntLiteral(t *testing.T) {
	for _, test := range []struct{ input, want string }{
		{"", ""}, {`""`, ""}, {"0", "0"}, {` "116" `, "116"},
		{"004", "4"}, {"2147483647", "2147483647"},
	} {
		got, err := makeIntLiteral(test.input)
		if err != nil || got != test.want {
			t.Errorf("makeIntLiteral(%q) = %q, %v; want %q", test.input, got, err, test.want)
		}
	}
	for _, input := range []string{"-1", "true", "1;2", "1+2", "2147483648"} {
		if _, err := makeIntLiteral(input); err == nil {
			t.Errorf("makeIntLiteral(%q) should fail", input)
		}
	}
}

func TestScalarAndBoolValues(t *testing.T) {
	for _, value := range []string{"true", `"true"`, ` " true " `} {
		if !makeBoolValue(value) {
			t.Errorf("%q must enable a bool switch", value)
		}
	}
	for _, value := range []string{"", "false", `"false"`, "1", "TRUE"} {
		if makeBoolValue(value) {
			t.Errorf("%q must not enable a bool switch", value)
		}
	}
	if makeVarScalar(`" 330 "`) != "330" {
		t.Fatal("scalar whitespace normalization")
	}
	if makeVarValue(`" /path "`) != " /path " {
		t.Fatal("do not trim the contents of a quoted path")
	}
}

func TestWideIntegerLiterals(t *testing.T) {
	for _, test := range []struct{ input, want string }{
		{"", ""}, {"0", "0ULL"}, {`"1642433544000"`, "1642433544000ULL"},
		{"18446744073709551615", "18446744073709551615ULL"},
	} {
		got, err := makeUint64Literal(test.input)
		if err != nil || got != test.want {
			t.Errorf("unsigned %q = %q, %v", test.input, got, err)
		}
	}
	for _, test := range []struct{ input, want string }{
		{"", ""}, {"0", "0LL"}, {`"-3600"`, "-3600LL"},
		{"9223372036854775807", "9223372036854775807LL"},
		{"-9223372036854775808", "(-9223372036854775807LL - 1)"},
	} {
		got, err := makeInt64Literal(test.input)
		if err != nil || got != test.want {
			t.Errorf("signed %q = %q, %v", test.input, got, err)
		}
	}
	for _, input := range []string{"-1", "18446744073709551616", "bad"} {
		if _, err := makeUint64Literal(input); err == nil {
			t.Errorf("invalid unsigned input %q", input)
		}
	}
	for _, input := range []string{"9223372036854775808", "-9223372036854775809", "bad"} {
		if _, err := makeInt64Literal(input); err == nil {
			t.Errorf("invalid signed input %q", input)
		}
	}
}
