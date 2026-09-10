// HttpClient over Docker Desktop's named pipe (\\.\pipe\docker_engine) — the same
// endpoint the real `docker.exe` CLI talks to. libcurl has no Windows-named-pipe
// transport, so this hand-rolls plain HTTP/1.1 request/response framing over
// CreateFileW/ReadFile/WriteFile using overlapped I/O (so reads can honor the
// caller's timeout without a second thread). See http_client.hpp for the shared
// interface; httpGetTcp/urlEncode live in http_client_common.cpp.
//
// One pipe handle per request ("Connection: close"), matching how the unix-socket
// implementation opens a fresh curl handle per call — simpler than juggling
// keep-alive/pipelining for a client that only ever talks to one, local daemon.
#include "http_client.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace ebl {

namespace {

constexpr wchar_t kDockerPipeName[] = L"\\\\.\\pipe\\docker_engine";
constexpr DWORD kConnectTimeoutMs = 5000;
constexpr DWORD kMaxHeaderBytes = 1u << 20;

struct PipeHandle {
  HANDLE h;
  ~PipeHandle() {
    if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
  }
};

struct EventHandle {
  HANDLE h;
  EventHandle() : h(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!h) throw std::runtime_error("Failed to create a Windows event for pipe I/O");
  }
  ~EventHandle() { ::CloseHandle(h); }
};

HANDLE connectDockerPipe() {
  DWORD deadline = ::GetTickCount() + kConnectTimeoutMs;
  for (;;) {
    HANDLE h = ::CreateFileW(kDockerPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      DWORD mode = PIPE_READMODE_BYTE;
      ::SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
      return h;
    }
    DWORD err = ::GetLastError();
    if (err != ERROR_PIPE_BUSY) {
      throw std::runtime_error("Could not connect to the Docker Engine API pipe (\\\\.\\pipe\\docker_engine, "
                                "error " +
                                std::to_string(err) + "). Is Docker Desktop installed and running?");
    }
    DWORD now = ::GetTickCount();
    DWORD remaining = deadline > now ? deadline - now : 0;
    if (remaining == 0 || !::WaitNamedPipeW(kDockerPipeName, remaining)) {
      throw std::runtime_error("Timed out waiting for the Docker Engine API pipe — is Docker Desktop starting up?");
    }
  }
}

void writeAll(HANDLE pipe, const char* data, size_t len) {
  size_t written = 0;
  while (written < len) {
    EventHandle ev;
    OVERLAPPED ov{};
    ov.hEvent = ev.h;
    DWORD chunk = static_cast<DWORD>(std::min<size_t>(len - written, 1u << 20));
    BOOL ok = ::WriteFile(pipe, data + written, chunk, nullptr, &ov);
    if (!ok && ::GetLastError() != ERROR_IO_PENDING) {
      throw std::runtime_error("Failed writing to the Docker Engine API pipe");
    }
    DWORD transferred = 0;
    if (!::GetOverlappedResult(pipe, &ov, &transferred, TRUE)) {
      throw std::runtime_error("Failed writing to the Docker Engine API pipe");
    }
    written += transferred;
  }
}

/** One ReadFile call, waiting up to timeoutMs (0 == wait forever) for data.
 * Returns false on a clean close (nothing left to read), true with `out` set
 * otherwise. Throws on a real I/O error or on hitting the timeout. */
bool readSome(HANDLE pipe, DWORD timeoutMs, std::string& out) {
  char buf[8192];
  EventHandle ev;
  OVERLAPPED ov{};
  ov.hEvent = ev.h;
  BOOL ok = ::ReadFile(pipe, buf, sizeof(buf), nullptr, &ov);
  if (!ok) {
    DWORD err = ::GetLastError();
    if (err == ERROR_BROKEN_PIPE) return false;
    if (err != ERROR_IO_PENDING) throw std::runtime_error("Failed reading from the Docker Engine API pipe");
  }
  DWORD waitResult = ::WaitForSingleObject(ev.h, timeoutMs == 0 ? INFINITE : timeoutMs);
  if (waitResult == WAIT_TIMEOUT) {
    ::CancelIoEx(pipe, &ov);
    throw std::runtime_error("Timed out waiting for a response from the Docker daemon");
  }
  DWORD transferred = 0;
  if (!::GetOverlappedResult(pipe, &ov, &transferred, TRUE)) {
    DWORD err = ::GetLastError();
    if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF) return false;
    throw std::runtime_error("Failed reading from the Docker Engine API pipe");
  }
  if (transferred == 0) return false;
  out.assign(buf, transferred);
  return true;
}

