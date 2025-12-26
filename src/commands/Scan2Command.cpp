/**
 * @file Scan2Command.cpp
 * @brief Implementation of the Scan2Command for scanning VIB/VBK files for metadata blocks.
 *
 * This file provides the improved version (V2) of the scanning functionality that
 * searches through Veeam backup files to identify metadata blocks and optionally
 * data blocks. The scanner can start from a specified offset and is the default
 * scanning implementation (registered as "scan" command).
 */

#include "Scan2Command.hpp"
#include "utils/common.hpp"
#include "scanning/ScannerV2.hpp"

REGISTER_COMMAND(Scan2Command);

/**
 * @brief Constructs a Scan2Command with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
Scan2Command::Scan2Command(bool reg) : Command(reg, "scan", "scan for MD blocks") {
    m_parser.add_argument("filename").help("VIB/VBK file");
    m_parser.add_argument("-s", "--start").help("start offset (hex)").scan<'x', uint64_t>().default_value(uint64_t{0});

    auto &arg = m_parser.add_argument("--blocks").help("(or --data) find data blocks").default_value(false).implicit_value(true);
    m_parser.add_argument("--carve").help("carve multiple veeam backups from a disk.").default_value(false).implicit_value(true);
    m_parser.add_argument("--keysets").help("load keysets").default_value(std::string{});

    m_parser.add_hidden_alias_for(arg, "--data");
}

/**
 * @brief Executes the scan command to find metadata and data blocks in a VIB/VBK file.
 *
 * Initializes the V2 scanner with the specified file, start offset, and block scanning
 * options, then performs the scan operation. The scanner outputs found blocks to
 * appropriately named output files.
 *
 * @return EXIT_SUCCESS (0) on successful completion.
 */
int Scan2Command::run() {
    const std::string vbk_fname = m_parser.get("filename");
    init_log(vbk_fname);

    const size_t vbk_size = Reader::get_size(vbk_fname);
    logger->info("source vbk {} ({:x} = {})", vbk_fname, vbk_size, bytes2human(vbk_size));

    ScannerV2 scanner(
        vbk_fname,
        m_parser.get<uint64_t>("start"),
        m_parser.get<bool>("blocks"),
        m_parser.get<bool>("carve"),
        m_parser.get<std::string>("keysets")
    );
    scanner.scan();
    return 0;
}
