#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

#include "cli/command_parser.h"
#include "cli/executor.h"
#include "cli/renderer.h"

int main(int argc, char** argv) {
  try {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
      arguments.emplace_back(argv[index]);
    }
    const auto options = emberdb::cli::parseOptions(arguments);
    emberdb::cli::executeCommand(options, std::cout);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "emberdb: " << error.what() << '\n';
    emberdb::cli::printUsage(std::cerr);
    return 1;
  }
}
