package twrp

import (
	"android/soong/android"
	"android/soong/cc"
)

func libTarDefaultsFactory() android.Module {
	module := cc.DefaultsFactory()
	android.AddLoadHook(module, func(ctx android.LoadHookContext) {
		type props struct {
			Cflags       []string
			Include_dirs []string
		}
		p := &props{}
		if getMakeVars(ctx, "TW_LIBTAR_DEBUG") == "true" {
			p.Cflags = append(p.Cflags, "-DTW_LIBTAR_DEBUG")
		}
		if getMakeVars(ctx, "TW_INCLUDE_CRYPTO_FBE") == "true" {
			p.Cflags = append(p.Cflags, "-DUSE_FSCRYPT")
			p.Include_dirs = append(p.Include_dirs, "system/vold")
			if getMakeVars(ctx, "TW_USE_FSCRYPT_POLICY") == "1" {
				p.Cflags = append(p.Cflags, "-DUSE_FSCRYPT_POLICY_V1")
			} else {
				p.Cflags = append(p.Cflags, "-DUSE_FSCRYPT_POLICY_V2")
			}
		}
		ctx.AppendProperties(p)
	})
	return module
}

func init() {
	android.RegisterModuleType("libtar_defaults", libTarDefaultsFactory)
}
