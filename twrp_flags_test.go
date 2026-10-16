package twrp

import (
	"encoding/hex"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func flagsForTest(t *testing.T, vars map[string]string) []string {
	t.Helper()
	flags, err := twrpFlags(func(name string) string { return vars[name] })
	if err != nil {
		t.Fatal(err)
	}
	return flags
}

func TestTwrpFlagsDefaults(t *testing.T) {
	want := []string{
		`-DTW_DEFAULT_LANGUAGE="en"`,
		`-DTW_DEVICE_VERSION="-""0"`,
		`-DTW_GIT_REVISION="unknown"`,
	}
	if got := flagsForTest(t, nil); !reflect.DeepEqual(got, want) {
		t.Fatalf("flags = %q, want %q", got, want)
	}
}

func TestTwrpStringFlags(t *testing.T) {
	for _, variable := range twrpStringVariables {
		t.Run(variable.variable, func(t *testing.T) {
			for _, value := range []string{"one;two three", `"one;two three"`} {
				flags := flagsForTest(t, map[string]string{variable.variable: value})
				want := "-D" + variable.macro + `="one;two three"`
				if flags[0] != want {
					t.Fatalf("flags = %q, want %q", flags, want)
				}
			}
			for _, value := range []string{"", " ", `""`} {
				flags := flagsForTest(t, map[string]string{variable.variable: value})
				for _, flag := range flags {
					if strings.HasPrefix(flag, "-D"+variable.macro+"=") {
						t.Fatalf("empty value unexpectedly produced %q", flag)
					}
				}
			}
		})
	}
}

func TestTwrpBooleanFlags(t *testing.T) {
	for _, enabled := range []string{"true", `"true"`, "false", `"false"`, ""} {
		vars := map[string]string{"TW_LOAD_VENDOR_MODULES": "one.ko"}
		for _, name := range []string{"TW_LOAD_VENDOR_MODULES_EXCLUDE_GKI", "TW_LOAD_VENDOR_BOOT_MODULES", "TW_LOAD_PREBUILT_MODULES_AT_FIRST"} {
			vars[name] = enabled
		}
		flags := strings.Join(flagsForTest(t, vars), "\n") + "\n"
		for name := range vars {
			if name == "TW_LOAD_VENDOR_MODULES" {
				continue
			}
			if got := strings.Contains(flags, "-D"+name+"\n"); got != makeBoolValue(enabled) {
				t.Errorf("%s=%q: flags %q", name, enabled, flags)
			}
		}
		delete(vars, "TW_LOAD_VENDOR_MODULES")
		if len(flagsForTest(t, vars)) != 3 {
			t.Fatal("module switches must not enable the loader without a module list")
		}
	}
}

func TestTwrpNumericFlags(t *testing.T) {
	flags := flagsForTest(t, map[string]string{"TW_CUSTOM_POWER_BUTTON": `"116"`, "BOARD_BOOT_HEADER_VERSION": "004"})
	if !reflect.DeepEqual(flags[len(flags)-2:], []string{"-DTW_CUSTOM_POWER_BUTTON=116", "-DBOARD_BOOT_HEADER_VERSION=4"}) {
		t.Fatal(flags)
	}
	for _, name := range []string{"TW_CUSTOM_POWER_BUTTON", "BOARD_BOOT_HEADER_VERSION"} {
		_, err := twrpFlags(func(key string) string {
			if key == name {
				return "invalid"
			}
			return ""
		})
		if err == nil || !strings.Contains(err.Error(), name) {
			t.Fatalf("expected an error naming %s, got %v", name, err)
		}
	}
}

// Verify generated macro values with a real C++ compiler.
func TestTwrpFlagsCompiler(t *testing.T) {
	compiler, err := exec.LookPath("c++")
	if err != nil {
		t.Skip("host C++ compiler not installed")
	}
	for _, quoted := range []bool{false, true} {
		t.Run(fmt.Sprintf("quoted=%t", quoted), func(t *testing.T) {
			vars := map[string]string{}
			want := map[string]string{}
			for _, variable := range twrpStringVariables {
				value := "one;two three"
				vars[variable.variable] = value
				if quoted {
					vars[variable.variable] = `"` + value + `"`
				}
				want[variable.macro] = value
			}
			// C escapes must not be double-escaped.
			vars["TW_INPUT_BLACKLIST"] = `"accelerometer\x0atest2\nname\"quoted"`
			want["TW_INPUT_BLACKLIST"] = "accelerometer\ntest2\nname\"quoted"
			vars["TW_CUSTOM_BATTERY_PATH"] = "恢复\\battery\x012"
			want["TW_CUSTOM_BATTERY_PATH"] = "恢复\\battery\x012"
			vars["TW_CUSTOM_POWER_BUTTON"] = `"116"`
			vars["BOARD_BOOT_HEADER_VERSION"] = "004"
			vars["TW_USE_KEY_CODE_TOUCH_SYNC"] = `"330"`
			vars["TW_QCOM_ATS_OFFSET"] = `"1642433544000"`
			vars["TW_CLOCK_OFFSET"] = `"-3600"`
			vars["TW_LOAD_VENDOR_BOOT_MODULES"] = "false"
			vars["TW_LOAD_PREBUILT_MODULES_AT_FIRST"] = "true"
			vars["TW_DEVICE_VERSION"] = `"device\"version"`
			want["TW_DEVICE_VERSION"] = "-device\"version"
			want["TW_DEFAULT_LANGUAGE"] = "en"
			want["TW_GIT_REVISION"] = "unknown"

			var source strings.Builder
			source.WriteString(`#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstddef>
static_assert(TW_CUSTOM_POWER_BUTTON == 116);
static_assert(BOARD_BOOT_HEADER_VERSION == 4);
static_assert(TW_USE_KEY_CODE_TOUCH_SYNC == 330);
static_assert(TW_QCOM_ATS_OFFSET == 1642433544000ULL);
static_assert(TW_CLOCK_OFFSET == -3600LL);
#ifdef TW_LOAD_VENDOR_BOOT_MODULES
#error false must not enable TW_LOAD_VENDOR_BOOT_MODULES
#endif
#ifndef TW_LOAD_PREBUILT_MODULES_AT_FIRST
#error true must enable TW_LOAD_PREBUILT_MODULES_AT_FIRST
#endif
void dump(const char* name, const char* value, std::size_t size) {
    std::cout << name << '=';
    for (std::size_t i = 0; i < size; ++i)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(static_cast<unsigned char>(value[i]));
    std::cout << '\n';
}
int main() {
    std::stringstream properties(TW_OVERRIDE_SYSTEM_PROPS);
`)
			for name := range want {
				fmt.Fprintf(&source, "dump(\"%s\", %s, sizeof(%s) - 1);\n", name, name, name)
			}
			source.WriteString("}\n")
			dir := t.TempDir()
			input, output := filepath.Join(dir, "probe.cpp"), filepath.Join(dir, "probe")
			if err := os.WriteFile(input, []byte(source.String()), 0600); err != nil {
				t.Fatal(err)
			}
			args := append([]string{"-std=c++17", "-Wall", "-Werror"}, flagsForTest(t, vars)...)
			args = append(args, input, "-o", output)
			if log, err := exec.Command(compiler, args...).CombinedOutput(); err != nil {
				t.Fatalf("compile: %v\n%s", err, log)
			}
			log, err := exec.Command(output).Output()
			if err != nil {
				t.Fatal(err)
			}
			for _, line := range strings.Split(strings.TrimSpace(string(log)), "\n") {
				pair := strings.SplitN(line, "=", 2)
				if len(pair) != 2 {
					t.Fatalf("invalid probe output: %q", line)
				}
				value, err := hex.DecodeString(pair[1])
				if err != nil {
					t.Fatal(err)
				}
				if string(value) != want[pair[0]] {
					t.Errorf("%s = %q, want %q", pair[0], value, want[pair[0]])
				}
				delete(want, pair[0])
			}
			if len(want) != 0 {
				t.Fatalf("missing probe results: %v", want)
			}
		})
	}
}

func TestTwrpFscryptPolicyFlags(t *testing.T) {
	for _, test := range []struct {
		fbe, policy string
		want        bool
	}{
		{"true", "1", true}, {"true", `"1"`, true},
		{"true", "2", false}, {"true", "", false},
		{"false", "1", false}, {"", "1", false},
	} {
		flags := flagsForTest(t, map[string]string{"TW_INCLUDE_CRYPTO_FBE": test.fbe, "TW_USE_FSCRYPT_POLICY": test.policy})
		got := strings.Contains(strings.Join(flags, "\n"), "-DUSE_FSCRYPT_POLICY_V1")
		if got != test.want {
			t.Errorf("FBE=%q policy=%q: flags %q", test.fbe, test.policy, flags)
		}
	}
}

func TestOptionalNumericFlags(t *testing.T) {
	vars := map[string]string{"TW_USE_KEY_CODE_TOUCH_SYNC": "330", "TW_QCOM_ATS_OFFSET": "1642433544000", "TW_CLOCK_OFFSET": "-3600"}
	flags := flagsForTest(t, vars)
	for _, expected := range []string{"-DTW_USE_KEY_CODE_TOUCH_SYNC=330", "-DTW_QCOM_ATS_OFFSET=1642433544000ULL", "-DTW_CLOCK_OFFSET=-3600LL"} {
		if !strings.Contains(strings.Join(flags, "\n"), expected) {
			t.Fatal(flags)
		}
	}
	for name := range vars {
		_, err := twrpFlags(func(key string) string {
			if key == name {
				return "bad"
			}
			return ""
		})
		if err == nil || !strings.Contains(err.Error(), name) {
			t.Errorf("missing error for %s: %v", name, err)
		}
	}
}
