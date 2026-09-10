// Package versioncompare compares dotted-integer version strings (e.g. "1.2.10"
// vs "1.2.9"), tolerating a leading "v" on either side and treating a missing
// trailing component as 0. Direct port of cli/src/version_compare.cpp.
package versioncompare

import (
	"strconv"
	"strings"
)

func stripLeadingV(s string) string {
	if strings.HasPrefix(s, "v") {
		return s[1:]
	}
	return s
}

func versionParts(s string) []int {
	fields := strings.Split(s, ".")
	parts := make([]int, len(fields))
	for i, f := range fields {
		n, err := strconv.Atoi(f)
		if err != nil {
			n = 0
		}
		parts[i] = n
	}
	return parts
}

// IsNewer reports whether a is a strictly newer version than b, comparing each
// dotted component numerically (not lexically - "0.10.0" is newer than "0.9.0").
func IsNewer(a, b string) bool {
	pa := versionParts(stripLeadingV(a))
	pb := versionParts(stripLeadingV(b))
	n := len(pa)
	if len(pb) > n {
		n = len(pb)
	}
	for i := 0; i < n; i++ {
		av, bv := 0, 0
		if i < len(pa) {
			av = pa[i]
		}
		if i < len(pb) {
			bv = pb[i]
		}
		if av != bv {
			return av > bv
		}
	}
	return false
}
