#pragma once
// Small interactive-prompt helpers shared by any command that needs to ask the user
// something at a terminal (currently `ebl config`'s wizard and `ebl build`'s
// missing-token prompt).
#include <string>

namespace ebl {

/** Prints `question [defaultValue]: `, returns the typed line or defaultValue if
 * the user just pressed Enter. */
std::string promptString(const std::string& question, const std::string& defaultValue);

int promptInt(const std::string& question, int defaultValue);

/** Like promptString but with terminal echo disabled — used for anything
 * token/password-shaped. Falls back to a normal (echoed) read if stdin isn't
 * actually a terminal (e.g. piped input in a script). */
std::string promptHidden(const std::string& question);

}  // namespace ebl
