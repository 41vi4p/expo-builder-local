#pragma once

namespace ebl::commands {

/** `ebl clean [options]`. argv/argc are the arguments *after* the "clean"
 * subcommand token. Returns the process exit code. */
int runClean(int argc, char** argv);

void printCleanUsage();

}  // namespace ebl::commands