std::string lowercase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool hasHeader(const std::vector<std::string>& headers, const std::string& name) {
  std::string prefix = lowercase(name) + ":";
  for (const auto& h : headers) {
    if (lowercase(h).rfind(prefix, 0) == 0) return true;
  }
  return false;
}

std::string buildRequest(const std::string& method, const std::string& path, const std::string& body,
                          const std::vector<std::string>& headers) {
  std::string req = method + " " + path + " HTTP/1.1\r\n";
  req += "Host: localhost\r\n";
  req += "Connection: close\r\n";
  for (const auto& h : headers) req += h + "\r\n";
  if (!hasHeader(headers, "Content-Length")) req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  req += "\r\n";
  req += body;
  return req;
}

struct ResponseHead {
  long status = 0;
  bool chunked = false;
  bool hasContentLength = false;
  size_t contentLength = 0;
  std::string leftover;  // body bytes already read past the \r\n\r\n terminator
};

ResponseHead readResponseHead(HANDLE pipe, DWORD timeoutMs) {
  std::string buf;
  size_t headerEnd;
  for (;;) {
    headerEnd = buf.find("\r\n\r\n");
    if (headerEnd != std::string::npos) break;
    std::string chunk;
    if (!readSome(pipe, timeoutMs, chunk)) {
      throw std::runtime_error("The Docker daemon closed the connection before sending a full response");
    }
    buf += chunk;
    if (buf.size() > kMaxHeaderBytes) throw std::runtime_error("Response headers from the Docker daemon are too large");
  }

  std::string headText = buf.substr(0, headerEnd);
  ResponseHead head;
  head.leftover = buf.substr(headerEnd + 4);

  size_t lineEnd = headText.find("\r\n");
  std::string statusLine = headText.substr(0, lineEnd);
  size_t sp1 = statusLine.find(' ');
  if (sp1 != std::string::npos) {
    size_t sp2 = statusLine.find(' ', sp1 + 1);
    std::string codeStr = statusLine.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
    try {
      head.status = std::stol(codeStr);
    } catch (const std::exception&) {
      throw std::runtime_error("Malformed HTTP status line from the Docker daemon: " + statusLine);
    }
  }

  size_t pos = lineEnd == std::string::npos ? headText.size() : lineEnd + 2;
  while (pos < headText.size()) {
    size_t nl = headText.find("\r\n", pos);
    if (nl == std::string::npos) nl = headText.size();
    std::string line = headText.substr(pos, nl - pos);
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string name = lowercase(line.substr(0, colon));
      size_t valStart = colon + 1;
      while (valStart < line.size() && line[valStart] == ' ') valStart++;
      std::string value = line.substr(valStart);
      if (name == "content-length") {
        head.hasContentLength = true;
        head.contentLength = static_cast<size_t>(std::stoull(value));
      } else if (name == "transfer-encoding" && lowercase(value).find("chunked") != std::string::npos) {
        head.chunked = true;
      }
    }
    pos = nl + 2;
  }

  return head;
}

/** Pulls more bytes into `pending` if it's currently empty. Returns false if the
 * pipe closed with nothing left to give. */
bool refill(HANDLE pipe, DWORD timeoutMs, std::string& pending) {
  if (!pending.empty()) return true;
  return readSome(pipe, timeoutMs, pending);
}

std::string takeBytes(std::string& pending, HANDLE pipe, DWORD timeoutMs, size_t n) {
  std::string out;
  out.reserve(n);
  while (out.size() < n) {
    if (!refill(pipe, timeoutMs, pending)) {
      throw std::runtime_error("The Docker daemon closed the connection mid-response");
    }
    size_t take = std::min(n - out.size(), pending.size());
    out.append(pending, 0, take);
    pending.erase(0, take);
  }
  return out;
}

std::string readLine(std::string& pending, HANDLE pipe, DWORD timeoutMs) {
  for (;;) {
    size_t nl = pending.find("\r\n");
    if (nl != std::string::npos) {
      std::string line = pending.substr(0, nl);
      pending.erase(0, nl + 2);
      return line;
    }
    if (!refill(pipe, timeoutMs, pending)) {
      throw std::runtime_error("The Docker daemon closed the connection mid-response");
    }
  }
}

