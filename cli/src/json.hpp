#pragma once
// A small, dependency-free JSON value type: just enough parsing and serialization to
// talk to the Docker Engine API and read package.json/eas.json/build.gradle-adjacent
// data. Not a general-purpose JSON library — no streaming parser, no comments/trailing
// commas, strict RFC 8259 grammar only. Objects preserve insertion order (Docker's API
// doesn't care, but it makes debugging output readable).
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ebl {

class JsonError : public std::runtime_error {
public:
  explicit JsonError(const std::string& msg) : std::runtime_error(msg) {}
};

class Json {
public:
  enum class Type { Null, Boolean, Number, String, Array, Object };
  using Array = std::vector<Json>;
  using Object = std::vector<std::pair<std::string, Json>>;

  Json() : type_(Type::Null) {}
  Json(std::nullptr_t) : type_(Type::Null) {}
  Json(bool b) : type_(Type::Boolean), bool_(b) {}
  Json(int n) : type_(Type::Number), num_(static_cast<double>(n)) {}
  Json(long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
  Json(long long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
  Json(unsigned int n) : type_(Type::Number), num_(static_cast<double>(n)) {}
  Json(double n) : type_(Type::Number), num_(n) {}
  Json(const std::string& s) : type_(Type::String), str_(s) {}
  Json(const char* s) : type_(Type::String), str_(s) {}

  static Json array() {
    Json j;
    j.type_ = Type::Array;
    return j;
  }
  static Json object() {
    Json j;
    j.type_ = Type::Object;
    return j;
  }
  static Json parse(const std::string& text);

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isObject() const { return type_ == Type::Object; }
  bool isArray() const { return type_ == Type::Array; }
  bool isString() const { return type_ == Type::String; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isBool() const { return type_ == Type::Boolean; }

  bool contains(const std::string& key) const;
  const Json& at(const std::string& key) const;   // throws if missing
  Json get(const std::string& key) const;          // returns Null if missing
  void set(const std::string& key, Json value);

  size_t size() const;
  const Json& at(size_t idx) const;
  void push_back(Json value);
  const Array& items() const { return arr_; }
  const Object& members() const { return obj_; }

  std::string asString(const std::string& def = "") const;
  double asDouble(double def = 0.0) const;
  long long asInt(long long def = 0) const;
  bool asBool(bool def = false) const;

  std::string dump() const;

private:
  Type type_;
  bool bool_ = false;
  double num_ = 0.0;
  std::string str_;
  Array arr_;
  Object obj_;
};

}  // namespace ebl
