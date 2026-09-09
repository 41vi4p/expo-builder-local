#include <filesystem>
#include <fstream>
#include <random>

#include "../src/detect.hpp"
#include "test_framework.hpp"

namespace fs = std::filesystem;

namespace {

fs::path makeTempDir() {
  std::random_device rd;
  fs::path dir = fs::temp_directory_path() / ("ebl-test-" + std::to_string(rd()));
  fs::create_directories(dir);
  return dir;
}

void writeFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  out << content;
}

}  // namespace

EBL_TEST(detects_valid_expo_project) {
  fs::path dir = makeTempDir();
  writeFile(dir / "package.json", R"({"name":"my-app","version":"1.0.0","dependencies":{"expo":"^52.0.0"}})");
  writeFile(dir / "app.json", R"({"expo":{"owner":"project-cell"}})");
  writeFile(dir / "eas.json", R"({"build":{"preview":{},"production":{}}})");

  ebl::ExpoProjectInfo info = ebl::detectExpoProject(dir.string());
  EBL_CHECK(info.isExpoProject);
  EBL_CHECK_EQ(info.name, std::string("my-app"));
  EBL_CHECK_EQ(info.version, std::string("1.0.0"));
  EBL_CHECK_EQ(info.owner, std::string("project-cell"));
  EBL_CHECK_EQ(info.easProfiles.size(), static_cast<size_t>(2));

  fs::remove_all(dir);
}

EBL_TEST(rejects_missing_package_json) {
  fs::path dir = makeTempDir();
  ebl::ExpoProjectInfo info = ebl::detectExpoProject(dir.string());
  EBL_CHECK(!info.isExpoProject);
  EBL_CHECK(!info.reason.empty());
  fs::remove_all(dir);
}

EBL_TEST(rejects_non_expo_package_json) {
  fs::path dir = makeTempDir();
  writeFile(dir / "package.json", R"({"name":"not-expo","dependencies":{"react":"^18.0.0"}})");
  ebl::ExpoProjectInfo info = ebl::detectExpoProject(dir.string());
  EBL_CHECK(!info.isExpoProject);
  fs::remove_all(dir);
}

EBL_TEST(accepts_expo_as_dev_dependency) {
  fs::path dir = makeTempDir();
  writeFile(dir / "package.json", R"({"name":"my-app","devDependencies":{"expo":"^52.0.0"}})");
  ebl::ExpoProjectInfo info = ebl::detectExpoProject(dir.string());
  EBL_CHECK(info.isExpoProject);
  fs::remove_all(dir);
}

EBL_TEST(tolerates_malformed_app_json_without_failing_detection) {
  fs::path dir = makeTempDir();
  writeFile(dir / "package.json", R"({"name":"my-app","dependencies":{"expo":"^52.0.0"}})");
  writeFile(dir / "app.json", "{not valid json");
  ebl::ExpoProjectInfo info = ebl::detectExpoProject(dir.string());
  EBL_CHECK(info.isExpoProject);
  EBL_CHECK(info.owner.empty());
  fs::remove_all(dir);
}

EBL_TEST(rejects_malformed_package_json) {
  fs::path dir = makeTempDir();
  writeFile(dir / "package.json", "{not valid json");
  ebl::ExpoProjectInfo info = ebl::detectExpoProject(dir.string());
  EBL_CHECK(!info.isExpoProject);
  fs::remove_all(dir);
}
