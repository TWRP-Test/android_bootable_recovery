// TWRP-only: BoardConfig options layered on the current minui backend implementation.
package twrp

import (
	"android/soong/android"
	"android/soong/cc"
)

func globalFlags(ctx android.BaseContext) []string {
	var cflags []string

	if getMakeBool(ctx, "TW_DRM_TWO_STAGE_MODESET") {
		cflags = append(cflags, "-DTW_DRM_TWO_STAGE_MODESET")
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

	if getMakeBool(ctx, "TW_HAPTICS_TSPDRV") {
		srcs = append(srcs, "tspdrv.cpp")
	}
	return srcs
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

	return sharedLibs
}

func libTwrpMinuiDefaults(ctx android.LoadHookContext) {
	type props struct {
		Cflags      []string
		Srcs        []string
		Shared_libs []string
	}
	ctx.AppendProperties(&props{
		Cflags:      globalFlags(ctx),
		Srcs:        globalSrcs(ctx),
		Shared_libs: globalSharedLibs(ctx),
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
