// Platform-independent pieces of http_client.hpp: httpGetTcp (plain TCP HTTP,
// libcurl handles this fine on every platform) and urlEncode. Compiled on both
// Windows and non-Windows; the Docker-socket-specific HttpClient methods live in
// http_client_unix.cpp / http_client_win.cpp instead — see http_client.hpp.
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

}  // namespace

HttpResponse httpGetTcp(const std::string& url, long timeoutSeconds) {
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("Failed to initialize a libcurl handle");

  HttpResponse response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, bufferWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  CURLcode res = curl_easy_perform(curl);
  if (res == CURLE_OK) {
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    response.status = status;
  }  // else: leave status at 0 — connection refused/timed out, treated as "not up yet"
  curl_easy_cleanup(curl);
  return response;
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
