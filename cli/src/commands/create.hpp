#pragma once

namespace ebl::commands {

/** `ebl create <path> <name> [options]`. argv/argc are the arguments *after* the
 * "create" subcommand token. Returns the process exit code. */
int runCreate(int argc, char** argv);

void printCreateUsage();

}  // namespace ebl::commands
