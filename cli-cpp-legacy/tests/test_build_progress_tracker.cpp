#include "../src/build_progress_tracker.hpp"
#include "test_framework.hpp"

using ebl::BuildProgressTracker;

EBL_TEST(phase_marker_jumps_to_that_phase_base_weight) {
  BuildProgressTracker t;
  t.setEngine("gradle");
  // Gradle weights: setup=2, install=15, prebuild=10, signing=3, gradle=65, ...
  auto p1 = t.handleLine("@@PHASE:setup:Starting");
  EBL_CHECK(p1.has_value());
  EBL_CHECK_EQ(*p1, 0);  // base for phase index 0 is 0 (nothing before it)

  auto p2 = t.handleLine("@@PHASE:install:Installing deps");
  EBL_CHECK(p2.has_value());
  EBL_CHECK_EQ(*p2, 2);  // base = setup's weight (2)

  auto p3 = t.handleLine("@@PHASE:prebuild:Prebuilding");
  EBL_CHECK(p3.has_value());
  EBL_CHECK_EQ(*p3, 17);  // base = setup+install = 2+15

  auto p4 = t.handleLine("@@PHASE:gradle:Running gradle");
  EBL_CHECK(p4.has_value());
  EBL_CHECK_EQ(*p4, 30);  // base = setup+install+prebuild+signing = 2+15+10+3
}

EBL_TEST(progress_marker_blends_within_current_phase_budget) {
  BuildProgressTracker t;
  t.setEngine("gradle");
  t.handleLine("@@PHASE:install:Installing");  // base=2, weight=15
  auto half = t.handleLine("@@PROGRESS:50");
  EBL_CHECK(half.has_value());
  EBL_CHECK_EQ(*half, 10);  // 2 + round(15*0.5) = 2 + round(7.5) = 2 + 8 = 10

  auto full = t.handleLine("@@PROGRESS:100");
  EBL_CHECK(full.has_value());
  EBL_CHECK_EQ(*full, 2 + 15);
}

EBL_TEST(gradle_console_rich_line_advances_within_gradle_phase) {
  BuildProgressTracker t;
  t.setEngine("gradle");
  t.handleLine("@@PHASE:gradle:Running gradle");  // base = 30, weight = 65
  auto pct = t.handleLine("<=====-----> 50% EXECUTING");
  EBL_CHECK(pct.has_value());
  EBL_CHECK_EQ(*pct, 63);  // 30 + round(65*0.5) = 30 + round(32.5) = 30 + 33 = 63 (round-half-up)

  // A later, higher reading advances further.
  auto pct2 = t.handleLine("<==========> 90% EXECUTING");
  EBL_CHECK(pct2.has_value());
  EBL_CHECK_EQ(*pct2, 89);  // 30 + round(65*0.9) = 30 + round(58.5) = 30 + 59 = 89
}

EBL_TEST(gradle_regex_ignored_outside_the_gradle_phase) {
  BuildProgressTracker t;
  t.setEngine("gradle");
  t.handleLine("@@PHASE:install:Installing");
  auto pct = t.handleLine("<=====-----> 50% EXECUTING");
  EBL_CHECK(!pct.has_value());
}

EBL_TEST(eas_milestones_step_forward_in_order) {
  BuildProgressTracker t;
  t.setEngine("eas");
  // EAS weights: setup=2, install=15, prebuild=0, signing=3, gradle=0, eas=75, ...
  t.handleLine("@@PHASE:eas:Running eas build");  // base = 2+15+0+3 = 20, weight = 75

  auto m1 = t.handleLine("Resolving credentials for build");
  EBL_CHECK(m1.has_value());
  EBL_CHECK_EQ(*m1, 20 + 8);  // 20 + round(75*0.1) = 20+7.5 -> 28 (round-half-up) => 20+8=28

  auto m2 = t.handleLine("Compressing project files");
  EBL_CHECK(m2.has_value());
  EBL_CHECK_EQ(*m2, 20 + 15);  // 20 + round(75*0.2) = 20+15 = 35

  auto m3 = t.handleLine("BUILD SUCCESSFUL in 3m");
  EBL_CHECK(m3.has_value());
  EBL_CHECK_EQ(*m3, 20 + 71);  // 20 + round(75*0.95) = 20+71.25 -> 91 (round-half-up) => 20+71=91
}

EBL_TEST(eas_milestones_never_go_backward_even_if_a_line_matches_an_earlier_one_again) {
  BuildProgressTracker t;
  t.setEngine("eas");
  t.handleLine("@@PHASE:eas:Running eas build");
  auto m1 = t.handleLine("Compressing project files");
  EBL_CHECK(m1.has_value());
  int afterCompress = *m1;

  // The tracker only scans forward from the last matched milestone index, so an
  // earlier milestone string appearing again (e.g. in unrelated log noise)
  // doesn't match and doesn't move the percent backward.
  auto m2 = t.handleLine("Resolving credentials again for retry");
  EBL_CHECK(!m2.has_value());

  auto m3 = t.handleLine("Installing node_modules");
  EBL_CHECK(m3.has_value());
  EBL_CHECK(*m3 >= afterCompress);
}

EBL_TEST(percent_never_decreases_even_if_a_later_phase_marker_implies_less) {
  BuildProgressTracker t;
  t.setEngine("gradle");
  t.handleLine("@@PHASE:gradle:Running gradle");  // base = 30
  t.handleLine("<==========> 90% EXECUTING");     // advances well past 30

  // A malformed/out-of-order @@PROGRESS: within the same phase must not pull the
  // overall percent back down.
  auto after = t.handleLine("@@PROGRESS:0");
  EBL_CHECK(after.has_value());
  EBL_CHECK(*after >= 30 + 58);  // still at or above the earlier 90%-implied value
}

EBL_TEST(unrelated_log_lines_return_nullopt) {
  BuildProgressTracker t;
  t.setEngine("gradle");
  t.handleLine("@@PHASE:setup:Starting");
  auto r = t.handleLine("some ordinary build tool output line");
  EBL_CHECK(!r.has_value());
}

EBL_TEST(unrecognized_phase_id_is_ignored_rather_than_guessed) {
  BuildProgressTracker t;
  t.setEngine("gradle");
  auto r = t.handleLine("@@PHASE:not-a-real-phase:Whatever");
  EBL_CHECK(!r.has_value());
}

EBL_TEST(malformed_progress_value_is_ignored) {
  BuildProgressTracker t;
  t.setEngine("gradle");
  t.handleLine("@@PHASE:install:Installing");
  auto r = t.handleLine("@@PROGRESS:not-a-number");
  EBL_CHECK(!r.has_value());
}

EBL_TEST(default_engine_is_gradle_when_setEngine_never_called) {
  BuildProgressTracker t;
  auto pct = t.handleLine("@@PHASE:gradle:Running gradle");
  EBL_CHECK(pct.has_value());
  auto live = t.handleLine("<=====-----> 20% EXECUTING");
  EBL_CHECK(live.has_value());
}

EBL_TEST(unknown_engine_string_falls_back_to_gradle) {
  BuildProgressTracker t;
  t.setEngine("something-else");
  t.handleLine("@@PHASE:eas:Running eas");  // gradle weight for "eas" phase index is 0
  auto pct = t.handleLine("@@PROGRESS:100");
  EBL_CHECK(pct.has_value());
  // Gradle's eas-phase weight is 0, so 100% within it adds nothing new.
  EBL_CHECK_EQ(*pct, 2 + 15 + 10 + 3 + 65);  // base for phase after gradle = sum of all gradle weights before eas
}
