/**
 * @file Process_carver.cpp
 * @brief Implementation of metadata processor for reconstructing metadata from carved data.
 *
 * This file provides functionality to process carved metadata offset files, read the
 * corresponding metadata pages from device/VBK files, validate and filter metadata
 * pages, and output reconstructed metadata to both CSV descriptor files and binary
 * metadata files. It handles metadata bank structures, page validation, and
 * deduplication.
 */

#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <openssl/evp.h>
#include "Veeam/VBK.hpp"

#include "Process_carver.hpp"

using namespace Veeam::VBK;

/**
 * @brief Constructs a MetadataProcessor for carved metadata reconstruction.
 *
 * Initializes the processor with device and log paths, opens output files for
 * descriptor CSV and metadata binary output, and prepares buffers for processing.
 *
 * @param device_path Path to the device or VBK file containing metadata.
 * @param log_path Path to the carved metadata offset log file.
 * @param alignment Alignment offset to apply when reading from device.
 */
MetadataProcessor::MetadataProcessor(const std::string& device_path,
                                   const std::string& log_path,
                                   uint64_t alignment)
    : m_device_path(device_path)
    , m_log_path(log_path)
    , m_alignment(alignment)
    , m_device_handle(-1)
    , m_initialized(false) {
    
    m_buffer.resize(MAX_BANK_SIZE * 128);
    
    initReferenceBuffer();
    m_reference_buffer.resize(PAGE_SIZE);
    std::fill(m_reference_buffer.begin(), m_reference_buffer.end(), 0);

    m_found_offsets.reserve(1024); 

    std::string descriptor_path = remove_extension(log_path) + "-Descriptor.csv";
    std::string metadata_path = remove_extension(log_path) + "-MetaData.bin";

    m_descriptor_file.open(descriptor_path);
    if (!m_descriptor_file.is_open()) {
        std::cerr << "Failed to open descriptor file: " << descriptor_path << std::endl;
        return;
    }

    m_metadata_file.open(metadata_path, std::ios::binary);
    if (!m_metadata_file.is_open()) {
        std::cerr << "Failed to open metadata file: " << metadata_path << std::endl;
        return;
    }

    m_initialized = true;
}

MetadataProcessor::~MetadataProcessor() {
    if (m_device_handle >= 0) {
        close(m_device_handle);
    }
    if (m_descriptor_file.is_open()) {
        m_descriptor_file.close();
    }
    if (m_metadata_file.is_open()) {
        m_metadata_file.close();
    }
}

bool MetadataProcessor::openDevice() {
    #ifdef _WIN32
        m_device_handle = _open(m_device_path.c_str(), _O_RDONLY | _O_BINARY);
    #else
        m_device_handle = open(m_device_path.c_str(), O_RDONLY);
    #endif
    //m_device_handle = open(m_device_path.c_str(), O_RDONLY);
    if (m_device_handle < 0) {
        std::cerr << "Failed to open device: " << m_device_path << " (errno: " << errno << ")" << std::endl;
        return false;
    }
    return true;
}

std::string MetadataProcessor::remove_extension(const std::string& path) {
    size_t last_dot = path.find_last_of(".");
    if (last_dot != std::string::npos) {
        return path.substr(0, last_dot);
    }
    return path;
}

std::vector<std::string> MetadataProcessor::szGetCsvN(const std::string& sz) {
    std::vector<std::string> ot;
    std::string szBuf;
    
    for (size_t i = 0; i < sz.length(); i++) {
        if ((sz[i] == ';') || (i == sz.length() - 1)) {
            if ((sz[i] != ';')) {
                szBuf += sz[i];
            }
            ot.push_back(szBuf);
            szBuf.clear();
        } else {
            szBuf += sz[i];
        }
    }
    return ot;
}

