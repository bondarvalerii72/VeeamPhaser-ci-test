/**
 * @file ExtractContext.cpp
 * @brief Implementation of file extraction context for reconstructing files from metadata.
 *
 * This file provides the core functionality for extracting files from Veeam metadata.
 * It handles block decompression, CRC validation, sparse file detection, differential
 * backups, and file reconstruction from both VBK files and carved data sources.
 * Supports multiple decompression formats (LZ4, ZLIB) and maintains statistics
 * about extraction success rates.
 */

#include "ExtractContext.hpp"
#include "io/Writer.hpp"

#include <lz4.h>
#include <zlib.h>
#include <zstd.h>
#include <algorithm>

extern int verbosity;

/**
 * @brief Constructs an extraction context with all necessary dependencies.
 *
 * Initializes the context by loading block descriptors from the metadata datastore.
 * The block descriptors map MD5 hashes to block locations, enabling file reconstruction.
 *
 * @param meta CMeta object containing file metadata.
 * @param vbkf Reader for the VBK file (may be nullptr if using external hash table).
 * @param exHT External hash table for carved data lookups.
 * @param device_files Vector of device file readers for carved data access.
 * @param cache Block cache for performance optimization.
 * @param prev_level Previous logging level for restoration.
 * @param level_changed Whether logging level was temporarily changed.
 */
ExtractContext::ExtractContext(CMeta& meta, std::unique_ptr<Reader> vbkf, const HashTable& exHT, std::vector<std::unique_ptr<Reader>>& device_files, cache_t& cache, const Logger::level prev_level, const bool level_changed)
    : meta(meta), vbkf(std::move(vbkf)), exHT(exHT), device_files(device_files), m_cache(cache) {

    // conditionally decrease verbosity
    logger->with_console_level( level_changed ? spdlog::level::critical : prev_level, [&](){
        bds = meta.read_datastore(DEFAULT_DATASTORE_PPI); // SnapshotDescriptor.DataStoreRootPage
        });

    logger->log(bds.size() == 0 ? spdlog::level::warn : spdlog::level::info, "Loaded {} BlockDescriptors from HT", bds.size());
}

ExtractContext::~ExtractContext() {
    if( bds.size() != used_bds.size() ){
        logger->info("used {} of {} BDs, unused: {}", used_bds.size(), bds.size(), (ssize_t)(bds.size() - used_bds.size()));
        if( xname.empty() ){
            logger->warn("{} of data is not claimed, some dir entries might be missing. try --deep option",
                bytes2human((bds.size() - used_bds.size()) * BLOCK_SIZE, "", BLOCK_SIZE));
        }
    }
}

