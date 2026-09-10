package commands

import "testing"

func TestSetupHelpReturnsZero(t *testing.T) {
	silenceOutput(t)
	if code := RunSetup([]string{"--help"}); code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
	if code := RunSetup([]string{"-h"}); code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
}