bool MetadataProcessor::processLog() {
    if (!m_initialized) {
        std::cerr << "Processor not initialized correctly" << std::endl;
        return false;
    }

    if (!openDevice()) {
        return false;
    }

    std::ifstream log_file(m_log_path);
    if (!log_file) {
        std::cerr << "Failed to open log file: " << m_log_path << std::endl;
        return false;
    }

    std::string line;
    uint64_t line_number = 0;
    while (std::getline(log_file, line)) {
        line_number++;
        if (!line.empty()) {
            if (!processLogEntry(line)) {
                std::cerr << "Failed to process line " << line_number << ": " << line << std::endl;
            }
        }
    }

    return true;
}

bool MetadataProcessor::processLogEntry(const std::string& log_line) {
    auto ot = szGetCsvN(log_line);
    
    if (ot.size() != 2) {
        return true;
    }
    
    if (ot[0][0] != 'M') {
        return true;
    }

    uint64_t qOffset;
    try {
        qOffset = std::stoull(ot[1], nullptr, 16);
    } catch (const std::exception& e) {
        std::cerr << "Invalid hex offset: " << ot[1] << std::endl;
        return false;
    }

    qOffset += m_alignment;

    // Check if the offset has already been processed
    for (const auto& found : m_found_offsets) {
        if (qOffset >= static_cast<uint64_t>(found.offset) &&
            qOffset <= static_cast<uint64_t>(found.offset + found.size - 1)) {
            return true;
        }
    }

    qOffset = (qOffset + PAGE_SIZE) & ~0xFFF;

    std::cout << "Processing log entry with offset 0x" << std::hex 
              << qOffset << std::dec << std::endl;

    return searchMetadata(static_cast<int64_t>(qOffset));
}

bool MetadataProcessor::isOffsetProcessed(int64_t offset) const {
    for (const auto& found : m_found_offsets) {
        if (offset >= found.offset && 
            offset <= (found.offset + found.size - 1)) {
            return true;
        }
    }
    return false;
}

bool MetadataProcessor::writeMetadata(const std::vector<uint8_t>& metadata) {
    if (!m_metadata_file.is_open()) {
        return false;
    }

    m_metadata_file.write(reinterpret_cast<const char*>(metadata.data()), 
                         metadata.size());
    m_metadata_file.flush();
    return true;
}

