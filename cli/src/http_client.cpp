#include "http_client.hpp"

#include <curl/curl.h>

#include <cctype>
#include <stdexcept>

namespace ebl {

namespace {

size_t bufferWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t streamWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* cb = static_cast<const std::function<void(const char*, size_t)>*>(userdata);
  (*cb)(ptr, size * nmemb);
  return size * nmemb;
}

/** Builds a configured (but not yet executed) curl easy handle for one request.
 * `*headerListOut` is populated so the caller can free it after curl_easy_perform. */
CURL* makeHandle(const std::string& socketPath, const std::string& method, const std::string& path,
                  const std::string& body, const std::vector<std::string>& headers, curl_slist** headerListOut) {
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("Failed to initialize a libcurl handle");

  curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socketPath.c_str());
  // Docker's daemon speaks plain HTTP/1.1 over the unix socket; the host/scheme in
  // the URL is a formality libcurl needs but the daemon itself ignores.
  std::string url = "http://localhost" + path;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  if (method == "GET") {
    // no-op, GET is curl's default method
  } else if (method == "POST") {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  } else {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (!body.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
  }

  curl_slist* headerList = nullptr;
  for (const auto& h : headers) headerList = curl_slist_append(headerList, h.c_str());
  if (headerList) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
  *headerListOut = headerList;

  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  return curl;
}

}  // namespace

HttpClient::HttpClient(std::string unixSocketPath) : socketPath_(std::move(unixSocketPath)) {}

HttpResponse HttpClient::request(const std::string& method, const std::string& path, const std::string& body,
                                  const std::vector<std::string>& headers, long timeoutSeconds) {
  curl_slist* headerList = nullptr;
  CURL* curl = makeHandle(socketPath_, method, path, body, headers, &headerList);

  HttpResponse response;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, bufferWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    std::string err = curl_easy_strerror(res);
    curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);
    throw std::runtime_error("Request to the Docker daemon failed: " + err +
                             " (is Docker running, and is the socket path correct?)");
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  response.status = status;

  curl_slist_free_all(headerList);
  curl_easy_cleanup(curl);
  return response;
}

long HttpClient::streamRequest(const std::string& method, const std::string& path, const std::string& body,
                                const std::vector<std::string>& headers,
                                const std::function<void(const char*, size_t)>& onChunk) {
  curl_slist* headerList = nullptr;
  CURL* curl = makeHandle(socketPath_, method, path, body, headers, &headerList);

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &onChunk);
  // Builds and running containers can legitimately take many minutes — no timeout;
  // the user can Ctrl-C if something is genuinely stuck.
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);

  CURLcode res = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

  curl_slist_free_all(headerList);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    throw std::runtime_error("Streaming request to the Docker daemon failed: " +
                              std::string(curl_easy_strerror(res)));
  }
  return status;
}

std::string urlEncode(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

}  // namespace ebl
