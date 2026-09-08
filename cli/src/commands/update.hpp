#pragma once

namespace ebl::commands {

/** `ebl update [options]`. argv/argc are the arguments *after* the "update"
 * subcommand token. Returns the process exit code. */
int runUpdate(int argc, char** argv);

void printUpdateUsage();

}  // namespace ebl::commands
