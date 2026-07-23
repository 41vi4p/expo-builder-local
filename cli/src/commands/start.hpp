#pragma once

namespace ebl::commands {

int runStart(int argc, char** argv);
void printStartUsage();

int runStop(int argc, char** argv);
void printStopUsage();

}  // namespace ebl::commands
