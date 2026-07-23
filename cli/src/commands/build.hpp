#pragma once

namespace ebl::commands {

/** `ebl build [path] [options]`. argv/argc are the arguments *after* the "build"
 * subcommand token (argv[0] is the first real option/positional, not the program
 * name). Returns the process exit code. */
int runBuild(int argc, char** argv);

void printBuildUsage();

}  // namespace ebl::commands
