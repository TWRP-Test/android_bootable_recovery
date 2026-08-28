package twrp

import (
	"android/soong/android"
	"strings"
)

func getMakeVars(ctx android.BaseContext, mVar string) string {
	makeVars := ctx.Config().VendorConfig("twrpVarsPlugin")
	var makeVar = ""
	if makeVars.IsSet(mVar) {
		makeVar = strings.TrimSpace(makeVars.String(mVar))
		if len(makeVar) >= 2 && makeVar[0] == '"' && makeVar[len(makeVar)-1] == '"' {
			makeVar = makeVar[1 : len(makeVar)-1]
		}
	}
	return makeVar
}