void ExtractContext::process_file(const std::string& pathname, const CMeta::VFile& vFile, bool resume){
    if( vFile.is_dir() ){
        return;
    }
    if( needle_ppi.valid() ){
        if( vFile.attribs.ppi != needle_ppi ){
            return;
        }
    }
    else if( !xname.empty() ){
        if( xname_is_glob ) {
            if( !simple_glob_match(xname, pathname) ){
                return;
            }
        } else if( xname_is_full ){
            if( pathname != xname ){
                return;
            }
        } else {
            size_t pos = pathname.find_last_of('/');
            pos = (pos == std::string::npos) ? 0 : (pos + 1);
            if( pathname.substr(pos) != xname ){
                return;
            }
        }
    }

    found = true;

    FileTestInfo fti(vFile, pathname, md_fname);
    off_t actual_written = 0;
    fs::path out_fname;
    if( !test_only ){
        // get_out_pathname() will create directories if needed, so don't run it if we're only testing
        out_fname = get_out_pathname(md_fname, sanitize_fname(pathname));
    }

    logger->info("{} {} = {} blocks, {}",
        test_only ? "Testing" : "Extracting",
        vFile.name,
        vFile.attribs.nBlocks,
        bytes2human(vFile.attribs.filesize, " bytes")
        );
    
    //resume extraction
    bool should_truncate = !vFile.is_diff();
    size_t blocks_to_skip = 0; 

    if(resume){

        if (!test_only && fs::exists(out_fname)) {
            off_t existing_size = fs::file_size(out_fname);
            
            logger->info("Resume: existing file size = {} bytes", existing_size);
            logger->info("Resume: BLOCK_SIZE = {} bytes", BLOCK_SIZE);
            
            // blocks to skip (current block count - 2 blocks)
            size_t existing_blocks = existing_size / BLOCK_SIZE;
            blocks_to_skip = (existing_blocks >= 2) ? (existing_blocks - 2) : 0;
            
            logger->info("Resume: existing_blocks = {}, blocks_to_skip = {}", existing_blocks, blocks_to_skip);
            
            if (blocks_to_skip > 0) {
                should_truncate = false; //resume from the current extracted block
            }
            
            off_t resume_offset = blocks_to_skip * BLOCK_SIZE;
            
            logger->info("Resuming extraction: skipping {} blocks, overwriting last 2 blocks for alignment", 
                        blocks_to_skip);
            logger->info("Resume position: {} bytes", bytes2human(resume_offset, " bytes"));
            
            // resume counters: blocks, sparse blocks 
            size_t skipped_sparse = 0;
            VAllBlocks vAllB_temp = meta.get_file_blocks(vFile);
            for (size_t i = 0; i < blocks_to_skip && i < vAllB_temp.size(); i++) {
                if (vAllB_temp[i].is_empty()) {
                    skipped_sparse++;
                }
            }
            
            fti.nOK = blocks_to_skip - skipped_sparse; // OK blocks
            fti.sparse_blocks = skipped_sparse;
            
            logger->info("Resuming: initialized counters with {} OK blocks and {} sparse blocks", 
                        fti.nOK, fti.sparse_blocks);
        }
    } 

    std::optional<Writer> writer;
    if (!test_only) {
        if( vFile.is_diff() && !fs::exists(out_fname) ){
            logger->warn("{} type is \"{}\" but source doesn't exist", vFile.name, vFile.type_str());
        }
        writer.emplace(out_fname, should_truncate);
        
        if (blocks_to_skip > 0 && writer) {
            off_t resume_offset = blocks_to_skip * BLOCK_SIZE;
            writer->seek(resume_offset); // resume position
        }
    }

    // trace < debug < info < warn < error < critical < off
    if( test_only && logger->console_level() <= spdlog::level::info ){   
        need_table_header = true;
    }

    VAllBlocks vAllB = meta.get_file_blocks(vFile);

    buf_t lzBuf;
    buf_t lzBuf2;
    int64_t remaining_size = vFile.attribs.filesize;
    off_t prev_pos = 0;
    uint8_t prev_device_id = 255;

    if( vAllB.size() > (size_t)vFile.attribs.nBlocks ){
        logger->warn("vAllB.size() {:x} > vFile.attribs.nBlocks {:x}", vAllB.size(), vFile.attribs.nBlocks);
    } else {
        fti.nMissMD = vFile.attribs.nBlocks - vAllB.size();
    }

    for( size_t i=0; i<vAllB.size(); i++ ){
        // Skip blocks that were already processed (except last 2 for boundary alignment)
        if (i < blocks_to_skip) {
            continue;
        }
        
        if( remaining_size <= 0 ){
            logger->warn_once("Remaining size <= 0: {}", remaining_size);
        }
        size_t skip_size = 0;
        const VBlockDesc& blk = vAllB[i];
        logger->trace("Block #{:06x}: {}", i, blk.to_string());
        do {
            if( blk.is_empty() ) {
                // empty block, no need to lookup in any table
                fti.sparse_blocks++;
                skip_size = BLOCK_SIZE;
                break;
            }

            const auto it = bds.find(blk.hash);
            BlockDescriptor blkDesc;
            bool found_in_bds = (it != bds.end());
            
            if (found_in_bds){
                blkDesc = it->second;                
                used_bds.insert(blk.hash);
                
            } else if (exHT){
                // Block not in BDs, but we have exHT create minimal descriptor from VBlockDesc
                logger->debug("Block #{:x} not found in BDs, using exHT: {}", i, blk.to_string());
                blkDesc.location = BL_BLOCK_IN_BLOB;
                blkDesc.usageCnt = 0;
                blkDesc.offset = 0; 
                blkDesc.allocSize = 0;
                blkDesc.dedup = 0;
                blkDesc.digest = blk.hash;
                blkDesc.compType = CT_NONE;
                blkDesc.unused = 0;
                blkDesc.compSize = 0;
                blkDesc.srcSize = BLOCK_SIZE;
                blkDesc.keysetID = 0;
                // The actual values will be set in exHT below
            } else {
                // No BDs and no exHT cannot extract
                logger->warn("Block #{:x} not found in HT: {}", i, blk.to_string());
                fti.nMissHT++;
                if( blk.size != BLOCK_SIZE ){
                    logger->warn_once("blk.size {:x} != BLOCK_SIZE {:x}", blk.size, BLOCK_SIZE);
                }
                skip_size = BLOCK_SIZE;
                break;
            }
            
            if( writer ){
                if( vFile.is_diff() && blk.is_patch() ){
                    writer->seek(blk.vib_offset * BLOCK_SIZE);
                }
                logger->trace("write @ {:010x}: Block #{:06x}: {}", writer->tell(), i, blkDesc.to_string());
            } else {
                logger->trace("Block #{:06x}: {}", i, blkDesc.to_string());
            }

            off_t pos;
            uint8_t cur_device_id = 255;
            // these might be overriden if blkDesc compType = ZLIB, but we have found LZ4 block with the same MD5 hash, or vice versa
            ECompType effective_comp_type = blkDesc.compType;
            uint32_t effective_allocSize = blkDesc.allocSize;
            uint32_t effective_compSize = blkDesc.compSize;
            digest_t effective_keyset = blkDesc.keysetID;
            
            if (exHT){
                const auto data_block = exHT.findHash(blkDesc.digest);
                if(data_block){
                    logger->debug("exHT: {} -> {}", blkDesc.to_string(), data_block->to_string());
                    pos = data_block->offset;
                    effective_comp_type = data_block->comp_type;
                    effective_allocSize = data_block->comp_size + (effective_comp_type == CT_LZ4 ? sizeof(lz_hdr) : 0);
                    if (data_block->keyset_id) {
                        effective_allocSize = effective_allocSize + 0x10 - (effective_allocSize % 0x10);
                    }
                    effective_compSize = effective_allocSize;
                    effective_keyset = data_block->keyset_id;

                    cur_device_id = data_block->device_index;
                } else {
                    logger->warn("exHT: {} not found", blkDesc.to_string());
                    fti.nMissHT++;
                    skip_size = BLOCK_SIZE;
                    break;
                }
            } else {
                pos = blkDesc.offset;
            }

            Reader& active_file = (have_vbk ? *vbkf : *device_files.at(cur_device_id));

            if ((!writer && m_cache.contains(blkDesc.digest)) || (!have_vbk && device_files.empty())) {
                // test-only fast path, i.e. similar block already successfully processed => no need to seek/read/unpack once more
                fti.nOK++;
                skip_size = BLOCK_SIZE;
                break;
            }

            if (no_read && !device_files.empty()) {
                // skip reading blocks when extracting/testing files, only check if it exists in the hash table.
                fti.nOK++;
                skip_size = BLOCK_SIZE;
                break;
            }

            if(prev_pos == 0 || pos != prev_pos || effective_allocSize != lzBuf.size() || (!have_vbk && !device_files.empty() && (prev_device_id != cur_device_id))){ // If we have multiple device files, don't mix reads from different files at the same position.
                lzBuf.resize(effective_allocSize);
                ssize_t nread = active_file.read_at(vbk_offset + pos, lzBuf);
                if( nread != effective_allocSize){
                    logger->critical("read error at {:012x}: nread={:x}, sizeof(fBuf)={:x}", vbk_offset+pos, nread, effective_allocSize);
                    fti.nReadErr++;
                    skip_size = BLOCK_SIZE;
                    break;
                }

                if (effective_keyset) { // decrypt if keyset id is present
                    lzBuf.resize(effective_compSize);
                    const auto* cipher = meta.get_aes_cipher(effective_keyset);
                    if( !cipher ){
                        logger->warn("Block #{:x}: missing keyset {}", i, effective_keyset);
                        skip_size = BLOCK_SIZE;
                        break;
                    }
                    cipher->decrypt(lzBuf);
                }
                prev_pos = pos; // massive speedup in case of consecutive identical/empty blocks
                prev_device_id = cur_device_id;
            }

            size_t to_write;
            switch( (uint8_t) effective_comp_type ){
                case CT_NONE:
                    to_write = (remaining_size > 0 && remaining_size < (int64_t)lzBuf.size()) ? remaining_size : lzBuf.size();
                    if( writer ){
                        writer->write((char*)lzBuf.data(), to_write);
                    }
                    actual_written += to_write;
                    remaining_size -= to_write;
                    fti.nOK++;
                    m_cache.insert(blkDesc.digest);
                    break;

                case CT_LZ4:
                        {
                        // 0F 00 00 F8 XX XX XX XX [DATA] -- XX stands for CRC
                        lz_hdr* plz = (lz_hdr*)lzBuf.data();
                        if( plz->valid() ){
                            lzBuf2.resize(plz->srcSize);

                            auto comp_size = effective_keyset ? lzBuf.size() - sizeof(lz_hdr) : effective_compSize - sizeof(lz_hdr);
                            
                            int lz4res = LZ4_decompress_safe(
                                (const char*) &lzBuf[sizeof(lz_hdr)],
                                (char*) lzBuf2.data(),
                                comp_size,
                                lzBuf2.size());

                            to_write = (remaining_size > 0 && remaining_size < plz->srcSize) ? remaining_size : plz->srcSize;
                            if( writer ){
                                writer->write((char*)lzBuf2.data(), to_write);
                            }
                            actual_written += to_write;
                            remaining_size -= to_write;

                            uint32_t crc = vcrc32(0, (const char*)lzBuf2.data(), lzBuf2.size());
                            if( (size_t)lz4res != lzBuf2.size() ){
                                if( !test_only || verbosity > 0 ){
                                    logger->error("LZ4 failure lz4res={:8x}, expected crc {:08x}, actual crc {:08x} - {}, effective_allocSize {:08x}", lz4res, +plz->crc, crc, blkDesc.to_string(), effective_allocSize);
                                }
                                fti.nErrDecomp++;
                            } else if( crc != plz->crc ){
                                if( !test_only || verbosity > 0 ){
                                    logger->error("invalid CRC lz4res={:8x}, expected crc {:08x}, actual crc {:08x} - {}", lz4res, +plz->crc, crc, blkDesc.to_string());
                                }
                                fti.nErrCRC++;
                            } else {
                                fti.nOK++;
                                m_cache.insert(blkDesc.digest);
                            }
                        } else {
                            if( !test_only || verbosity > 0 ){
                                uint64_t offset_copy = blkDesc.offset; // Copy to avoid packed field binding issue
                                logger->warn("{:08x}: LZ4 magic mismatch", offset_copy);
                            }
                            fti.nErrDecomp++;
                            skip_size = BLOCK_SIZE;
                        }
                        }
                    break;

                case CT_RLE:
                    throw std::runtime_error("RLE decompression not implemented");

                case CT_ZLIB_HI:
                case CT_ZLIB_LO:
                        {
                        const size_t input_size = lzBuf.size();
                        const size_t output_size = std::min((uint32_t)BLOCK_SIZE, blkDesc.srcSize);
                        
                        lzBuf2.resize(output_size);
                        z_stream strm;
                        memset(&strm, 0, sizeof(strm));
                        strm.next_in = (Bytef *)lzBuf.data();
                        strm.avail_in = input_size;
                        strm.next_out = (Bytef *)lzBuf2.data();
                        strm.avail_out = output_size;
                        if (inflateInit2(&strm, 15) == Z_OK ){
                            int ret = inflate(&strm, Z_FINISH);
                            if (inflateEnd(&strm) == Z_OK && ret == Z_STREAM_END) { // check inflateEnd first to always have it called
                                const size_t actual_output = strm.total_out;
                                if (m_md5.Calculate(lzBuf2.data(), actual_output) == blkDesc.digest) {
                                    to_write = (remaining_size > 0 && remaining_size < (int64_t)actual_output) ? remaining_size : actual_output;
                                    if( writer ){
                                        writer->write((char*)lzBuf2.data(), to_write);
                                    }
                                    actual_written += to_write;
                                    remaining_size -= to_write;
                                    fti.nOK++;
                                    m_cache.insert(blkDesc.digest);
                                } else {
                                    logger->warn("zlib inflate() succeed, but md5 mismatch: {}", blkDesc.to_string());
                                    fti.nErrDecomp++;
                                    skip_size = BLOCK_SIZE;
                                }
                            } else {
                                logger->warn("zlib inflate() failed: ret={}, avail_in={}, avail_out={}, {}", ret, strm.avail_in, strm.avail_out, blkDesc.to_string());
                                fti.nErrDecomp++;
                                skip_size = BLOCK_SIZE;
                            }
                        } else {
                            logger->warn("zlib inflateInit2() failed: {}", blkDesc.to_string());
                            fti.nErrDecomp++;
                            skip_size = BLOCK_SIZE;
                        }
                        }
                    break;

                case CT_ZSTD3:
                case CT_ZSTD9:
                        {
                        const size_t input_size = lzBuf.size();
                        const size_t output_size = std::min((uint32_t)BLOCK_SIZE, blkDesc.srcSize ? blkDesc.srcSize : (uint32_t)BLOCK_SIZE);

                        lzBuf2.resize(output_size);

                        ZSTD_DCtx* const dctx = ZSTD_createDCtx();
                        if( !dctx ){
                            logger->warn("ZSTD_createDCtx() failed: {}", blkDesc.to_string());
                            fti.nErrDecomp++;
                            skip_size = BLOCK_SIZE;
                            break;
                        }

                        ZSTD_inBuffer input = { lzBuf.data(), input_size, 0 };
                        ZSTD_outBuffer output = { lzBuf2.data(), lzBuf2.size(), 0 };
                        size_t ret = ZSTD_decompressStream(dctx, &output, &input);
                        ZSTD_freeDCtx(dctx);

                        if( ret != 0 ){
                            if( ZSTD_isError(ret) ){
                                logger->warn("zstd decompress failed: {} - {}", ZSTD_getErrorName(ret), blkDesc.to_string());
                            } else {
                                logger->warn("zstd decompress incomplete (want {:x} more src bytes): {}", ret, blkDesc.to_string());
                            }
                            fti.nErrDecomp++;
                            skip_size = BLOCK_SIZE;
                            break;
                        }

                        const size_t actual_output = output.pos;
                        if( m_md5.Calculate(lzBuf2.data(), actual_output) == blkDesc.digest ){
                            to_write = (remaining_size > 0 && remaining_size < (int64_t)actual_output) ? remaining_size : actual_output;
                            if( writer ){
                                writer->write((char*)lzBuf2.data(), to_write);
                            }
                            actual_written += to_write;
                            remaining_size -= to_write;
                            fti.nOK++;
                            m_cache.insert(blkDesc.digest);
                        } else {
                            logger->warn("zstd decompress succeeded, but md5 mismatch: {}", blkDesc.to_string());
                            fti.nErrDecomp++;
                            skip_size = BLOCK_SIZE;
                        }
                        }
                    break;

                case 0:
                    if( !blk.hash ){
                        fti.sparse_blocks++;
                        skip_size = BLOCK_SIZE;
                        break;
                    }
                    [[fallthrough]]; // intentional fallthrough to default

                default:
                    logger->error("Unknown compression mode {:02x}", (uint8_t)effective_comp_type);
                    skip_size = BLOCK_SIZE;
                    break;
            } // switch(compType)
        } while(0);

        if( skip_size > 0 ){
            if( writer ){
                writer->seek(skip_size, SEEK_CUR); // seek instead of write-zeroes to make sparse file
            }
            remaining_size -= skip_size;
        }

        if( test_only || verbosity >= 0 ){
            if( need_table_header ){
                need_table_header = false;
                fmt::print("{}\n", fti.header());
            }

            if( i%10 == 0 ){
                static struct timespec prev_time = {0, 0};
                struct timespec cur_time;
                clock_gettime(CLOCK_MONOTONIC, &cur_time);

                uint64_t dt = (cur_time.tv_sec - prev_time.tv_sec) * 1000000000L + (cur_time.tv_nsec - prev_time.tv_nsec);
                if( dt > 100000000 ){
                    prev_time = cur_time;

                    fmt::print("{}\r", fti.to_string());
                    fflush(stdout);
                }
            }
        }
    }

    if( test_only || verbosity >= 0 ){
        fmt::print("{}\n", fti.to_string(true)); // update final results row on console
        logger->file_only(spdlog::level::info, "{}", fti.header());
        logger->file_only(spdlog::level::info, "{}", fti.to_string());
    }

    if( remaining_size > 0 && !vFile.is_diff() ){
        logger->warn("Remaining size {:x} > 0", remaining_size);
    }

    if( writer ){
        if( writer->tell() == actual_written ){
            logger->info("saved {} to \"{}\"",
                bytes2human(actual_written, " bytes"),
                out_fname);
        } else {
            logger->info("saved apparent {}, actual {} to \"{}\"",
                bytes2human(writer->tell(), " bytes"),
                bytes2human(actual_written, " bytes"),
                out_fname);
        }
    }

    if (!json_fname.empty()){
        std::ofstream json_out(json_fname, std::ios::app);
        if (!json_out.is_open()) {
            logger->error("Failed to open JSON output file: {}", json_fname.string());
        } else {
            json_out << fti.to_json() << std::endl;
        }
    }
}