bool MetadataProcessor::searchMetadata(int64_t target_offset) {
    int64_t inHPos = target_offset;
    int64_t searchStart = inHPos - (int64_t)(MAX_BANK_SIZE * 64);
    if (searchStart < 0) searchStart = 0;
    
    if (!readFromDevice(searchStart, m_buffer)) {
        std::cerr << "Failed to read from device at offset 0x" 
                  << std::hex << searchStart << std::endl;
        return false;
    }

    int64_t hPos = searchStart + (int64_t)(MAX_BANK_SIZE * 128);
    int64_t baseRead = searchStart;
    bool found = false;

    while (hPos > (inHPos - (int64_t)(MAX_BANK_SIZE * 64))) {
        // Check hPos e fPos
        if (hPos < 0) {
            break;
        }

        hPos -= 0x200;
        int64_t fPos = (hPos - baseRead);

        if (fPos < 0 || fPos >= (int64_t)m_buffer.size() - MAX_BANK_SIZE) {
            continue;
        }

        if (isOffsetProcessed(hPos)) {
            continue;
        }

        if (fPos + sizeof(MetadataHeader) > m_buffer.size()) {
            continue;
        }

        if (fPos + 2 >= (int64_t)m_buffer.size()) {
            continue;
        }

        uint8_t mdType = m_buffer[fPos + 2];
        m_buffer[fPos + 2] = 0;

        if (fPos + PAGE_SIZE > m_buffer.size()) {
            m_buffer[fPos + 2] = mdType;
            continue;
        }

        std::vector<uint8_t> hBuf;
        try {
            hBuf.assign(m_buffer.begin() + fPos, 
                       m_buffer.begin() + fPos + PAGE_SIZE);
        } catch (const std::exception& e) {
            std::cerr << "Buffer copy failed at offset 0x" << std::hex << fPos 
                      << ": " << e.what() << std::endl;
            continue;
        }
        
        for (size_t i = 2; i < hBuf.size(); i++) {
            hBuf[i] = m_buffer[fPos + i] & 0xFE;
        }

        m_buffer[fPos + 2] = mdType;

        if (fPos + sizeof(uint16_t) > m_buffer.size()) {
            continue;
        }

        uint16_t* mdSize = reinterpret_cast<uint16_t*>(&m_buffer[fPos]);

        if (std::memcmp(&hBuf[2], &m_reference_buffer[2], PAGE_SIZE - 2) == 0 && 
            *mdSize <= 0x800 && *mdSize > 0 && mdType == 0) {
            
            uint32_t currMetaSize = (*mdSize << 12) | 0x2000;

            if (fPos + currMetaSize > (int64_t)m_buffer.size()) {
                continue;
            }

            buf_t metadata;
            try {
                metadata.assign(m_buffer.begin() + fPos, m_buffer.begin() + fPos + currMetaSize);
            } catch (const std::exception& e) {
                std::cerr << "Metadata copy failed: " << e.what() << std::endl;
                continue;
            }
            
            auto md5_hash = m_md5.Calculate(metadata);
            auto meta_offset = m_metadata_file.tellp();

            if (!writeMetadata(metadata) || !writeDescriptor(hPos, meta_offset, currMetaSize, md5_hash)) {
                std::cerr << "Failed to write metadata at offset 0x" << std::hex << hPos << std::endl;
                continue;
            }
            
            std::cout << "Found Meta at 0x" << std::hex << hPos 
                      << ", size 0x" << currMetaSize << std::endl;
            
            FoundMetadata found_meta{hPos, currMetaSize};
            m_found_offsets.push_back(found_meta);
            
            found = true;
        }
    }

    if (inHPos >= MAX_BANK_SIZE * 64) {
        FoundMetadata searched_region{
            inHPos - (MAX_BANK_SIZE * 64),
            MAX_BANK_SIZE * 128
        };
        m_found_offsets.push_back(searched_region);
    }

    return found;
}

bool MetadataProcessor::readFromDevice(int64_t offset, std::vector<uint8_t>& buffer) {
    if (lseek(m_device_handle, offset, SEEK_SET) != offset) {
        std::cerr << "Failed to seek to offset 0x" << std::hex << offset 
                  << " (errno: " << std::dec << errno << ")" << std::endl;
        return false;
    }

    size_t bytes_read = 0;
    while (bytes_read < buffer.size()) {
        ssize_t result = read(m_device_handle, 
                            buffer.data() + bytes_read, 
                            buffer.size() - bytes_read);
        if (result <= 0) {
            if (result < 0) {
                std::cerr << "Read error at offset 0x" << std::hex 
                         << (offset + bytes_read) 
                         << " (errno: " << std::dec << errno << ")" << std::endl;
            }
            return false;
        }
        bytes_read += result;
    }
    return true;
}

bool MetadataProcessor::writeDescriptor(int64_t offset, int64_t meta_offset, uint32_t size, const digest_t& md5) {
    if (!m_descriptor_file.is_open()) {
        return false;
    }

    // Format: META;offset;meta_offset;size;MD5
    m_descriptor_file << "META;" 
                     << std::hex << std::uppercase 
                     << std::setw(8) << std::setfill('0') << offset << ";"
                     << std::setw(8) << std::setfill('0') << meta_offset << ";"
                     << std::hex << std::uppercase << size << ";";
                    // << std::setw(8) << std::setfill('0') << size << ";";

    for (uint8_t byte : md5) {
        m_descriptor_file << std::hex << std::uppercase 
                         << std::setw(2) << std::setfill('0') 
                         << static_cast<int>(byte);
    }
    m_descriptor_file << std::endl;
    return true;
}
