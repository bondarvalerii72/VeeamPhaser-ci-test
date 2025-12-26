#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <fstream>
#include "MD5.hpp"

struct MetadataHeader {
    uint16_t size;
    uint8_t type;  // 0 = normal, 1 = unneeded, 2 = encrypted
    uint8_t reserved;
};

struct FoundMetadata {
    int64_t offset;
    uint32_t size;
};

class MetadataProcessor {
public:
    MetadataProcessor(const std::string& device_path, 
                     const std::string& log_path,
                     uint64_t alignment);
    ~MetadataProcessor();

    bool processLog();

private:
    bool openDevice();
    bool processLogEntry(const std::string& log_line);
    bool searchMetadata(int64_t base_offset);
    bool readFromDevice(int64_t offset, std::vector<uint8_t>& buffer);
    bool isOffsetProcessed(int64_t offset) const;
    bool writeDescriptor(int64_t offset, int64_t meta_offset, uint32_t size, const digest_t& md5);
    bool writeMetadata(const std::vector<uint8_t>& metadata);
    std::string remove_extension(const std::string& path);
    std::vector<std::string> szGetCsvN(const std::string& sz);
    std::string m_device_path;
    std::string m_log_path;
    uint64_t m_alignment;
    int m_device_handle;
    std::vector<FoundMetadata> m_found_offsets;
    std::ofstream m_descriptor_file;
    std::ofstream m_metadata_file;
    bool m_initialized;

    static constexpr size_t REFERENCE_BUFFER_SIZE = 0x1000;
    std::vector<uint8_t> m_buffer;
    std::vector<uint8_t> m_reference_buffer;
    MD5 m_md5;
    
    void initReferenceBuffer() {
        m_reference_buffer.resize(REFERENCE_BUFFER_SIZE);
        std::fill(m_reference_buffer.begin(), m_reference_buffer.end(), 0);
    }
};
