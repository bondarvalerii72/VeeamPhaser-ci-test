/**
 * @file main.cpp
 * @brief Main entry point for VeeamPhaser application.
 *
 * This file contains the main function that handles command-line parsing,
 * command registration, logging initialization, and command execution.
 * It implements implicit command selection (vbk as default), command aliases
 * (scan2 -> scan), and automatic self-testing before command execution.
 */

#include <argparse/argparse.hpp>

#include "utils/common.hpp"
#include "../dist/version.h"

#include "commands/TestCommand.hpp"

#ifdef __WIN32__
#include <windows.h>
/**
 * @brief Enables ANSI escape code support in Windows console.
 *
 * Required for colored output on Windows 10+. No-op on other platforms.
 */
void enable_ansi_escape_codes() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
/**
 * @brief No-op on Unix platforms (ANSI codes already supported).
 */
void enable_ansi_escape_codes() {}
#endif

extern argparse::ArgumentParser program;
extern int verbosity;

/**
 * @brief Main entry point for VeeamPhaser application.
 *
 * Handles:
 * - Signal handler registration for crash dumps
 * - ANSI escape code enablement for colored output
 * - Command-line argument parsing with implicit command support
 * - Command alias handling (scan2 -> scan)
 * - Automatic self-testing before command execution
 * - Logging initialization and configuration
 * - Command execution and error handling
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return EXIT_SUCCESS (0) on success, EXIT_FAILURE (1) on error.
 */
int main(int argc, char*argv[]) {
    signal(SIGSEGV, signal_handler);
    signal(SIGABRT, signal_handler);

    enable_ansi_escape_codes();
    register_program_args(program);

    std::map<std::string, std::unique_ptr<argparse::ArgumentParser>> subparsers;

    for (const auto& [name, cmd] : Command::registry()) {
        program.add_subparser(cmd->parser());
    }

    try {
        std::vector<std::string> unknown_args = program.parse_known_args(argc, argv); // doesnt raise error on unknown args
        const bool no_subcommand_used = std::all_of(Command::registry().begin(), Command::registry().end(), [&](const auto& cmd) { return !program.is_subcommand_used(cmd.first); } );
        if( unknown_args.size() > 0 ){
            if( no_subcommand_used && unknown_args[0] == "scan2" ){
                // "scan2" is an alias for "scan"
                unknown_args[0] = "scan";
                unknown_args.insert(unknown_args.begin(), argv[0]);
                program.parse_args(unknown_args); // raises error on unknown args
            } else if( no_subcommand_used && std::filesystem::exists(unknown_args[0]) ){
                // no subcommand used => implicit "vbk" command
                unknown_args.insert(unknown_args.begin(), "vbk");
                unknown_args.insert(unknown_args.begin(), argv[0]);
                program.parse_args(unknown_args); // raises error on unknown args
            } else {
                std::cerr << "[?] Unknown arguments: ";
                for (const auto& arg : unknown_args) {
                    std::cerr << "\"" << arg << "\" ";
                }
                std::cerr << std::endl;
                std::exit(1);
            }
        }
    } catch (const std::exception& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    logger->set_arguments(argc, argv);
    logger->set_banner(APP_NAME " " APP_VERSION);
    logger->set_verbosity(verbosity); // should be before logger->add_file() call
    logger->set_dedup_limit(program.get<int>("--log-dedup-limit"));
    if( program.is_used("--log") ){
        init_log("", program.get<std::string>("--log"));
    }

    auto& selfTestCmd = Command::registry()[TEST_CMD_NAME];
    for (const auto& [name, cmd] : Command::registry()) {
        if (program.is_subcommand_used(name)) {
            if( name == TEST_CMD_NAME ){
                // explicit self-test, make it visible
                logger->set_verbosity(9);
            } else {
                // implicit self-test, make it silent
                if( selfTestCmd->run() != 0 ){
                    logger->critical("self-test failed, exiting");
                    exit(1);
                }
            }

            if( cmd->parser().is_used("--log") ){
                init_log("", cmd->parser().get<std::string>("--log"));
            }
            // warning: only use if all your loggers are thread-safe ("_mt" loggers)
            spdlog::flush_every(std::chrono::seconds(5));

            return cmd->run();
        }
    }

    std::cout << program;
    return 0;
}
