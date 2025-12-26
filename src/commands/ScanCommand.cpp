/**
 * @file ScanCommand.cpp
 * @brief Implementation of the legacy ScanCommand for scanning VIB/VBK files.
 *
 * This file provides the original (legacy) version of the scanning functionality
 * for identifying metadata blocks in Veeam backup files. This version is retained
 * for compatibility and comparison purposes. New code should use Scan2Command instead.
 */

#include "ScanCommand.hpp"
#include "utils/common.hpp"
#include "scanning/Scanner.hpp"

REGISTER_COMMAND(ScanCommand);

/**
 * @brief Constructs a ScanCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
ScanCommand::ScanCommand(bool reg) : Command(reg, "legacy_scan", "scan for MD blocks (legacy)") {
    m_parser.add_argument("filename").help("VIB/VBK file");
    m_parser.add_argument("-s", "--start").help("start offset (hex)").scan<'x', uint64_t>().default_value(uint64_t{0});
}

/**
 * @brief Executes the legacy scan command to find metadata blocks.
 *
 * Initializes the legacy scanner with the specified file and start offset,
 * then performs the scan operation using the original scanning algorithm.
 *
 * @return EXIT_SUCCESS (0) on successful completion.
 */
int ScanCommand::run() {
    const std::string fname = m_parser.get("filename");
    init_log(fname);
    Scanner scanner(fname, m_parser.get<uint64_t>("start"));
    scanner.scan();
    return 0;
}
