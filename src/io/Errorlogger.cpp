/**
 * @file Errorlogger.cpp
 * @brief Implementation of ErrorLogger for tracking block processing errors.
 *
 * This file provides functionality to log and persist error records during block
 * processing operations. Error records track failed block decompression or validation,
 * storing block metadata for later analysis or repair operations.
 */

#include "Errorlogger.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

/**
 * @brief Constructs an ErrorLogger with the specified output file.
 * @param filename Path where error records will be saved.
 */
ErrorLogger::ErrorLogger(const std::string& filename)
    : outputFilename(filename)
{
}

/**
 * @brief Adds an error record to the logger.
 *
 * @param offset Block offset in the source file.
 * @param filePos Position within the reconstructed file.
 * @param uncompSize Uncompressed block size.
 * @param errorCode Error code identifying the type of failure.
 * @param hash MD5 hash of the block.
 */
void ErrorLogger::addRecord(uint32_t offset, uint32_t filePos, uint32_t uncompSize, char errorCode, const digest_t hash) {
    ErrorRecord record;
    record.offset = offset;
    record.filePos = filePos;
    record.uncompSize = uncompSize;
    record.errorCode = errorCode;
    record.hash = hash;
    records.push_back(record);
}

/**
 * @brief Saves all error records to the output file in binary format.
 * @return True if successfully saved, false on file error.
 */
bool ErrorLogger::saveToFile() const {
    std::ofstream ofs(outputFilename, std::ios::binary);
    if (!ofs) {
        std::cerr << "Error opening the file " << outputFilename << " for writing." << std::endl;
        return false;
    }

    uint32_t count = static_cast<uint32_t>(records.size());
    ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

    if (!records.empty()) {
        ofs.write(reinterpret_cast<const char*>(records.data()), records.size() * sizeof(ErrorRecord));
    }

    ofs.close();
    return true;
}

/**
 * @brief Loads error records from the output file.
 * @return True if successfully loaded, false on file error.
 */
bool ErrorLogger::loadFromFile() {
    std::ifstream ifs(outputFilename, std::ios::binary);
    if (!ifs) {
        std::cerr << "Error opening the file " << outputFilename << " for reading." << std::endl;
        return false;
    }

    uint32_t count = 0;
    ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
    records.resize(count);
    if (count > 0) {
        ifs.read(reinterpret_cast<char*>(records.data()), count * sizeof(ErrorRecord));
    }

    ifs.close();
    return true;
}

/**
 * @brief Clears all error records from memory.
 */
void ErrorLogger::clearRecords() {
    records.clear();
}
