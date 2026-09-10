package hostinfo

import "testing"

// TestOSVersionReturnsSomethingNonEmpty is a smoke test - the real
// verification for this package happened by hand during the Go migration:
// running this package's OSVersion() side by side with the actual C++
// hostOsVersion() implementation on this same machine produced byte-for-byte
// identical output ("Ubuntu 24.04.4 LTS (kernel 7.0.0-31-generic)" at the
// time) - see docs/CHANGELOG.md. Both implementations read the same
// /etc/os-release + uname() data, so there's no meaningful fake/mocked unit
// test to write beyond confirming the function doesn't panic and returns
// something.
func TestOSVersionReturnsSomethingNonEmpty(t *testing.T) {
	got := OSVersion()
	if got == "" {
		t.Error("expected a non-empty OS version string")
	}
	t.Logf("OSVersion() = %q", got)
}
