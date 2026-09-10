#include "json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ebl {

bool Json::contains(const std::string& key) const {
  if (type_ != Type::Object) return false;
  for (const auto& [k, v] : obj_) {
    if (k == key) return true;
  }
  return false;
}

const Json& Json::at(const std::string& key) const {
  if (type_ != Type::Object) throw JsonError("Json::at(key) called on a non-object value");
  for (const auto& [k, v] : obj_) {
    if (k == key) return v;
  }
  throw JsonError("Missing key: " + key);
}

Json Json::get(const std::string& key) const {
  if (type_ != Type::Object) return Json();
  for (const auto& [k, v] : obj_) {
    if (k == key) return v;
  }
  return Json();
}

void Json::set(const std::string& key, Json value) {
  if (type_ == Type::Null) type_ = Type::Object;
  if (type_ != Type::Object) throw JsonError("Json::set(key, value) called on a non-object value");
  for (auto& [k, v] : obj_) {
    if (k == key) {
      v = std::move(value);
      return;
    }
  }
  obj_.emplace_back(key, std::move(value));
}

size_t Json::size() const {
  if (type_ == Type::Array) return arr_.size();
  if (type_ == Type::Object) return obj_.size();
  return 0;
}

const Json& Json::at(size_t idx) const {
  if (type_ != Type::Array) throw JsonError("Json::at(index) called on a non-array value");
  if (idx >= arr_.size()) throw JsonError("Array index out of range");
  return arr_[idx];
}

void Json::push_back(Json value) {
  if (type_ == Type::Null) type_ = Type::Array;
  if (type_ != Type::Array) throw JsonError("Json::push_back called on a non-array value");
  arr_.push_back(std::move(value));
}

std::string Json::asString(const std::string& def) const { return type_ == Type::String ? str_ : def; }
double Json::asDouble(double def) const { return type_ == Type::Number ? num_ : def; }
long long Json::asInt(long long def) const { return type_ == Type::Number ? static_cast<long long>(num_) : def; }
bool Json::asBool(bool def) const { return type_ == Type::Boolean ? bool_ : def; }

// --- serialization -----------------------------------------------------------------

static void appendUtf8(std::string& out, unsigned int cp) {
  if (cp <= 0x7F) {
    out += static_cast<char>(cp);
  } else if (cp <= 0x7FF) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

static void escapeStringTo(const std::string& s, std::string& out) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

static void dumpNumber(double n, std::string& out) {
  if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 1e15) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(n));
    out += buf;
  } else {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", n);
    out += buf;
  }
}

static void dumpTo(const Json& j, std::string& out) {
  switch (j.type()) {
    case Json::Type::Null:
      out += "null";
      break;
    case Json::Type::Boolean:
      out += j.asBool() ? "true" : "false";
      break;
    case Json::Type::Number:
      dumpNumber(j.asDouble(), out);
      break;
    case Json::Type::String:
      escapeStringTo(j.asString(), out);
      break;
    case Json::Type::Array: {
      out += '[';
      bool first = true;
      for (const auto& item : j.items()) {
        if (!first) out += ',';
        first = false;
        dumpTo(item, out);
      }
      out += ']';
      break;
    }
    case Json::Type::Object: {
      out += '{';
      bool first = true;
      for (const auto& [key, value] : j.members()) {
        if (!first) out += ',';
        first = false;
        escapeStringTo(key, out);
        out += ':';
        dumpTo(value, out);
      }
      out += '}';
      break;
    }
  }
}

std::string Json::dump() const {
  std::string out;
  dumpTo(*this, out);
  return out;
}

// --- parsing -----------------------------------------------------------------------

namespace {

class Parser {
public:
  explicit Parser(const std::string& text) : s_(text) {}

  Json parseValue() {
    skipWs();
    char c = peek();
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') return Json(parseString());
    if (c == 't') { expectLiteral("true"); return Json(true); }
    if (c == 'f') { expectLiteral("false"); return Json(false); }
    if (c == 'n') { expectLiteral("null"); return Json(nullptr); }
    return parseNumber();
  }

private:
  const std::string& s_;
  size_t i_ = 0;

  void skipWs() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) i_++;
  }
  char peek() {
    if (i_ >= s_.size()) throw JsonError("Unexpected end of JSON input");
    return s_[i_];
  }
  char next() {
    if (i_ >= s_.size()) throw JsonError("Unexpected end of JSON input");
    return s_[i_++];
  }
  void expect(char c) {
    char got = next();
    if (got != c) throw JsonError(std::string("Expected '") + c + "' but got '" + got + "'");
  }
  void expectLiteral(const char* lit) {
    size_t len = std::strlen(lit);
    if (s_.compare(i_, len, lit) != 0) throw JsonError(std::string("Invalid literal, expected ") + lit);
    i_ += len;
  }

  std::string parseString() {
    expect('"');
    std::string out;
    while (true) {
      if (i_ >= s_.size()) throw JsonError("Unterminated string");
      char c = s_[i_++];
      if (c == '"') break;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i_ >= s_.size()) throw JsonError("Unterminated escape sequence");
      char e = s_[i_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned int cp = parseHex4();
          if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 1 < s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
            i_ += 2;
            unsigned int low = parseHex4();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
          appendUtf8(out, cp);
          break;
        }
        default:
          throw JsonError("Invalid escape character in string");
      }
    }
    return out;
  }

  unsigned int parseHex4() {
    if (i_ + 4 > s_.size()) throw JsonError("Invalid \\u escape");
    unsigned int cp = static_cast<unsigned int>(std::stoul(s_.substr(i_, 4), nullptr, 16));
    i_ += 4;
    return cp;
  }

  Json parseNumber() {
    size_t start = i_;
    if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) i_++;
    while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) i_++;
    if (i_ < s_.size() && s_[i_] == '.') {
      i_++;
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) i_++;
    }
    if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
      i_++;
      if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) i_++;
      while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) i_++;
    }
    if (i_ == start) throw JsonError("Invalid number literal");
    return Json(std::stod(s_.substr(start, i_ - start)));
  }

  Json parseArray() {
    expect('[');
    Json arr = Json::array();
    skipWs();
    if (peek() == ']') {
      i_++;
      return arr;
    }
    while (true) {
      arr.push_back(parseValue());
      skipWs();
      char c = next();
      if (c == ']') break;
      if (c != ',') throw JsonError("Expected ',' or ']' in array");
      skipWs();
    }
    return arr;
  }

  Json parseObject() {
    expect('{');
    Json obj = Json::object();
    skipWs();
    if (peek() == '}') {
      i_++;
      return obj;
    }
    while (true) {
      skipWs();
      std::string key = parseString();
      skipWs();
      expect(':');
      Json value = parseValue();
      obj.set(key, std::move(value));
      skipWs();
      char c = next();
      if (c == '}') break;
      if (c != ',') throw JsonError("Expected ',' or '}' in object");
    }
    return obj;
  }
};

}  // namespace

Json Json::parse(const std::string& text) {
  Parser parser(text);
  return parser.parseValue();
}

}  // namespace ebl
