/**
 * @file BlocksCommand.cpp
 * @brief Implementation of the BlocksCommand for extracting LZ4 compressed blocks from VIB/VBK files.
 *
 * This file provides functionality to scan Veeam backup files for LZ4 compressed blocks,
 * decompress them, verify their CRC32 checksums, and extract them to an output file.
 * Each block is processed and written to a binary output file with fixed block sizes.
 */

#include "BlocksCommand.hpp"
#include "utils/common.hpp"
#include "core/structs.hpp"
#include <lz4.h>

extern "C" {
    uint32_t vcrc32(uint32_t crc, const void *buf, unsigned int len);
}

REGISTER_COMMAND(BlocksCommand);

/**
 * @brief Constructs a BlocksCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
BlocksCommand::BlocksCommand(bool reg) : Command(reg, "blocks", "extract all blocks from VIB/VBK") {
    m_parser.add_argument("filename").help("VIB/VBK file");
}

/**
 * @brief Scans a VIB/VBK file to locate all LZ4 compressed blocks.
 *
 * This function reads the entire file in chunks and searches for LZ4 block signatures
 * (LZ_START_MAGIC). When a valid signature is found, it extracts the block metadata
 * including position, CRC, and source size.
 *
 * @param f File pointer to an open VIB/VBK file positioned at the beginning.
 * @return Vector of BlockStruct containing position, CRC, and size information for each found block.
 */
std::vector<BlockStruct> vbListBlocks(FILE* f){
    std::vector<BlockStruct> blocks;
    blocks.reserve(0x1000);

    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    const size_t bufsize = 0x8000000;
    char* buf = (char*)malloc(bufsize);
    uint64_t hs = 0;
    while( hs < fsize ) {
        size_t lzsz = (hs + bufsize > fsize) ? (fsize - hs) : bufsize;
        size_t nread = fread(buf, 1, lzsz, f);
        if( nread != lzsz ){
            logger->warn("nread {:x} != lzsz {:x}", nread, lzsz);
        }
        for( size_t i = 0; i < nread; i++ ) {
            // XXX no need to check every byte, values are aligned to 4, or even to 0x1000 bytes
            if( *(uint32_t*)(buf+i) == LZ_START_MAGIC && buf[i+11] == 0 ) {
                blocks.emplace_back(BlockStruct{hs+i, *(uint32_t*)(buf+i+4), *(uint32_t*)(buf+i+8)});
            }
        }
        hs += nread;
    }

    free(buf);
    return blocks;
}

/**
 * @brief Extracts and decompresses all LZ4 blocks from a VIB/VBK file.
 *
 * This function scans the input file for all LZ4 blocks, decompresses each block,
 * verifies the CRC32 checksum, and writes the decompressed data to an output file.
 * Each block is written with a fixed BLOCK_SIZE regardless of actual data size.
 * Blocks that fail validation are logged but skipped.
 *
 * @param fname Path to the VIB/VBK file to process.
 */
void extract_all_blocks(std::string fname){
    FILE* f = fopen(fname.c_str(), "rb");
    auto ofname = get_out_pathname(fname, "blocks.bin");
    std::ofstream of(ofname, std::ios::binary);
    if( !of ){
        logger->critical("{}: {}", ofname, strerror(errno));
        exit(1);
    }

    fseek(f, 0, SEEK_END);
    size_t fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    const size_t bufsize = 0x1600000;
    char* unpBuf = (char*)malloc(bufsize);
    char* unpBuf2 = (char*)malloc(bufsize);

    auto blocks = vbListBlocks(f);
    logger->info("Found {} blocks", blocks.size());
    for( size_t i = 0; i < blocks.size(); i++ ) {
        size_t ppSize = (i+1 < blocks.size()) ? blocks[i+1].pos - blocks[i].pos : fsize - blocks[i].pos;
        const auto& b = blocks[i];
        if( b.srcSize > 0 && b.srcSize <= BLOCK_SIZE && ppSize < bufsize ) {
            fseek(f, b.pos, SEEK_SET);
            size_t nread = fread(unpBuf, 1, ppSize, f);
            if( nread != ppSize ){
                logger->warn("nread {:x} != ppSize {:x}", nread, ppSize);
            }
            memset(unpBuf2, 0, bufsize);
            int lz4res = LZ4_decompress_safe(unpBuf+0x0c, unpBuf2, nread, bufsize); // XXX lz4res is not checked in the original code
            uint32_t crc = vcrc32(0, unpBuf2, b.srcSize);
            logger->info("{:8x}: crc {}{:08x}{} size {:8x} ppSize {:8x} lz4res {}",
                b.pos,
                b.crc == crc ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,
                b.crc,
                ANSI_COLOR_RESET,
                b.srcSize,
                ppSize,
                lz4res
                );
            of.write(unpBuf2, BLOCK_SIZE);
        } else {
            logger->warn("{}[-] {:8x}: crc {:8x} size {:8x}{}", ANSI_COLOR_RED, b.pos, b.crc, b.srcSize, ANSI_COLOR_RESET);
        }
    }

    free(unpBuf);
    free(unpBuf2);
    fclose(f);
}

/**
 * @brief Executes the blocks command to extract all blocks from the specified file.
 *
 * Initializes logging and calls extract_all_blocks() to perform the extraction.
 *
 * @return EXIT_SUCCESS (0) on successful completion.
 */
int BlocksCommand::run() {
    const std::string fname = m_parser.get("filename");
    init_log(fname);
    extract_all_blocks(fname);
    return 0;
}