long deliverBody(HANDLE pipe, DWORD timeoutMs, const ResponseHead& head,
                  const std::function<void(const char*, size_t)>& onChunk) {
  std::string pending = head.leftover;

  if (head.chunked) {
    for (;;) {
      std::string sizeLine = readLine(pending, pipe, timeoutMs);
      size_t semi = sizeLine.find(';');
      if (semi != std::string::npos) sizeLine = sizeLine.substr(0, semi);
      size_t chunkSize;
      try {
        chunkSize = static_cast<size_t>(std::stoull(sizeLine, nullptr, 16));
      } catch (const std::exception&) {
        throw std::runtime_error("Malformed chunked response from the Docker daemon");
      }
      if (chunkSize == 0) {
        while (!readLine(pending, pipe, timeoutMs).empty()) {
        }  // drain trailing headers, if any, up to the final blank line
        break;
      }
      std::string data = takeBytes(pending, pipe, timeoutMs, chunkSize);
      if (!data.empty()) onChunk(data.data(), data.size());
      takeBytes(pending, pipe, timeoutMs, 2);  // trailing CRLF after each chunk's data
    }
  } else if (head.hasContentLength) {
    size_t remaining = head.contentLength;
    if (!pending.empty()) {
      size_t take = std::min(remaining, pending.size());
      if (take > 0) onChunk(pending.data(), take);
      remaining -= take;
    }
    std::string chunk;
    while (remaining > 0 && readSome(pipe, timeoutMs, chunk)) {
      size_t take = std::min(remaining, chunk.size());
      if (take > 0) onChunk(chunk.data(), take);
      remaining -= take;
      // Any bytes beyond `take` would belong to a pipelined response, which never
      // happens here (Connection: close, one request per pipe handle) — ignored.
    }
  } else {
    // No framing given at all — read until the daemon closes the connection.
    if (!pending.empty()) onChunk(pending.data(), pending.size());
    std::string chunk;
    while (readSome(pipe, timeoutMs, chunk)) {
      if (!chunk.empty()) onChunk(chunk.data(), chunk.size());
    }
  }

  return head.status;
}

long performRequest(const std::string& method, const std::string& path, const std::string& body,
                     const std::vector<std::string>& headers, DWORD timeoutMs,
                     const std::function<void(const char*, size_t)>& onChunk) {
  HANDLE pipe = connectDockerPipe();
  PipeHandle guard{pipe};
  std::string req = buildRequest(method, path, body, headers);
  writeAll(pipe, req.data(), req.size());
  ResponseHead head = readResponseHead(pipe, timeoutMs);
  return deliverBody(pipe, timeoutMs, head, onChunk);
}

}  // namespace

// `unixSocketPath` is accepted for interface parity with the unix build but
// unused — the Docker Desktop pipe path is fixed, not configurable per-instance.
HttpClient::HttpClient(std::string unixSocketPath) : socketPath_(std::move(unixSocketPath)) {}

HttpResponse HttpClient::request(const std::string& method, const std::string& path, const std::string& body,
                                  const std::vector<std::string>& headers, long timeoutSeconds) {
  HttpResponse response;
  try {
    DWORD timeoutMs = timeoutSeconds <= 0 ? 0 : static_cast<DWORD>(timeoutSeconds) * 1000;
    response.status = performRequest(method, path, body, headers, timeoutMs,
                                      [&response](const char* data, size_t len) { response.body.append(data, len); });
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Request to the Docker daemon failed: ") + e.what() +
                              " (is Docker Desktop running?)");
  }
  return response;
}

long HttpClient::streamRequest(const std::string& method, const std::string& path, const std::string& body,
                                const std::vector<std::string>& headers,
                                const std::function<void(const char*, size_t)>& onChunk) {
  try {
    // Builds and running containers can legitimately take many minutes — no
    // timeout; the user can Ctrl-C if something is genuinely stuck.
    return performRequest(method, path, body, headers, 0, onChunk);
  } catch (const std::exception& e) {
    throw std::runtime_error(std::string("Streaming request to the Docker daemon failed: ") + e.what());
  }
}

}  // namespace ebl
