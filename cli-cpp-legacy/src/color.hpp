#pragma once
// Minimal ANSI color helpers — no-op (plain text) when stdout isn't a terminal, e.g.
// when output is piped or redirected to a file.
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace ebl::color {

inline bool enabled() {
#ifdef _WIN32
  static bool value = _isatty(_fileno(stdout)) != 0;
#else
  static bool value = isatty(fileno(stdout)) != 0;
#endif
  return value;
}

/** Same idea as enabled(), but checks stdin - needed by anything that reads
 * interactive input (e.g. `ebl build --tui`'s menu), since that can't work at all
 * if stdin is piped/redirected (no arrow-key presses will ever arrive), separately
 * from whether stdout happens to be a real terminal. */
inline bool stdinIsTty() {
#ifdef _WIN32
  static bool value = _isatty(_fileno(stdin)) != 0;
#else
  static bool value = isatty(fileno(stdin)) != 0;
#endif
  return value;
}

inline std::string wrap(const std::string& code, const std::string& text) {
  if (!enabled()) return text;
  return "\x1b[" + code + "m" + text + "\x1b[0m";
}

inline std::string bold(const std::string& s) { return wrap("1", s); }
inline std::string dim(const std::string& s) { return wrap("2", s); }
inline std::string red(const std::string& s) { return wrap("31", s); }
inline std::string green(const std::string& s) { return wrap("32", s); }
inline std::string yellow(const std::string& s) { return wrap("33", s); }
inline std::string cyan(const std::string& s) { return wrap("36", s); }

}  // namespace ebl::color
