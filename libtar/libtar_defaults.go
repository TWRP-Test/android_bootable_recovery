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
		if getMakeBool(ctx, "TW_LIBTAR_DEBUG") {
			p.Cflags = append(p.Cflags, "-DTW_LIBTAR_DEBUG")
		}
		if getMakeBool(ctx, "TW_INCLUDE_CRYPTO_FBE") {
			p.Cflags = append(p.Cflags, "-DUSE_FSCRYPT")
			p.Include_dirs = append(p.Include_dirs, "system/vold")
			if getMakeScalar(ctx, "TW_USE_FSCRYPT_POLICY") == "1" {
				p.Cflags = append(p.Cflags, "-DUSE_FSCRYPT_POLICY_V1")
			}
		}
		ctx.AppendProperties(p)
	})
	return module
}

func init() {
	android.RegisterModuleType("libtar_defaults", libTarDefaultsFactory)
}
