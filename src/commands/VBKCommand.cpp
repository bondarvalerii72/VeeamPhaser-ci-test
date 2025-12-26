/**
 * @file VBKCommand.cpp
 * @brief Implementation of the VBKCommand for one-step VIB/VBK processing.
 *
 * This file provides the main entry point for processing Veeam backup files (VBK/VIB).
 * It automatically detects and validates the file header, identifies valid backup slots,
 * verifies bank integrity, and selects the best slot for metadata extraction. Once a
 * valid slot is identified, it delegates to MDCommand for actual file extraction and
 * metadata processing. This is the default command when no specific command is specified.
 */

#include "VBKCommand.hpp"
#include "utils/common.hpp"
#include "../Veeam/VBK.hpp"
#include "io/Reader.hpp"
#include "MDCommand.hpp"
//#include <zstd.h>

using namespace Veeam::VBK;

REGISTER_COMMAND(VBKCommand);

/**
 * @brief Constructs a VBKCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
VBKCommand::VBKCommand(bool reg) : Command(reg, "vbk", "one-step vbk/vib processing [default if none given]") {
    m_parser.add_argument("filenames").help("VBK/VIB filename");

    MDCommand::add_common_args(m_parser);

    m_parser.add_argument("--offset").default_value((uint64_t)0ULL).scan<'x', uint64_t>().help("start offset (hex)");
    m_parser.add_argument("--slot").scan<'i', int>().help("specify slot explicitly (default: auto)");
    m_parser.add_argument("--password").default_value(std::string("")).help("Password for decrypting encrypted vbk.");

    // MDCommand integration
    m_parser.add_argument("--vbk").hidden();
    m_parser.add_argument("--no-vbk").default_value(false).hidden();
    m_parser.add_argument("--vbk-offset").default_value((uint64_t)0ULL).hidden();
}

/**
 * @brief Executes the VBK command to process a VIB/VBK file.
 *
 * This function performs comprehensive VBK/VIB file analysis:
 * 1. Validates the file header and determines slot size
 * 2. Scans all slots (up to MAX_SLOTS) or a specific slot if --slot is provided
 * 3. For each slot, validates its CRC and verifies all associated banks
 * 4. Identifies the slot with the most valid banks (best score)
 * 5. Warns if slot merging would improve coverage
 * 6. Delegates to MDCommand for metadata processing using the best slot
 *
 * The function handles multiple backup snapshots (slots) and their data banks,
 * selecting the most complete and valid slot for file extraction. If no valid
 * slots are found, it suggests using the 'scan' command for more advanced recovery.
 *
 * @return EXIT_SUCCESS (0) if a valid slot is found and processed, EXIT_FAILURE (1) otherwise.
 */
