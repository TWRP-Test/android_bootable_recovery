package twrp

import "fmt"

var twrpStringVariables = []struct {
	variable string
	macro    string
}{
	{"TW_OZIP_DECRYPT_KEY", "TW_OZIP_DECRYPT_KEY"},
	{"TW_LOAD_VENDOR_MODULES", "TW_LOAD_VENDOR_MODULES"},
	{"TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS", "TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS"},
	{"TW_INTERNAL_STORAGE_PATH", "TW_INTERNAL_STORAGE_PATH"},
	{"TW_EXTERNAL_STORAGE_PATH", "TW_EXTERNAL_STORAGE_PATH"},
	{"TARGET_USE_CUSTOM_LUN_FILE_PATH", "CUSTOM_LUN_FILE"},
	{"TW_ADDITIONAL_APEX_FILES", "TW_ADDITIONAL_APEX_FILES"},
	{"TW_BRIGHTNESS_PATH", "TW_BRIGHTNESS_PATH"},
	{"TW_SECONDARY_BRIGHTNESS_PATH", "TW_SECONDARY_BRIGHTNESS_PATH"},
	{"TW_CUSTOM_BATTERY_PATH", "TW_CUSTOM_BATTERY_PATH"},
	{"TW_CUSTOM_CPU_TEMP_PATH", "TW_CUSTOM_CPU_TEMP_PATH"},
	{"TW_OVERRIDE_SYSTEM_PROPS", "TW_OVERRIDE_SYSTEM_PROPS"},
	{"TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS", "TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS"},
	{"TARGET_OTA_ASSERT_DEVICE", "TARGET_OTA_ASSERT_DEVICE"},
	{"TW_BACKUP_EXCLUSIONS", "TW_BACKUP_EXCLUSIONS"},
	{"TW_INPUT_BLACKLIST", "TW_INPUT_BLACKLIST"},
	{"TW_WHITELIST_INPUT", "WHITELIST_INPUT"},
	{"TW_SUPPORT_INPUT_AIDL_HAPTICS_FQNAME", "USE_QTI_AIDL_HAPTICS_FQNAME"},
	{"USB_MTP_DEVICE", "USB_MTP_DEVICE"},
}

func twrpFlags(getVar func(string) string) ([]string, error) {
	var flags []string
	for _, variable := range twrpStringVariables {
		if literal := makeCStringLiteral(getVar(variable.variable)); literal != "" {
			flags = append(flags, "-D"+variable.macro+"="+literal)
		}
	}

	for _, variable := range []struct {
		name     string
		fallback string
		prefix   string
	}{
		{"TW_DEFAULT_LANGUAGE", "en", ""},
		{"TW_DEVICE_VERSION", "0", `"-"`},
		{"TW_GIT_REVISION", "unknown", ""},
	} {
		literal := makeCStringLiteral(getVar(variable.name))
		if literal == "" {
			literal = makeCStringLiteral(variable.fallback)
		}
		flags = append(flags, "-D"+variable.name+"="+variable.prefix+literal)
	}

	for _, variable := range []struct {
		name  string
		parse func(string) (string, error)
	}{
		{"TW_CUSTOM_POWER_BUTTON", makeIntLiteral},
		{"BOARD_BOOT_HEADER_VERSION", makeIntLiteral},
		{"TW_USE_KEY_CODE_TOUCH_SYNC", makeIntLiteral},
		{"TW_QCOM_ATS_OFFSET", makeUint64Literal},
		{"TW_CLOCK_OFFSET", makeInt64Literal},
	} {
		literal, err := variable.parse(getVar(variable.name))
		if err != nil {
			return nil, fmt.Errorf("%s: %w", variable.name, err)
		}
		if literal != "" {
			flags = append(flags, "-D"+variable.name+"="+literal)
		}
	}

	// Match the policy layout used by libtar and libvold.
	if makeBoolValue(getVar("TW_INCLUDE_CRYPTO_FBE")) &&
		makeVarScalar(getVar("TW_USE_FSCRYPT_POLICY")) == "1" {
		flags = append(flags, "-DUSE_FSCRYPT_POLICY_V1")
	}

	if makeCStringLiteral(getVar("TW_LOAD_VENDOR_MODULES")) != "" {
		for _, name := range []string{
			"TW_LOAD_VENDOR_MODULES_EXCLUDE_GKI",
			"TW_LOAD_VENDOR_BOOT_MODULES",
			"TW_LOAD_PREBUILT_MODULES_AT_FIRST",
		} {
			if makeBoolValue(getVar(name)) {
				flags = append(flags, "-D"+name)
			}
		}
	}
	return flags, nil
}
