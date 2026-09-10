#include "build_progress_tracker.hpp"

#include <algorithm>
#include <array>
#include <regex>
#include <vector>

namespace ebl {

namespace {

constexpr std::array<const char*, 8> kPhaseOrder = {"setup", "install", "prebuild", "signing",
                                                     "gradle", "eas", "collect", "done"};

// Same weights/order as orchestrator/src/build/progress.ts's GRADLE_WEIGHTS/
// EAS_WEIGHTS - keep both in sync by hand if either changes.
constexpr std::array<int, 8> kGradleWeights = {2, 15, 10, 3, 65, 0, 5, 0};
constexpr std::array<int, 8> kEasWeights = {2, 15, 0, 3, 0, 75, 5, 0};

int phaseIndexOf(const std::string& id) {
  for (size_t i = 0; i < kPhaseOrder.size(); i++) {
    if (id == kPhaseOrder[i]) return static_cast<int>(i);
  }
  return -1;
}

struct EasMilestone {
  std::regex pattern;
  double fraction;
};

// Coarse, best-effort milestones within `eas build --local` output - eas-cli
// never prints a live percentage the way Gradle does, so this steps through
// recognizable phase-transition lines instead. Same list/order/fractions as
// progress.ts's EAS_MILESTONES.
const std::vector<EasMilestone>& easMilestones() {
  static const std::vector<EasMilestone> milestones = {
      {std::regex("Resolving credentials", std::regex::icase), 0.1},
      {std::regex("Compressing project", std::regex::icase), 0.2},
      {std::regex("Prebuild|Running.*prebuild", std::regex::icase), 0.35},
      {std::regex("Installing", std::regex::icase), 0.45},
      {std::regex("Running gradle|Gradle build", std::regex::icase), 0.55},
      {std::regex("BUILD SUCCESSFUL", std::regex::icase), 0.95},
  };
  return milestones;
}

const std::regex& gradleProgressRegex() {
  // Same pattern as progress.ts's GRADLE_PROGRESS_RE - matches Gradle's own
  // `--console=rich` live line, e.g. "<====-----> 63% EXECUTING".
  static const std::regex re(R"((\d{1,3})%\s+(EXECUTING|CONFIGURING|INITIALIZING))");
  return re;
}

}  // namespace

void BuildProgressTracker::setEngine(const std::string& engine) { engine_ = engine == "eas" ? "eas" : "gradle"; }

int BuildProgressTracker::baseForCurrentPhase() const {
  const auto& weights = engine_ == "eas" ? kEasWeights : kGradleWeights;
  int base = 0;
  for (int i = 0; i < phaseIndex_ && i < static_cast<int>(kPhaseOrder.size()); i++) {
    base += weights[static_cast<size_t>(i)];
  }
  return base;
}

int BuildProgressTracker::weightForCurrentPhase() const {
  if (phaseIndex_ < 0 || phaseIndex_ >= static_cast<int>(kPhaseOrder.size())) return 0;
  const auto& weights = engine_ == "eas" ? kEasWeights : kGradleWeights;
  return weights[static_cast<size_t>(phaseIndex_)];
}

int BuildProgressTracker::clampAdvance(double pct) {
  int rounded = static_cast<int>(pct + 0.5);
  percent_ = std::max(percent_, std::min(100, rounded));
  return percent_;
}

std::optional<int> BuildProgressTracker::handleLine(const std::string& line) {
  if (line.rfind("@@PHASE:", 0) == 0) {
    std::string rest = line.substr(8);
    size_t sep = rest.find(':');
    std::string id = sep == std::string::npos ? rest : rest.substr(0, sep);
    int idx = phaseIndexOf(id);
    if (idx < 0) return std::nullopt;  // unrecognized phase id - ignore rather than guess
    phaseIndex_ = idx;
    easMilestoneIdx_ = 0;
    return clampAdvance(static_cast<double>(baseForCurrentPhase()));
  }

  if (line.rfind("@@PROGRESS:", 0) == 0) {
    try {
      int within = std::stoi(line.substr(11));
      double fraction = static_cast<double>(std::max(0, std::min(100, within))) / 100.0;
      return clampAdvance(baseForCurrentPhase() + weightForCurrentPhase() * fraction);
    } catch (const std::exception&) {
      return std::nullopt;  // malformed - keep whatever was there
    }
  }

  bool inGradlePhase = engine_ == "gradle" && phaseIndex_ >= 0 &&
                        phaseIndex_ < static_cast<int>(kPhaseOrder.size()) &&
                        std::string(kPhaseOrder[static_cast<size_t>(phaseIndex_)]) == "gradle";
  if (inGradlePhase) {
    std::smatch m;
    if (std::regex_search(line, m, gradleProgressRegex())) {
      double fraction = std::stod(m[1].str()) / 100.0;
      return clampAdvance(baseForCurrentPhase() + weightForCurrentPhase() * fraction);
    }
    return std::nullopt;
  }

  bool inEasPhase = engine_ == "eas" && phaseIndex_ >= 0 && phaseIndex_ < static_cast<int>(kPhaseOrder.size()) &&
                     std::string(kPhaseOrder[static_cast<size_t>(phaseIndex_)]) == "eas";
  if (inEasPhase) {
    const auto& milestones = easMilestones();
    for (size_t i = easMilestoneIdx_; i < milestones.size(); i++) {
      if (std::regex_search(line, milestones[i].pattern)) {
        easMilestoneIdx_ = i + 1;
        return clampAdvance(baseForCurrentPhase() + weightForCurrentPhase() * milestones[i].fraction);
      }
    }
    return std::nullopt;
  }

  return std::nullopt;
}

}  // namespace ebl
