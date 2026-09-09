#include "../src/json.hpp"
#include "test_framework.hpp"

using ebl::Json;
using ebl::JsonError;

EBL_TEST(parses_flat_object) {
  Json j = Json::parse(R"({"name":"my-app","version":"1.2.3","private":true})");
  EBL_CHECK(j.isObject());
  EBL_CHECK_EQ(j.get("name").asString(), std::string("my-app"));
  EBL_CHECK_EQ(j.get("version").asString(), std::string("1.2.3"));
  EBL_CHECK(j.get("private").asBool(false));
}

EBL_TEST(parses_nested_object) {
  Json j = Json::parse(R"({"build":{"preview":{},"production":{}}})");
  Json build = j.get("build");
  EBL_CHECK(build.isObject());
  EBL_CHECK_EQ(build.members().size(), static_cast<size_t>(2));
}

EBL_TEST(missing_key_get_returns_null_not_throw) {
  Json j = Json::parse(R"({"a":1})");
  Json missing = j.get("b");
  EBL_CHECK(missing.isNull());
  EBL_CHECK_EQ(missing.asString("fallback"), std::string("fallback"));
}

EBL_TEST(at_throws_on_missing_key) {
  Json j = Json::parse(R"({"a":1})");
  bool threw = false;
  try {
    j.at("nope");
  } catch (const std::exception&) {
    threw = true;
  }
  EBL_CHECK(threw);
}

EBL_TEST(malformed_json_throws_json_error) {
  bool threw = false;
  try {
    Json::parse("{not valid json");
  } catch (const JsonError&) {
    threw = true;
  }
  EBL_CHECK(threw);
}

EBL_TEST(handles_escaped_strings) {
  Json j = Json::parse(R"({"path":"C:\\Users\\dev\\app","note":"line1\nline2"})");
  EBL_CHECK_EQ(j.get("path").asString(), std::string("C:\\Users\\dev\\app"));
  EBL_CHECK_EQ(j.get("note").asString(), std::string("line1\nline2"));
}

EBL_TEST(round_trips_object_through_dump_and_parse) {
  Json obj = Json::object();
  obj.set("owner", Json("project-cell"));
  obj.set("count", Json(3));
  obj.set("enabled", Json(true));
  std::string dumped = obj.dump();
  Json reparsed = Json::parse(dumped);
  EBL_CHECK_EQ(reparsed.get("owner").asString(), std::string("project-cell"));
  EBL_CHECK_EQ(reparsed.get("count").asInt(0), 3LL);
  EBL_CHECK(reparsed.get("enabled").asBool(false));
}

EBL_TEST(array_push_and_index) {
  Json arr = Json::array();
  arr.push_back(Json("preview"));
  arr.push_back(Json("production"));
  EBL_CHECK_EQ(arr.size(), static_cast<size_t>(2));
  EBL_CHECK_EQ(arr.at(0).asString(), std::string("preview"));
  EBL_CHECK_EQ(arr.at(1).asString(), std::string("production"));
}

EBL_TEST(number_precision_survives_int_round_trip) {
  Json j = Json::parse(R"({"versionCode":42})");
  EBL_CHECK_EQ(j.get("versionCode").asInt(0), 42LL);
}
