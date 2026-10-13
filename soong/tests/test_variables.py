#!/usr/bin/env python3
"""Run host regressions with: python3 soong/tests/test_variables.py."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

RECOVERY = Path(__file__).resolve().parents[2]
VENDOR_CANDIDATES = [RECOVERY.parent / "android_vendor_twrp", RECOVERY.parents[1] / "vendor/twrp"]
VENDOR = Path(os.environ["TWRP_VENDOR_DIR"]) if "TWRP_VENDOR_DIR" in os.environ else next(
    (candidate for candidate in VENDOR_CANDIDATES if (candidate / "config/BoardConfigSoong.mk").is_file()),
    VENDOR_CANDIDATES[0],
)


def exports():
    config = (VENDOR / "config/BoardConfigSoong.mk").read_text()
    return set(config.split("EXPORT_TO_SOONG :=", 1)[1].split("\n\n", 1)[0].replace("\\", "").split())


def make_exports(directory, settings):
    result = directory / "exports.txt"
    makefile = directory / "fixture.mk"
    (directory / "bootable").mkdir()
    (directory / "bootable/recovery").symlink_to(RECOVERY, target_is_directory=True)
    makefile.write_text(
        """define soong_config_set
$(eval SOONG_CONFIG_$(strip $1) += $(strip $2))
$(eval SOONG_CONFIG_$(strip $1)_$(strip $2) := $(strip $3))
endef
soong_config_set_bool = $(call soong_config_set,$1,$2,$(filter true,$3))
soong_config_set_int = $(call soong_config_set,$1,$2,$3)
soong_config_set_string_list = $(call soong_config_set,$1,$2,$3)
"""
        + "\n".join(f"{name} := {value}" for name, value in settings.items())
        + f"\ninclude {VENDOR / 'config/BoardConfigSoong.mk'}\n"
        + ".PHONY: export\nexport:\n"
        + f"\t$(foreach v,$(EXPORT_TO_SOONG),$(file >>{result},$(v)\t$(SOONG_CONFIG_twrpVarsPlugin_$(v))))\n"
        + f"\t$(file >{directory / 'vold-policy.txt'},$(SOONG_CONFIG_makeVarsPlugin_TW_USE_FSCRYPT_POLICY))\n"
        + f"\t$(foreach v,$(sort $(SOONG_CONFIG_twrpGlobalVars)),$(file >>{directory / 'global-vars.txt'},$(v)\t$(SOONG_CONFIG_twrpGlobalVars_$(v))))\n"
        + "\t@:\n"
    )
    subprocess.run(["make", "--no-print-directory", "-f", str(makefile), "export"], cwd=directory, check=True, capture_output=True, text=True)
    return dict(line.split("\t", 1) for line in result.read_text().splitlines())


class VariablesTest(unittest.TestCase):
    def test_make_snapshot(self):
        settings = {
            "TW_INCLUDE_CRYPTO": "true",
            "TW_OVERRIDE_SYSTEM_PROPS": '"ro.build.version.security_patch;ro.vendor.build.security_patch"',
            "TW_INPUT_BLACKLIST": r'"accelerometer\nname\"quoted"',
            "TW_DRM_TWO_STAGE_MODESET": "true",
            "TW_LIBTAR_DEBUG": "true",
            "TW_USE_FSCRYPT_POLICY": "1",
            "TW_LOAD_PREBUILT_MODULES_AT_FIRST": "true",
            "TARGET_CUSTOM_KERNEL_HEADERS": '"device/example/include"',
        }
        with tempfile.TemporaryDirectory(prefix="twrp-make-tests-") as temporary:
            values = make_exports(Path(temporary), settings)
        for name, value in settings.items():
            if name != "TW_INCLUDE_CRYPTO":
                self.assertEqual(values[name], value, name)
        self.assertEqual(set(values), exports())
        self.assertEqual(values["TW_INCLUDE_CRYPTO_FBE"], "true", "inferred FBE must be exported after inference")
        self.assertEqual(values["TW_DEFAULT_LANGUAGE"], "en")
        self.assertEqual(values["TW_DEVICE_VERSION"], "0")
        self.assertTrue(values["TW_GIT_REVISION"])

    def test_typed_namespace_routes(self):
        settings = {
            "TW_QCOM_ATS_OFFSET": '"1642433544000"',
            "TW_CLOCK_OFFSET": '"-3600"',
            "TW_USE_KEY_CODE_TOUCH_SYNC": '"330"',
            "TW_AVB_VBMETA_FLAGS_ALL_DISABLED": '"true"',
            "TW_NO_SCREEN_TIMEOUT": '"true"',
            "TW_INCLUDE_CRYPTO": '"true"',
            "TW_FRAMERATE": '"120"',
            "TW_MAX_BRIGHTNESS": '"2047"',
            "TW_INPUT_BLACKLIST": r'"accelerometer\ntest2"',
        }
        with tempfile.TemporaryDirectory(prefix="twrp-namespace-tests-") as temporary:
            directory = Path(temporary)
            raw = make_exports(directory, settings)
            legacy = dict(line.split("\t", 1) for line in (directory / "global-vars.txt").read_text().splitlines())
        for name in settings:
            if name in exports():
                self.assertEqual(raw[name], settings[name], name)
        for key, value in {
            "qcom_ats_offset": "1642433544000", "clock_offset": "-3600",
            "tw_use_key_code_touch_sync": "330", "avb_vbmeta_flags_all_disabled": "true",
            "tw_no_screen_timeout": "true", "tw_framerate": "120", "max_brightness": "2047",
            "include_crypto": "true", "include_crypto_fbe": "true",
        }.items():
            self.assertEqual(legacy[key], value, key)
        self.assertEqual(raw["TW_INCLUDE_CRYPTO_FBE"], "true")
        self.assertNotIn("true", legacy, "a missing comma must not create a namespace key named true")

    def test_optional_offsets_and_mtp_bool(self):
        for excluded, enabled in [("", "true"), ("false", "true"), ('"false"', "true"), ("true", ""), ('"true"', "")]:
            with tempfile.TemporaryDirectory(prefix="twrp-optional-params-") as temporary:
                directory = Path(temporary)
                raw = make_exports(directory, {"TW_EXCLUDE_MTP": excluded})
                legacy = dict(line.split("\t", 1) for line in (directory / "global-vars.txt").read_text().splitlines())
                self.assertEqual(raw["TW_QCOM_ATS_OFFSET"], "")
                self.assertEqual(raw["TW_CLOCK_OFFSET"], "")
                self.assertEqual(legacy["qcom_ats_offset"], "0")
                self.assertEqual(legacy["clock_offset"], "0")
                self.assertEqual(legacy["has_mtp"], enabled)

    def test_vold_policy(self):
        for policy, expected in [("1", "1"), ('"1"', "1"), ("2", "2"), ("", "")]:
            with tempfile.TemporaryDirectory(prefix="twrp-vold-policy-tests-") as temporary:
                directory = Path(temporary)
                values = make_exports(directory, {"TW_USE_FSCRYPT_POLICY": policy})
                self.assertEqual(values["TW_USE_FSCRYPT_POLICY"], policy)
                self.assertEqual((directory / "vold-policy.txt").read_text().strip(), expected)

    def test_go_and_cpp(self):
        with tempfile.TemporaryDirectory(prefix="twrp-go-tests-") as temporary:
            directory = Path(temporary)
            for name in ["soong/variables.go", "soong/variables_test.go", "twrp_flags.go", "twrp_flags_test.go"]:
                shutil.copyfile(RECOVERY / name, directory / Path(name).name)
            result = subprocess.run(["go", "test", "-count=1", "-v", "."], cwd=directory, env={**os.environ, "GO111MODULE": "off"}, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn("--- SKIP: TestTwrpFlagsCompiler", result.stdout, "install a host c++ compiler to exercise macro byte semantics")

    def test_make_to_compiler(self):
        with tempfile.TemporaryDirectory(prefix="twrp-pipeline-tests-") as temporary:
            directory = Path(temporary)
            values = make_exports(directory, {
                "TW_OVERRIDE_SYSTEM_PROPS": '"ro.build.version.security_patch;ro.vendor.build.security_patch"',
                "TW_INTERNAL_STORAGE_PATH": '"/sdcard"',
                "TW_SECONDARY_BRIGHTNESS_PATH": '"/sys/class/leds/secondary/brightness"',
                "TW_INPUT_BLACKLIST": r'"accelerometer\ntest2"',
            })
            for name in ["soong/variables.go", "twrp_flags.go"]:
                text = (RECOVERY / name).read_text().replace("package twrp", "package main", 1)
                (directory / Path(name).name).write_text(text)
            (directory / "main.go").write_text('''package main
import ("encoding/json"; "os")
func main() {
    var vars map[string]string
    if err := json.NewDecoder(os.Stdin).Decode(&vars); err != nil { panic(err) }
    flags, err := twrpFlags(func(name string) string { return vars[name] })
    if err != nil { panic(err) }
    if err := json.NewEncoder(os.Stdout).Encode(flags); err != nil { panic(err) }
}
''')
            result = subprocess.run(["go", "run", "."], input=json.dumps(values), cwd=directory, env={**os.environ, "GO111MODULE": "off"}, check=True, capture_output=True, text=True)
            flags = json.loads(result.stdout)
            source = directory / "probe.cpp"
            source.write_text('''#include <sstream>
#include <string>
int main() {
    std::stringstream properties(TW_OVERRIDE_SYSTEM_PROPS);
    if (properties.str() != "ro.build.version.security_patch;ro.vendor.build.security_patch") return 1;
    if (std::string(TW_INTERNAL_STORAGE_PATH) != "/sdcard") return 2;
    if (std::string(TW_SECONDARY_BRIGHTNESS_PATH) != "/sys/class/leds/secondary/brightness") return 3;
    if (std::string(TW_INPUT_BLACKLIST) != "accelerometer\\ntest2") return 4;
    if (std::string(TW_DEFAULT_LANGUAGE) != "en") return 5;
    if (std::string(TW_DEVICE_VERSION) != "-0") return 6;
}
''')
            binary = directory / "probe"
            subprocess.run(["c++", "-std=c++17", "-Wall", "-Werror", *flags, str(source), "-o", str(binary)], check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True)

    def test_optional_external_storage_path(self):
        # Exercise the actual path-selection block with and without the macro.
        partition = (RECOVERY / "partition.cpp").read_text()
        function = partition.split("void TWPartition::Setup_Data_Media()", 1)[1]
        block = function.split("Is_Settings_Storage = true;", 1)[1].split("const std::string media0", 1)[0]
        with tempfile.TemporaryDirectory(prefix="twrp-optional-storage-tests-") as temporary:
            directory = Path(temporary)
            for value, expected in [(None, "/sdcard"), ('"/sdcard"', "/emmc"), ('"/external_sd"', "/sdcard")]:
                source = directory / "probe.cpp"
                source.write_text(
                    '#include <string>\n#include <string_view>\nint main() {\n'
                    + 'std::string Symlink_Mount_Point;\nauto Make_Dir = [](const char*, bool) {};\n'
                    + block
                    + f'\nreturn Symlink_Mount_Point == "{expected}" ? 0 : 1;\n}}\n'
                )
                flags = [] if value is None else ["-DTW_EXTERNAL_STORAGE_PATH=" + value]
                binary = directory / "probe"
                subprocess.run(["c++", "-std=c++17", "-Wall", "-Werror", *flags, str(source), "-o", str(binary)], check=True, capture_output=True, text=True)
                subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
