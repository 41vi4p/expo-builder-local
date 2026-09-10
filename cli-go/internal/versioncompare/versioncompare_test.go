package versioncompare

import "testing"

// Ported 1:1 from cli/tests/test_version_compare.cpp.

func TestMinorVersionTenBeatsNineNumericallyNotLexically(t *testing.T) {
	if !IsNewer("0.10.0", "0.9.0") {
		t.Error("expected 0.10.0 to be newer than 0.9.0")
	}
	if IsNewer("0.9.0", "0.10.0") {
		t.Error("expected 0.9.0 to not be newer than 0.10.0")
	}
}

func TestPatchVersionTenBeatsNineNumericallyNotLexically(t *testing.T) {
	if !IsNewer("1.2.10", "1.2.9") {
		t.Error("expected 1.2.10 to be newer than 1.2.9")
	}
	if IsNewer("1.2.9", "1.2.10") {
		t.Error("expected 1.2.9 to not be newer than 1.2.10")
	}
}

func TestLeadingVIsToleratedOnEitherSide(t *testing.T) {
	if !IsNewer("v1.0.0", "0.9.0") {
		t.Error("expected v1.0.0 to be newer than 0.9.0")
	}
	if !IsNewer("1.0.0", "v0.9.0") {
		t.Error("expected 1.0.0 to be newer than v0.9.0")
	}
	if !IsNewer("v1.0.0", "v0.9.0") {
		t.Error("expected v1.0.0 to be newer than v0.9.0")
	}
}

func TestEqualVersionsAreNotNewer(t *testing.T) {
	if IsNewer("1.2.3", "1.2.3") {
		t.Error("expected equal versions to not be newer")
	}
	if IsNewer("v1.2.3", "1.2.3") {
		t.Error("expected v1.2.3 to not be newer than 1.2.3")
	}
}

func TestOlderVersionIsNotNewer(t *testing.T) {
	if IsNewer("0.9.0", "1.0.0") {
		t.Error("expected 0.9.0 to not be newer than 1.0.0")
	}
}

func TestMissingTrailingComponentTreatedAsZero(t *testing.T) {
	if IsNewer("1.2", "1.2.0") {
		t.Error("expected 1.2 to not be newer than 1.2.0")
	}
	if !IsNewer("1.3", "1.2.9") {
		t.Error("expected 1.3 to be newer than 1.2.9")
	}
}
