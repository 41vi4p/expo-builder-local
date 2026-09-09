#include "../src/version_compare.hpp"
#include "test_framework.hpp"

using ebl::isVersionNewer;

EBL_TEST(minor_version_ten_beats_nine_numerically_not_lexically) {
  // The classic string-vs-numeric trap: "0.10.0" < "0.9.0" as raw strings, but
  // 0.10.0 is the actually-newer release.
  EBL_CHECK(isVersionNewer("0.10.0", "0.9.0"));
  EBL_CHECK(!isVersionNewer("0.9.0", "0.10.0"));
}

EBL_TEST(patch_version_ten_beats_nine_numerically_not_lexically) {
  EBL_CHECK(isVersionNewer("1.2.10", "1.2.9"));
  EBL_CHECK(!isVersionNewer("1.2.9", "1.2.10"));
}

EBL_TEST(leading_v_is_tolerated_on_either_side) {
  EBL_CHECK(isVersionNewer("v1.0.0", "0.9.0"));
  EBL_CHECK(isVersionNewer("1.0.0", "v0.9.0"));
  EBL_CHECK(isVersionNewer("v1.0.0", "v0.9.0"));
}

EBL_TEST(equal_versions_are_not_newer) {
  EBL_CHECK(!isVersionNewer("1.2.3", "1.2.3"));
  EBL_CHECK(!isVersionNewer("v1.2.3", "1.2.3"));
}

EBL_TEST(older_version_is_not_newer) {
  EBL_CHECK(!isVersionNewer("0.9.0", "1.0.0"));
}

EBL_TEST(missing_trailing_component_treated_as_zero) {
  // "1.2" vs "1.2.0" - a shorter version string isn't automatically "older"; a
  // missing component compares as 0, same as semver's own convention.
  EBL_CHECK(!isVersionNewer("1.2", "1.2.0"));
  EBL_CHECK(isVersionNewer("1.3", "1.2.9"));
}
