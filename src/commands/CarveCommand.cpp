/**
 * @file CarveCommand.cpp
 * @brief Implementation of the CarveCommand for carving data blocks from VIB/VBK files.
 *
 * This file provides functionality to scan and carve data blocks and empty blocks
 * from Veeam backup files. The carving process identifies block structures and
 * extracts them, outputting the results to a CSV file for further analysis.
 */

#include "CarveCommand.hpp"
#include "utils/common.hpp"
#include "processing/Carver.hpp"
#include "io/Reader.hpp"

REGISTER_COMMAND(CarveCommand);

/**
 * @brief Constructs a CarveCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
CarveCommand::CarveCommand(bool reg) : Command(reg, "carve", "Block Carver. (Data blocks | Empty blocks)") {
    m_parser.add_argument("filename").help("VIB/VBK file");
}

/**
 * @brief Executes the carve command to extract block information from a VIB/VBK file.
 *
 * This function initializes the carver, opens the input file and output files,
 * processes the entire file to identify and carve blocks, and logs statistics
 * about the carving operation.
 *
 * @return EXIT_SUCCESS (0) on successful completion, EXIT_FAILURE (1) on error.
 */
int CarveCommand::run() {
    const std::string fname = m_parser.get("filename");
    init_log(fname);

    const size_t vbk_size = Reader::get_size(fname);
    logger->info("source: {} ({:x} = {})", fname, vbk_size, bytes2human(vbk_size));

    Carver carver;
    int64_t offset = 0;
    std::string output = get_out_pathname(fname, "carved_blocks.csv").string();

    if (!carver.OpenInput(fname, offset)) {
        logger->critical("{}: {}", fname, strerror(errno));
        exit(1);
    }
        
    if (!carver.OpenOutputFiles(output)) {
        logger->critical("{}: {}", output, strerror(errno));
        exit(1);
    }
        
    carver.Process();
    for (const auto& stat : carver.stats()) {
        logger->info("{}", stat);
    }
    return 0;
}
