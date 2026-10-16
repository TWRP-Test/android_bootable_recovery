package twrp

import (
	"android/soong/android"
	"android/soong/cc"
	"path/filepath"
)

func globalFlags(ctx android.BaseContext) []string {
	var cflags []string

	if getMakeBool(ctx, "TW_DRM_TWO_STAGE_MODESET") {
		cflags = append(cflags, "-DTW_DRM_TWO_STAGE_MODESET")
	}

	matches, err := filepath.Glob("external/libdrm/Android.*")
	_ = matches
	if err == nil {
		cflags = append(cflags, "-DHAS_DRM")
	}

	var forcedPixelFormat = getMakeScalar(ctx, "TARGET_RECOVERY_FORCE_PIXEL_FORMAT")
	var pixelFormat = forcedPixelFormat
	if pixelFormat == "" {
		pixelFormat = getMakeScalar(ctx, "TARGET_RECOVERY_PIXEL_FORMAT")
	}

	switch pixelFormat {
	case "RGBA_8888":
		cflags = append(cflags, "-DRECOVERY_RGBA")
		break

	case "RGBX_8888":
		cflags = append(cflags, "-DRECOVERY_RGBX")
		break

	case "BGRA_8888":
		cflags = append(cflags, "-DRECOVERY_BGRA")
		break

	case "ABGR_8888":
		cflags = append(cflags, "-DRECOVERY_ABGR")
		break

	case "ARGB_8888":
		cflags = append(cflags, "-DRECOVERY_ARGB")
		break

	case "RGB_565":
		if forcedPixelFormat != "" {
			cflags = append(cflags, "-DRECOVERY_FORCE_RGB_565")
		}
		break
	}

	var tw_rotation = getMakeScalar(ctx, "TW_ROTATION")
	switch tw_rotation {
	case "0":
		fallthrough
	case "90":
		fallthrough
	case "180":
		fallthrough
	case "270":
		cflags = append(cflags, "-DTW_ROTATION="+tw_rotation)
	default:
		if getMakeBool(ctx, "BOARD_HAS_FLIPPED_SCREEN") {
			cflags = append(cflags, "-DTW_ROTATION=180")
		} else {
			cflags = append(cflags, "-DTW_ROTATION=0")
		}
	}
	return cflags
}

func globalSrcs(ctx android.BaseContext) []string {
	var srcs []string

	if getMakeBool(ctx, "TW_TARGET_USES_QCOM_BSP") {
		srcs = append(srcs, "graphics_overlay.cpp")
	}

	matches, err := filepath.Glob("external/libdrm/Android.*")
	_ = matches
	if err == nil {
		srcs = append(srcs, "graphics_drm.cpp")
	}

	if getMakeBool(ctx, "TW_HAPTICS_TSPDRV") {
		srcs = append(srcs, "tspdrv.cpp")
	}
	return srcs
}

func globalIncludes(ctx android.BaseContext) []string {
	includes := []string{"bootable/recovery/twrpminui"}
	if getMakeBool(ctx, "TW_TARGET_USES_QCOM_BSP") {
		if headers := getMakeValue(ctx, "TARGET_CUSTOM_KERNEL_HEADERS"); headers != "" {
			includes = append(includes, headers)
		}
	}
	if getMakeBool(ctx, "TW_INCLUDE_JPEG") {
		includes = append(includes, "external/jpeg")
	}
	return includes
}

func globalHeaderLibs(ctx android.BaseContext) []string {
	if getMakeBool(ctx, "TW_TARGET_USES_QCOM_BSP") &&
		getMakeValue(ctx, "TARGET_CUSTOM_KERNEL_HEADERS") == "" {
		return []string{"generated_kernel_headers"}
	}
	return nil
}

func globalStaticLibs(ctx android.BaseContext) []string {
	var staticLibs []string

	matches, err := filepath.Glob("external/libdrm/Android.*")
	_ = matches
	if err == nil {
		matches, err = filepath.Glob("external/libdrm/Android.common.mk")
		if err != nil {
			staticLibs = append(staticLibs, "libdrm_platform")
		} else {
			staticLibs = append(staticLibs, "libdrm")
		}
	}

	return staticLibs
}

func globalSharedLibs(ctx android.BaseContext) []string {
	var sharedLibs []string

	if getMakeBool(ctx, "TW_SUPPORT_INPUT_1_2_HAPTICS") {
		sharedLibs = append(sharedLibs, "android.hardware.vibrator@1.2")
		sharedLibs = append(sharedLibs, "libhidlbase")
	}

	if getMakeBool(ctx, "TW_SUPPORT_INPUT_AIDL_HAPTICS") {
		sharedLibs = append(sharedLibs, "android.hardware.vibrator-V2-ndk")
		sharedLibs = append(sharedLibs, "android.hardware.vibrator-V2-cpp")
	}

	if getMakeBool(ctx, "TW_INCLUDE_JPEG") {
		sharedLibs = append(sharedLibs, "libjpeg")
	}
	return sharedLibs
}

func libTwrpMinuiDefaults(ctx android.LoadHookContext) {
	type props struct {
		Cflags       []string
		Srcs         []string
		Include_dirs []string
		Static_libs  []string
		Shared_libs  []string
		Header_libs  []string
	}
	ctx.AppendProperties(&props{
		Cflags:       globalFlags(ctx),
		Srcs:         globalSrcs(ctx),
		Include_dirs: globalIncludes(ctx),
		Static_libs:  globalStaticLibs(ctx),
		Shared_libs:  globalSharedLibs(ctx),
		Header_libs:  globalHeaderLibs(ctx),
	})
}

func init() {
	android.RegisterModuleType("libtwrpminui_defaults", libTwrpMinuiDefaultsFactory)
}

func libTwrpMinuiDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, libTwrpMinuiDefaults)

	return module
}
