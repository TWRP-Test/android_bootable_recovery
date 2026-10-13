package twrp

import (
	"strings"
	"testing"

	"android/soong/android"
	"android/soong/cc"
	"github.com/google/blueprint/proptools"
)

func TestSharedTwrpDefaults(t *testing.T) {
	vars := map[string]string{
		"TW_OVERRIDE_SYSTEM_PROPS": `"ro.build.version.security_patch;ro.vendor.build.security_patch"`,
		"TW_INPUT_BLACKLIST":       `"accelerometer\ntest2"`,
		"TW_LOAD_VENDOR_MODULES":   "one.ko two.ko",
		"TW_INTERNAL_STORAGE_PATH": `"/sdcard"`,
		"TW_OZIP_DECRYPT_KEY":      `"a\"b"`,
	}
	result := android.GroupFixturePreparers(
		cc.PrepareForTestWithCcDefaultModules,
		android.FixtureRegisterWithContext(func(ctx android.RegistrationContext) {
			ctx.RegisterModuleType("twrp_cflags_defaults", twrpCflagsDefaultsFactory)
			ctx.RegisterModuleType("twrp_recovery_defaults", twrpRecoveryDefaultsFactory)
		}),
		android.FixtureModifyProductVariables(func(variables android.FixtureProductVariables) {
			variables.VendorVars = map[string]map[string]string{"twrpVarsPlugin": vars}
		}),
		android.FixtureAddTextFile("probe.cpp", "int probe;"),
	).RunTestWithBp(t, `
		twrp_cflags_defaults { name: "flags" }
		cc_defaults { name: "nested", defaults: ["flags"] }
		cc_library_static { name: "gui", defaults: ["flags"], srcs: ["probe.cpp"] }
		cc_library_static { name: "minui", defaults: ["nested"], srcs: ["probe.cpp"] }
		cc_library_static { name: "mtp", defaults: ["flags"], srcs: ["probe.cpp"] }
	`)
	flags, err := twrpFlags(func(name string) string { return vars[name] })
	if err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"gui", "minui", "mtp"} {
		cflags := result.ModuleForTests(t, name, "android_arm64_armv8-a_static").Rule("cc").Args["cFlags"]
		for _, flag := range flags {
			if escaped := proptools.NinjaAndShellEscape(flag); !strings.Contains(cflags, escaped) {
				t.Errorf("%s missing escaped flag %q in %q", name, escaped, cflags)
			}
		}
	}
}
