package twrp

import "android/soong/android"

// Preserve quotes and escapes until the value reaches its consumer.
func getMakeVars(ctx android.BaseContext, name string) string {
	return ctx.Config().VendorConfig("twrpVarsPlugin").String(name)
}

func getMakeValue(ctx android.BaseContext, name string) string {
	return makeVarValue(getMakeVars(ctx, name))
}

func getMakeScalar(ctx android.BaseContext, name string) string {
	return makeVarScalar(getMakeVars(ctx, name))
}

func getMakeBool(ctx android.BaseContext, name string) bool {
	return makeBoolValue(getMakeVars(ctx, name))
}
