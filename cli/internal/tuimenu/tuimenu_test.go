package tuimenu

import "testing"

func TestSelectReturnsMinusOneForEmptyOptions(t *testing.T) {
	if got := Select("Title", nil, 0); got != -1 {
		t.Errorf("got %d, want -1", got)
	}
}
