package twrp

import (
	"android/soong/android"
	"android/soong/cc"
	"fmt"
	"strings"
)

func globalFlags(ctx android.BaseContext) []string {
    var cflags []string

	var makeVar string
    if makeVar = getMakeVars(ctx, "TW_OZIP_DECRYPT_KEY"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_OZIP_DECRYPT_KEY=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_LOAD_VENDOR_MODULES"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_LOAD_VENDOR_MODULES=\"%s\"", makeVar))
        if makeVar = getMakeVars(ctx, "TW_LOAD_VENDOR_MODULES_EXCLUDE_GKI"); makeVar != "" {
            cflags = append(cflags, "-DTW_LOAD_VENDOR_MODULES_EXCLUDE_GKI")
        }
        if makeVar = getMakeVars(ctx, "TW_LOAD_VENDOR_BOOT_MODULES"); makeVar != "" {
            cflags = append(cflags, "-DTW_LOAD_VENDOR_BOOT_MODULES")
        }
        if makeVar = getMakeVars(ctx, "TW_LOAD_PREBUILT_MODULES_AT_FIRST"); makeVar != "" {
            cflags = append(cflags, "-DTW_LOAD_PREBUILT_MODULES_AT_FIRST")
        }
    }
    if makeVar = getMakeVars(ctx, "TW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_SYSTEM_BUILD_PROP_ADDITIONAL_PATHS=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_INTERNAL_STORAGE_PATH"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_INTERNAL_STORAGE_PATH=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_INTERNAL_STORAGE_MOUNT_POINT"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_INTERNAL_STORAGE_MOUNT_POINT=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_EXTERNAL_STORAGE_PATH"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_EXTERNAL_STORAGE_PATH=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_EXTERNAL_STORAGE_MOUNT_POINT"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_EXTERNAL_STORAGE_MOUNT_POINT=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_CUSTOM_POWER_BUTTON"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_CUSTOM_POWER_BUTTON=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TARGET_USE_CUSTOM_LUN_FILE_PATH"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTARGET_USE_CUSTOM_LUN_FILE_PATH=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_ADDITIONAL_APEX_FILES"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_ADDITIONAL_APEX_FILES=%s", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_BRIGHTNESS_PATH"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_BRIGHTNESS_PATH=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_SECONDARY_BRIGHTNESS_PATH"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_SECONDARY_BRIGHTNESS_PATH=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_CUSTOM_BATTERY_PATH"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_CUSTOM_BATTERY_PATH=%s", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_CUSTOM_CPU_TEMP_PATH"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_CUSTOM_CPU_TEMP_PATH=%s", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_OVERRIDE_SYSTEM_PROPS"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_OVERRIDE_SYSTEM_PROPS=%s", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_OVERRIDE_PROPS_ADDITIONAL_PARTITIONS=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TARGET_OTA_ASSERT_DEVICE"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTARGET_OTA_ASSERT_DEVICE=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "TW_BACKUP_EXCLUSIONS"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DTW_BACKUP_EXCLUSIONS=\"%s\"", makeVar))
    }
    if makeVar = getMakeVars(ctx, "BOARD_BOOT_HEADER_VERSION"); makeVar != "" {
        cflags = append(cflags, fmt.Sprintf("-DBOARD_BOOT_HEADER_VERSION=\"%s\"", makeVar))
    }
	
    return cflags
}

func globalStaticLibs(ctx android.BaseContext) []string {
	var staticLibs []string

	var makeVar string
    if makeVar = getMakeVars(ctx, "TW_LOAD_VENDOR_MODULES"); makeVar != "" {
        staticLibs = append(staticLibs, "libmodprobe")
    }
    if makeVar = getMakeVars(ctx, "TARGET_RECOVERY_TWRP_LIB"); makeVar != "" {
        staticLibs = append(staticLibs, strings.Split(makeVar, " ")...)
    }

	return staticLibs
}

func globalSrcs(ctx android.BaseContext) []string {
	var srcs []string

	var makeVar string
    if makeVar = getMakeVars(ctx, "TW_LOAD_VENDOR_MODULES"); makeVar != "" {
        srcs = append(srcs, "kernel_module_loader.cpp")
    }

	return srcs
}

func globalRequired(ctx android.BaseContext) []string {
	var required []string

	var makeVar string
    if makeVar = getMakeVars(ctx, "TW_OZIP_DECRYPT_KEY"); makeVar != "" {
        required = append(required, "ozip_decrypt")
    }

	return required
}

func twrpRecoveryDefaults(ctx android.LoadHookContext) {
	type props struct {
		Target struct {
			Android struct {
				Cflags  []string
				Enabled *bool
			}
		}
		Cflags       []string
		Static_libs  []string
        Srcs         []string
		Required     []string
		Include_dirs []string
	}

	p := &props{}
	p.Cflags = globalFlags(ctx)
	p.Static_libs = globalStaticLibs(ctx)
	p.Srcs = globalSrcs(ctx)
	p.Required = globalRequired(ctx)
	ctx.AppendProperties(p)
}

func init() {
	android.RegisterModuleType("twrp_recovery_defaults", twrpRecoveryDefaultsFactory)
}

func twrpRecoveryDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, twrpRecoveryDefaults)

	return module
}
