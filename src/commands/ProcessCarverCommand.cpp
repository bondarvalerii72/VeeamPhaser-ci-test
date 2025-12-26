/**
 * @file ProcessCarverCommand.cpp
 * @brief Implementation of the ProcessCarverCommand for processing carved metadata offsets.
 *
 * This file provides functionality to process metadata offset files that have been
 * carved from VIB/VBK files. It takes a metadata offset file and an alignment value
 * to reconstruct metadata structures from carved data.
 */

#include "ProcessCarverCommand.hpp"
#include "utils/common.hpp"
#include "processing/Process_carver.hpp"

REGISTER_COMMAND(ProcessCarverCommand);

/**
 * @brief Constructs a ProcessCarverCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
ProcessCarverCommand::ProcessCarverCommand(bool reg) : Command(reg, "process_carver", "Process metadata offsets") {
    m_parser.add_argument("filename1").help("VIB/VBK file");
    m_parser.add_argument("filename2").help("Offset metadata File");
    m_parser.add_argument("filename3").help("offset");
}

/**
 * @brief Processes carved metadata using offset information and alignment.
 *
 * This function initializes the metadata processor with the device path, metadata log path,
 * and alignment value. It processes the metadata log to reconstruct metadata structures.
 * The function exits on failure.
 *
 * @param device_path Path to the VIB/VBK device file.
 * @param log_path Path to the metadata offset log file.
 * @param aln Hexadecimal string representing the alignment offset.
 */
void process_carver(std::string device_path, std::string log_path, std::string aln) {
    uint64_t alignment = std::stoull(aln, nullptr, 16);;
    MetadataProcessor processor(device_path, log_path, alignment);

    if (!processor.processLog()) {
        logger->info("Failed to process metadata log");
        exit(1);
    }
    logger->info("Metadata generating completed successfully");
}

/**
 * @brief Executes the process_carver command to reconstruct metadata.
 *
 * Retrieves the VIB/VBK file path, metadata offset file path, and alignment offset
 * from command-line arguments, initializes logging, and starts the metadata
 * processing workflow.
 *
 * @return EXIT_SUCCESS (0) on successful completion.
 */
int ProcessCarverCommand::run() {
    const std::string fname = m_parser.get("filename1");
    const std::string lname = m_parser.get("filename2");
    const std::string offset = m_parser.get("filename3");
    init_log(fname);
    process_carver(fname,lname,offset);
    return 0;
}
