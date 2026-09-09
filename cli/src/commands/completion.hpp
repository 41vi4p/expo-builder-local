#pragma once

namespace ebl::commands {

/** `ebl completion <bash|zsh|fish|powershell>`. argv/argc are the arguments *after*
 * the "completion" subcommand token. Prints the requested shell's completion script
 * to stdout (nothing else - meant to be `eval`/`source`d directly, e.g.
 * `source <(ebl completion bash)`) and returns the process exit code. */
int runCompletion(int argc, char** argv);

void printCompletionUsage();

}  // namespace ebl::commands
