#pragma once

namespace ebl {

enum class TuiKey { Up, Down, Enter, Escape, Other };

/** Reads a single "navigation" keypress from the terminal in raw mode (no echo, no
 * line buffering) - blocks until one arrives. Arrow keys are recognized via their
 * platform-specific encoding and normalized to Up/Down; Enter/Return and Escape
 * (or Ctrl-C, which raw mode stops the terminal from turning into a real SIGINT)
 * are recognized directly; anything else comes back as Other for the caller to
 * ignore.
 *
 * POSIX: arrow keys arrive as the 3-byte escape sequence `\x1b[A`/`\x1b[B` -
 * indistinguishable from a bare Escape keypress until the *next* byte either does
 * or doesn't show up, so after seeing a lone 0x1B this waits up to 50ms (a real
 * escape sequence's remaining bytes arrive together, effectively instantly) before
 * deciding it was just Escape - without that, a bare Escape press would hang
 * waiting for a `[` that's never coming.
 *
 * Windows: `_getch()` gives arrow keys as an unambiguous two-call sequence (first
 * call returns 0/0xE0 signaling "extended key", second call is guaranteed to
 * follow immediately with the scan code) - no such ambiguity, no timeout needed.
 * UNVERIFIED ON REAL WINDOWS HARDWARE - written from documented Win32 console API
 * behavior; there's no Windows machine available to actually run this on (same
 * caveat as every other Windows-specific branch in this codebase - see
 * ../CLAUDE.md). Report anything that doesn't work. */
TuiKey readKey();

}  // namespace ebl
