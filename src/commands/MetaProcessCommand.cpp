/**
 * @file MetaProcessCommand.cpp
 * @brief Implementation of the MetaProcessCommand for generating metadata from descriptor files.
 *
 * This file provides functionality to process descriptor files and metadata binary files
 * to generate structured metadata. This is typically used in metadata reconstruction
 * workflows where metadata needs to be built from raw descriptor data.
 */

#include "MetaProcessCommand.hpp"
#include "utils/common.hpp"
#include "processing/Meta_process.hpp"

REGISTER_COMMAND(MetaProcessCommand);

/**
 * @brief Constructs a MetaProcessCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
MetaProcessCommand::MetaProcessCommand(bool reg) : Command(reg, "meta_process", "Generate metadata") {
    m_parser.add_argument("filename1").help("Descriptor File");
    m_parser.add_argument("filename2").help("MetaBin File");
}

/**
 * @brief Processes descriptor and metadata binary files to generate metadata.
 *
 * Executes the metadata processing workflow which reads the descriptor file and
 * metadata binary file, processes them, and generates structured metadata output.
 *
 * @param device_path Path to the descriptor file.
 * @param log_path Path to the metadata binary file.
 */
void meta_process(std::string device_path, std::string log_path) {
    Metaprocess processor(device_path, log_path);
    processor.Execute();
    logger->info("Metadata processing completed successfully");
}

/**
 * @brief Executes the meta_process command to generate metadata.
 *
 * Retrieves the descriptor and metadata binary file paths from command-line arguments
 * and initiates the metadata processing workflow.
 *
 * @return EXIT_SUCCESS (0) on successful completion.
 */
int MetaProcessCommand::run() {
    const std::string fname = m_parser.get("filename1");
    const std::string lname = m_parser.get("filename2");
    //init_log(fname);
    meta_process(fname,lname);
    return 0;
}
