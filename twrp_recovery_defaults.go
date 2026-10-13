package twrp

import (
	"android/soong/android"
	"android/soong/cc"
	"strings"
)

func globalFlags(ctx android.BaseContext) []string {
	flags, err := twrpFlags(func(name string) string { return getMakeVars(ctx, name) })
	if err != nil {
		ctx.ModuleErrorf("%s", err)
		return nil
	}
	return flags
}

func globalStaticLibs(ctx android.BaseContext) []string {
	var staticLibs []string

	if getMakeValue(ctx, "TW_LOAD_VENDOR_MODULES") != "" {
		staticLibs = append(staticLibs, "libmodprobe")
	}
	staticLibs = append(staticLibs, strings.Fields(getMakeValue(ctx, "TARGET_RECOVERY_TWRP_LIB"))...)

	return staticLibs
}

func globalSrcs(ctx android.BaseContext) []string {
	if getMakeValue(ctx, "TW_LOAD_VENDOR_MODULES") != "" {
		return []string{"kernel_module_loader.cpp"}
	}
	return nil
}

func globalRequired(ctx android.BaseContext) []string {
	if getMakeValue(ctx, "TW_OZIP_DECRYPT_KEY") != "" {
		return []string{"ozip_decrypt"}
	}
	return nil
}

func twrpRecoveryDefaults(ctx android.LoadHookContext) {
	type props struct {
		Static_libs []string
		Srcs        []string
		Required    []string
	}

	ctx.AppendProperties(&props{
		Static_libs: globalStaticLibs(ctx),
		Srcs:        globalSrcs(ctx),
		Required:    globalRequired(ctx),
	})
}

func twrpCflagsDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, func(ctx android.LoadHookContext) {
		ctx.AppendProperties(&struct{ Cflags []string }{Cflags: globalFlags(ctx)})
	})
	return module
}

func init() {
	android.RegisterModuleType("twrp_cflags_defaults", twrpCflagsDefaultsFactory)
	android.RegisterModuleType("twrp_recovery_defaults", twrpRecoveryDefaultsFactory)
}

func twrpRecoveryDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, twrpRecoveryDefaults)

	return module
}
