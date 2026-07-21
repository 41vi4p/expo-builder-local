#pragma once
// Thin HTTP-over-Unix-socket client used to talk to the Docker Engine API at
// /var/run/docker.sock. libcurl has built-in support for connecting over a unix
// socket (CURLOPT_UNIX_SOCKET_PATH) while still speaking plain HTTP/1.1 to a fake
// "http://localhost" URL, which is exactly the mechanism the real `docker` CLI itself
// relies on — no hand-rolled socket/HTTP framing needed here.
//
// Caller must call curl_global_init(CURL_GLOBAL_DEFAULT) once at process startup
// (main.cpp does this) before constructing an HttpClient.
#include <functional>
#include <string>
#include <vector>

namespace ebl {

struct HttpResponse {
  long status = 0;
  std::string body;
};

class HttpClient {
public:
  explicit HttpClient(std::string unixSocketPath);

  /** Buffered request/response, for calls with a bounded response size (image list,
   * container create/start/remove, volume create). `timeoutSeconds` of 0 means no
   * timeout — used for /containers/{id}/wait, which blocks until the build finishes. */
  HttpResponse request(const std::string& method, const std::string& path, const std::string& body = "",
                        const std::vector<std::string>& headers = {}, long timeoutSeconds = 120);

  /** Streaming response: onChunk fires as bytes arrive over the (potentially
   * long-lived) connection — used for the build and attach endpoints, both of which
   * can run for many minutes. Returns the final HTTP status code. */
  long streamRequest(const std::string& method, const std::string& path, const std::string& body,
                      const std::vector<std::string>& headers,
                      const std::function<void(const char*, size_t)>& onChunk);

private:
  std::string socketPath_;
};

/** Percent-encodes everything except unreserved characters (RFC 3986) — used for
 * query-string values like the image tag or a JSON `filters` blob. */
std::string urlEncode(const std::string& value);

}  // namespace ebl
