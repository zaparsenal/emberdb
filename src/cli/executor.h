#pragma once

#include <iosfwd>

#include "cli/command.h"

namespace emberdb::cli {

void executeCommand(const Options& options, std::ostream& output);

}  // namespace emberdb::cli
