/**
 * @file RepoCommand.cpp
 * @brief Implementation of the RepoCommand for repository indexing, hash searching, and repair.
 *
 * This file provides functionality to index VIB/VBK repository files into a searchable
 * database, search for specific block hashes, and repair corrupted files using the
 * indexed repository. The repository database enables efficient lookup of blocks by
 * their MD5 hashes for data recovery and repair operations.
 */

#include "RepoCommand.hpp"
#include "data/RepoIndexer.hpp"
#include "utils/common.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>

REGISTER_COMMAND(RepoCommand);

/**
 * @brief Constructs a RepoCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
RepoCommand::RepoCommand(bool reg) : Command(reg, "repo", "Repository Indexer, Hash Search(debug) and Repair") {
    m_parser.add_argument("file")
            .help("File to index")
            .default_value(std::string(""));
    m_parser.add_argument("-m")
            .help("Indexing mode: 0 for LZ4 block, 1 for flat")
            .default_value(std::string(""));
    m_parser.add_argument("-l")
            .help("Repository database file (default: repo.dat)")
            .default_value(std::string("repo.dat"));
    m_parser.add_argument("-p")
            .help("Search for hash (32 hexadecimal characters)")
            .default_value(std::string(""));
    m_parser.add_argument("-e")
            .help("Error offset file (repair)")
            .default_value(std::string(""));
    m_parser.add_argument("-t")
            .help("Target file for repair")
            .default_value(std::string(""));
}

/**
 * @brief Executes the repo command for indexing, searching, or repairing.
 *
 * This function handles three main operations:
 * 1. Repair mode (-e and -t): Repairs a target file using error offset information
 * 2. Search mode (-p): Searches for a specific MD5 hash in the repository
 * 3. Index mode (-m and file): Indexes a VIB/VBK file into the repository database
 *
 * @return EXIT_SUCCESS (0) on successful completion, EXIT_FAILURE (1) on error.
 */
int RepoCommand::run() {
    std::string repoFile = m_parser.get("-l");
    if (repoFile.empty()) {
        repoFile = "repo.dat";
    }

    RepoIndexer indexer(repoFile);

    std::string errorFile = m_parser.get("-e");
    std::string targetFile = m_parser.get("-t");
    if (!errorFile.empty() && !targetFile.empty()) {
        std::cout << "Running repair with error file: " << errorFile 
                  << " and target file: " << targetFile << std::endl;
        if (!indexer.repairFile(errorFile, targetFile)) {
            std::cerr << "Repair failed." << std::endl;
            return 1;
        }
        return 0;
    }

    std::string searchHash = m_parser.get("-p");
    if (!searchHash.empty()) {
        if (searchHash.length() != 32) {
            std::cerr << "Invalid hash. It must be 32 hexadecimal characters long." << std::endl;
            return 1;
        }
        uint8_t hashBytes[MD5::DIGEST_LENGTH];
        for (size_t i = 0; i < 16; i++) {
            std::string byteStr = searchHash.substr(i * 2, 2);
            hashBytes[i] = static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16));
        }
        digest_t hash = *(digest_t*)hashBytes;
        std::string filePath;
        uint64_t offset;
        uint32_t compLenp;
        uint32_t unCompLenp;
        if (indexer.findHash(hash, filePath, offset, compLenp, unCompLenp)) {
            std::cout << "Hash found: " << searchHash << std::endl;
            std::cout << "File: " << filePath << std::endl;
            std::cout << "Offset: " << offset << std::endl;
        } else {
            std::cout << "Hash not found: " << searchHash << std::endl;
        }
        return 0;
    }

    std::string modeStr = m_parser.get("-m");
    if (modeStr.empty()) {
        std::cerr << "Error: Mode (-m) not specified. Use 0 for LZ4 block or 1 for flat." << std::endl;
        return 1;
    }
    int mode = std::atoi(modeStr.c_str());
    if (mode != 0 && mode != 1) {
        std::cerr << "Error: Invalid mode. Use 0 for LZ4 block or 1 for flat." << std::endl;
        return 1;
    }

    std::string file = m_parser.get("file");
    if (file.empty()) {
        std::cerr << "Error: No file provided for indexing." << std::endl;
        return 1;
    }

    std::vector<std::string> files;
    files.push_back(file);
    indexer.indexFiles(mode, files);
    return 0;
}
