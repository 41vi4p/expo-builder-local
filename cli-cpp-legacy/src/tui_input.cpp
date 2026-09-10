#include "tui_input.hpp"

#ifdef _WIN32
#include <conio.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace ebl {

#ifdef _WIN32

TuiKey readKey() {
  int c = _getch();
  if (c == 0 || c == 0xE0) {
    // Extended key - the scan code is guaranteed to follow in a second call.
    int c2 = _getch();
    if (c2 == 72) return TuiKey::Up;    // VK_UP
    if (c2 == 80) return TuiKey::Down;  // VK_DOWN
    return TuiKey::Other;
  }
  if (c == '\r' || c == '\n') return TuiKey::Enter;
  if (c == 27 || c == 3) return TuiKey::Escape;  // Escape, or Ctrl-C (no SIGINT via _getch())
  return TuiKey::Other;
}

#else

namespace {

// RAII: puts stdin into raw mode (no echo, no line buffering - read() returns as
// soon as a single byte is available) for the duration of one readKey() call, then
// always restores whatever mode it found - so a thrown exception or early return
// can never leave the user's terminal stuck in raw mode.
class RawModeGuard {
 public:
  RawModeGuard() {
    valid_ = tcgetattr(STDIN_FILENO, &old_) == 0;
    if (!valid_) return;
    termios raw = old_;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  }
  ~RawModeGuard() {
    if (valid_) tcsetattr(STDIN_FILENO, TCSANOW, &old_);
  }
  RawModeGuard(const RawModeGuard&) = delete;
  RawModeGuard& operator=(const RawModeGuard&) = delete;

 private:
  termios old_{};
  bool valid_ = false;
};

bool moreInputWithin(int timeoutMs) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
  return select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) > 0;
}

// Raw read() directly on the fd - deliberately NOT std::getchar()/stdio. Mixing
// buffered stdio with select() on the same fd is unsafe: a single getchar() call
// can trigger a read() syscall that slurps every currently-available byte (e.g. an
// entire 3-byte arrow-key escape sequence) into stdio's own internal buffer at
// once, leaving nothing at the kernel level for a later select() to see - which
// made moreInputWithin() wrongly report "nothing more is coming" and misread every
// arrow key as a bare Escape (confirmed: a real pty-driven test of this exact bug
// before this fix). One byte in, one byte out, always straight from the fd.
int readByte() {
  unsigned char c;
  ssize_t n = read(STDIN_FILENO, &c, 1);
  return n == 1 ? c : -1;
}

}  // namespace

TuiKey readKey() {
  RawModeGuard guard;
  int c = readByte();
  if (c == 27) {
    if (!moreInputWithin(50)) return TuiKey::Escape;  // nothing followed - a bare Escape press
    int c2 = readByte();
    if (c2 == '[') {
      if (!moreInputWithin(50)) return TuiKey::Other;
      int c3 = readByte();
      if (c3 == 'A') return TuiKey::Up;
      if (c3 == 'B') return TuiKey::Down;
      return TuiKey::Other;
    }
    return TuiKey::Escape;
  }
  if (c == '\r' || c == '\n') return TuiKey::Enter;
  if (c == 3) return TuiKey::Escape;  // Ctrl-C - raw mode means no real SIGINT for it
  return TuiKey::Other;
}

#endif

}  // namespace ebl