int VBKCommand::run() {
    const std::string vbk_fname = m_parser.get("filenames");
    init_log(vbk_fname);

    const size_t vbk_size = Reader::get_size(vbk_fname);
    logger->info("source vbk {} ({:x} = {})", vbk_fname, vbk_size, bytes2human(vbk_size));

    Reader reader(vbk_fname);
    const uint64_t offset = m_parser.get<uint64_t>("--offset");

    buf_t hdr_buf(PAGE_SIZE);
    buf_t decomp_buf;
    reader.read_at(offset, hdr_buf.data(), PAGE_SIZE);
//    if( decompress_page(hdr_buf.data(), hdr_buf.size(), decomp_buf, offset) ){
//        hdr_buf = decomp_buf;
//    }
    FileHeader* hdr = (FileHeader*)hdr_buf.data();

    logger->trace("file header: {}", to_hexdump(hdr_buf));
    logger->info("{:08x}: {}{}{}", offset, hdr->valid() ? ANSI_COLOR_GREEN : ANSI_COLOR_RED, filter_unprintable(hdr->to_string()), ANSI_COLOR_RESET);

    const size_t slot_size = hdr->valid() ? hdr->slot_size() : 0x80000;
    logger->debug("max_banks: {:x}, slot_size: {:x}", hdr->max_banks(), slot_size);

    if( hdr_buf.size() > PAGE_SIZE ){
        CSlot* slot = (CSlot*)(hdr_buf.data() + PAGE_SIZE);
        logger->info("slot: {}", slot->to_string());
        logger->info("  {}", slot->snapshotDescriptor.to_string());
        for(uint32_t i=0; i<slot->allocated_banks; i++){
            const CSlot::BankInfo& bi = slot->bankInfos[i];
            logger->info("  bank {:02x}: {}", i, bi.to_string());
        }
    }

    std::map<size_t, std::vector<bool>> slots_map; // offset -> [valid banks]
    buf_t slot_buf(slot_size);
    buf_t bank_buf;
    size_t tail_offset = 0;
    size_t storage_eof = 0;
    for( size_t slot_idx=0; slot_idx<MAX_SLOTS; slot_idx++ ) {
        if( m_parser.is_used("--slot") && m_parser.get<int>("--slot") != (int)slot_idx ){
            continue;
        }
        logger->info("");
        size_t slot_offset = offset + PAGE_SIZE + slot_idx * slot_size;
        reader.read_at(slot_offset, slot_buf.data(), slot_size);
        CSlot* slot = (CSlot*)slot_buf.data();

        logger->trace("slot[{}]: {}", slot_idx, to_hexdump(slot_buf));
        bool valid = slot->size() <= slot_size && slot->valid_fast() && slot->valid_crc();
        const char* color = valid ? ANSI_COLOR_GREEN : ANSI_COLOR_RED;
        logger->info("{:08x}: slot[{}]: {}{}{}", slot_offset, slot_idx, color, slot->to_string(), ANSI_COLOR_RESET);
        logger->info("  {}", slot->snapshotDescriptor.to_string());
        if( slot->snapshotDescriptor.storage_eof > reader.size() ){
            logger->error("  storage_eof {:x} > actual EOF {:x}", slot->snapshotDescriptor.get_storage_eof(), reader.size());
        }
        if( !valid ) {
            continue;
        }

        slots_map[slot_offset] = std::vector<bool>(slot->allocated_banks, false);
        if( slot->snapshotDescriptor.get_storage_eof() > storage_eof ){
            storage_eof = slot->snapshotDescriptor.get_storage_eof();
        }
        for(uint32_t i=0; i<slot->allocated_banks; i++){
            const CSlot::BankInfo& bi = slot->bankInfos[i];
            bank_buf.resize(bi.size);
            reader.read_at(offset+bi.offset, bank_buf.data(), bi.size);
            if( offset + bi.offset + bi.size > tail_offset ){
                tail_offset = offset + bi.offset + bi.size;
            }

//            for( size_t j=0; j<bi.size; j+=PAGE_SIZE ){
//                if( decompress_page(bank_buf.data()+j, PAGE_SIZE, decomp_buf, offset+bi.offset+j) ){
//                    if( decomp_buf.size() > bank_buf.size()-j ){
//                        bank_buf.resize(j+decomp_buf.size());
//                    }
////                    memcpy(bank_buf.data()+j, decomp_buf.data(), decomp_buf.size());
////                    memset(bank_buf.data()+j, 0, decomp_buf.size());
//                }
//            }

            CBank* bank = (CBank*)bank_buf.data();
            color = ANSI_COLOR_RED;
            const size_t actual_size = bank->size();
            const uint32_t actual_crc = vcrc32(0, bank, bi.size);
            std::string crc_s;
            if( bank->valid_fast() && actual_crc == bi.crc && actual_size == bi.size ) {
                color = ANSI_COLOR_GREEN;
                slots_map[slot_offset][i] = true;
            } else {
                crc_s = fmt::format(" [actual crc {:08x}]", actual_crc);
                if( bi.size != actual_size ){
                    crc_s += fmt::format("[actual size {:x}]", actual_size);
                }
            }
            logger->info("    bank {:02x}: {}{}{}{}", i, color, bi.to_string(), ANSI_COLOR_RESET, crc_s);
        }
    }
    if( storage_eof && storage_eof > tail_offset ){
        logger->warn("{:012x}: {:x} bytes of data not covered by banks", tail_offset, storage_eof - tail_offset);
//        const size_t chunk_size = PAGE_SIZE;
//        buf_t chunk_buf(chunk_size);
//        for( size_t pos=tail_offset; pos<storage_eof; pos+=PAGE_SIZE ){
//            reader.read_at(pos, chunk_buf.data(), chunk_size);
//            if( decompress_page(chunk_buf.data(), chunk_buf.size(), decomp_buf, pos) ){
//            }
//        }
    }

    size_t best_score = 0;
    size_t best_slot_offset = 0;
    if( slots_map.size() ){
        std::vector<bool> merged_bitmap;
        for(auto& [slot_offset, bitmap] : slots_map){
            if( merged_bitmap.empty() ){
                merged_bitmap = bitmap;
            } else {
                merged_bitmap.resize(std::max(merged_bitmap.size(), bitmap.size()), false);
                for(size_t i=0; i<bitmap.size(); i++){
                    if( bitmap[i] ){
                        merged_bitmap[i] = true;
                    }
                }
            }

            size_t score = std::count(bitmap.begin(), bitmap.end(), true);
            logger->debug("slot @ {:08x}: score {:x}", slot_offset, score);
            if( score > best_score ){
                best_score = score;
                best_slot_offset = slot_offset;
            }
        }
        size_t merged_score = std::count(merged_bitmap.begin(), merged_bitmap.end(), true);
        if( merged_score > best_score ){
            logger->warn("merged_score {:x} > best_score {:x}: slot merging is necessary. 'scan' command can do that", merged_score, best_score);
        }
    }

    if( best_score ){
        logger->info("using{} slot @ {:x}", slots_map.size() == 1 ? "" : " best", best_slot_offset);
        m_parser.parse_args({"unused", "--vbk", vbk_fname});
        MDCommand md_cmd;
        md_cmd.set_parser(&m_parser);
        md_cmd.set_meta_offset(best_slot_offset);
        md_cmd.set_meta_source(CMeta::MS_SLOT);
        return md_cmd.run();
    } else {
        logger->critical("no valid slots found. cannot continue. try to use 'scan' command");
        return 1;
    }

    return 0;
}
