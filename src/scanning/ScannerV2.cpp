/**
 * @file ScannerV2.cpp
 * @brief Implementation of the improved Scanner for metadata and data block detection (V2).
 *
 * This file provides the enhanced scanning implementation for detecting metadata blocks,
 * slots, banks, and optionally data blocks in VIB/VBK files. Features improvements over
 * V1 including bitmap tracking of scanned regions, better bank merging logic, data block
 * carving support, and more efficient duplicate detection. This is the recommended
 * scanner version for new operations.
 */

#include "ScannerV2.hpp"
#include "utils/common.hpp"
#include "core/structs.hpp"

#include <lz4.h>
#include <zlib.h>

#include <fstream>
#include <cstring>
#include <algorithm>
#include <span>
#include <array>

using namespace Veeam::VBK;
using BankInfo = CSlot::BankInfo;

// maximum size that LZ4 compression may output in a "worst case" scenario
constexpr size_t MAX_COMP_SIZE = LZ4_COMPRESSBOUND(BLOCK_SIZE);
constexpr size_t BITMAP_BLOCK_SIZE = PAGE_SIZE; // 4kb

/**
 * @brief Initializes the scanner and opens output files.
 *
 * Sets up output CSV files for carved blocks and initializes the bitmap
 * for tracking scanned regions if data block carving is enabled.
 */
void ScannerV2::start() {
        if (!m_keysets_dump.empty()) {
        if (!load_keysets_dump(m_keysets_dump)) {
            logger->warn("Failed to load keysets from {}", m_keysets_dump);
        }
    }

    if (m_find_blocks) {
        m_decomp_buf.resize(MAX_COMP_SIZE);
        std::filesystem::path out_fname = get_out_pathname(m_fname, "carved_blocks.csv");
        logger->info("carving data blocks to {}{}", out_fname.string(), (m_start == 0) ? "" : " [append]");
        const auto mode = std::ios::out | std::ios::binary | ((m_start == 0) ? std::ios::trunc : std::ios::app);
        m_good_blocks_csv = std::ofstream(out_fname, mode);
        if (!m_good_blocks_csv.is_open()) {
            logger->error("Failed to open output file {}: {}", out_fname.string(), std::strerror(errno));
            throw std::runtime_error("Failed to open output file");
        }

        m_bad_blocks_csv = std::ofstream(get_out_pathname(m_fname, "bad_blocks.csv"), mode);
        m_bitmap = std::make_unique<BitFileMappedArray>(get_out_pathname(m_fname, "carved_blocks.map"), m_reader.size() / BITMAP_BLOCK_SIZE);
    }
    DblBufScanner::start();
}

bool ScannerV2::load_keysets_dump(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in) {
        return false;
    }

    m_aes_keys.clear();
    m_aes_ciphers.clear();

    for (uint32_t i = 0; i < count; ++i) {
        unsigned __int128 uuid_u = 0;
        crypto::aes_key aes{};

        in.read(reinterpret_cast<char*>(&uuid_u), sizeof(uuid_u));
        in.read(reinterpret_cast<char*>(aes.key), sizeof(aes.key));
        in.read(reinterpret_cast<char*>(aes.iv), sizeof(aes.iv));

        if (!in) {
            m_aes_keys.clear();
            return false;
        }

        const auto id = digest_t(static_cast<__uint128_t>(uuid_u));
        crypto::register_keyset(m_aes_keys, m_aes_ciphers, id, aes);
    }
    logger->info("Loaded {} keyset{} from {}", m_aes_keys.size(), m_aes_keys.size() == 1 ? "" : "s", path.string());
    return true;
}


const crypto::AES256* ScannerV2::get_aes_cipher(const digest_t& id) const {
    auto it = m_aes_ciphers.find(id);
    if (it == m_aes_ciphers.end()) {
        return nullptr;
    }
    return it->second.get();
}

void ScannerV2::process_buf(const buf_t& buf, off_t file_offset) {
    if (buf.size() < PAGE_SIZE) {
        logger->warn("{:x}: buf size {} is smaller than PAGE_SIZE, skipping scan", file_offset, buf.size());
        return;
    }
    for(size_t pos=0; pos <= buf.size() - PAGE_SIZE; pos += PAGE_SIZE){
        if( m_checked_offsets.find(file_offset + pos) != m_checked_offsets.end() ){
            continue;
        }
        // at least PAGE_SIZE of data is available
        check_slot(buf, file_offset, pos);
        check_bank(buf, file_offset, pos);
        if (m_find_blocks) {
            if( !check_data(buf, file_offset, pos) ) {
                if( is_all_zero(buf.data() + pos, PAGE_SIZE) ){
                    set_bitmap(file_offset + pos, PAGE_SIZE); // mark empty pages as occupied bc there is no point in scanning them again
                }
            }
        }
    }
}

static uint64_t calc_bank_id(const BankInfo& bi) {
    return ((uint64_t)bi.crc << 32) | (uint64_t)bi.size;
}

static std::string gen_bank_fname(uint64_t id) {
    return fmt::format("_{:08x}_{:08x}.bank", (uint32_t)(id >> 32), (uint32_t)id);
}


static uint64_t calc_slot_fingerprint(const CSlot* slot) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < slot->allocated_banks; i++) {
        const auto& bi = slot->bankInfos[i];
        h ^= bi.crc;  h *= 1099511628211ULL;
        h ^= bi.size; h *= 1099511628211ULL;
    }
    return h;
}


void ScannerV2::save_bank(const BankInfo& bi){
    uint64_t bank_id = calc_bank_id(bi);
    std::string fname = gen_bank_fname(bank_id);

    save_file(fname, bi.offset, bi.size);

    if( m_bank_usagecnt.find(bank_id) == m_bank_usagecnt.end() ){ // do nothing for repeating banks
        m_bank_usagecnt[bank_id] = 0;
    }
    set_bitmap(bi.offset, bi.size);
}

void ScannerV2::increment_bank_usagecnt(const BankInfo& bi) {
    m_bank_usagecnt[calc_bank_id(bi)]++;
}

/**
 * @brief Infers bank ID when a slot is not found unavailable.
 * @param bank Pointer to bank structure
 * @param bank_crc CRC32 of bank (for duplicate detection)
 * @return Inferred bank ID (0 to 0x7eff)
 */
uint32_t ScannerV2::guess_bank_id(const CBank* bank, uint32_t bank_crc) {
    using PhysPageId = Veeam::VBK::PhysPageId;

    // count frequency of next.bank_id
    std::unordered_map<uint32_t, uint32_t> bank_id_freq;
    
    for (uint32_t page_id = 0; page_id < bank->header_page.nPages; page_id++) {
        if (bank->header_page.free_pages[page_id]) {
            continue;
        }
        
        const auto& page = bank->data_pages[page_id];
        const PhysPageId* next_ptr = (const PhysPageId*)page.data;
        
        if (next_ptr->bank_id >= 0 && next_ptr->bank_id < 0x7f00 &&
            next_ptr->page_id >= 0 && next_ptr->page_id < 0x1000) {
            bank_id_freq[next_ptr->bank_id]++;
        }
    }
    
    uint32_t best_bank_id = 0;
    uint32_t best_freq = 0;
    for (const auto& [bid, freq] : bank_id_freq) {
        if (freq > best_freq) {
            best_freq = freq;
            best_bank_id = bid;
        }
    }
    
    if (best_freq > 1) {
        logger->debug("Bank[{:02x}] crc {:08x} inferred via next.bank_id frequency ({})", best_bank_id, bank_crc, best_freq);
        return best_bank_id;
    }
    
    // average self.bank_id from PageStack roots
    uint64_t bank_id_sum = 0;
    uint32_t valid_roots = 0;
    
    for (uint32_t page_id = 0; page_id < bank->header_page.nPages; page_id++) {
        if (bank->header_page.free_pages[page_id]) {
            continue;
        }
        
        const auto& page = bank->data_pages[page_id];
        const PhysPageId* self_ptr = (const PhysPageId*)(page.data + sizeof(PhysPageId));
        
        if (self_ptr->page_id == (int32_t)page_id) {
            if (self_ptr->bank_id >= 0 && self_ptr->bank_id < 0x7f00) {
                bank_id_sum += self_ptr->bank_id;
                valid_roots++;
            }
        }
    }
    
    if (valid_roots > 1) {
        uint32_t avg_bank_id = bank_id_sum / valid_roots;
        logger->debug("Bank[{:02x}] crc {:08x} inferred via self.bank_id average ({} roots)", avg_bank_id, bank_crc, valid_roots);
        return avg_bank_id;
    }
    
    // sequential fallback
    uint32_t fallback_id = m_current_bank_id;
    logger->debug("Bank[{:02x}] crc {:08x} inferred via sequential fallback", fallback_id, bank_crc);
    return fallback_id;
}

void ScannerV2::finish() {
    DblBufScanner::finish();

    if (!m_carve_mode && m_slots_map.empty() && m_is_encrypted && m_bank_id_to_bank.size() <= 1) {
        logger->warn("Encrypted banks detected and no bank was decrypted - skipping synthetic slot reconstruction");
    }
    
    // build a synthetic slot if we found banks but no slot metadata
    if (!m_carve_mode && m_slots_map.empty() && m_bank_id_to_bank.size() > 1 && !m_failed_guess) {
        logger->info("No slots found, creating synthetic slot from {} inferred banks", m_bank_id_to_bank.size());
        
        // allocate slot structure (max_banks is always 0x7f00)
        uint32_t max_banks = 0x7f00;

        uint32_t allocated_banks = 0;
        for (const auto& [bank_id, _] : m_bank_id_to_bank) {
            if (bank_id + 1 > allocated_banks) {
                allocated_banks = bank_id + 1;
            }
        }
        
        size_t slot_size = sizeof(CSlot) + max_banks * sizeof(CSlot::BankInfo);
        std::vector<uint8_t> slot_buf(slot_size, 0);
        CSlot* slot = reinterpret_cast<CSlot*>(slot_buf.data());
        
        slot->has_snapshot = 1;
        slot->max_banks = max_banks;
        slot->allocated_banks = allocated_banks;
        
        // set snapshot descriptor to typical defaults
        slot->snapshotDescriptor.version = 0x18;
        slot->snapshotDescriptor.nBanks = allocated_banks;
        
        // typical objRefs pointers seen in normal slots
        slot->snapshotDescriptor.objRefs.MetaRootDirPage = {0, 0};
        slot->snapshotDescriptor.objRefs.children_num = 1;
        slot->snapshotDescriptor.objRefs.DataStoreRootPage = {1, 0};
        slot->snapshotDescriptor.objRefs.BlocksCount = 0x1bf6;
        slot->snapshotDescriptor.objRefs.free_blocks_root = {2, 0};
        slot->snapshotDescriptor.objRefs.dedup_root = {1, 1};
        slot->snapshotDescriptor.objRefs.f30 = {-1, -1};
        slot->snapshotDescriptor.objRefs.f38 = {-1, -1};
        if (m_is_encrypted){
            slot->snapshotDescriptor.objRefs.CryptoStoreRootPage = {2, 1};
        } else {
            slot->snapshotDescriptor.objRefs.CryptoStoreRootPage = {-1, -1};
        }
        slot->snapshotDescriptor.objRefs.ArchiveBlobStorePage = {-1, -1};
        
        // populate bank info array with offsets starting after the slot header
        off_t current_offset = slot_size;
        uint64_t storage_eof = 0;
        for (const auto& [bank_id, bank_info] : m_bank_id_to_bank) {
            slot->bankInfos[bank_id].crc = bank_info.crc;
            slot->bankInfos[bank_id].offset = current_offset;
            slot->bankInfos[bank_id].size = bank_info.size;
            
            current_offset += bank_info.size;
            
            if (static_cast<uint64_t>(current_offset) > storage_eof) {
                storage_eof = static_cast<uint64_t>(current_offset);
            }
        }
        
        slot->snapshotDescriptor.storage_eof = storage_eof;
        
        slot->crc = vcrc32(0, ((uint8_t*)slot) + sizeof(uint32_t), slot_size - sizeof(uint32_t));
        
        std::filesystem::path slot_path = get_out_pathname(m_fname, "reconstructed_slot.slot");
        logger->info("Writing slot header to {}", slot_path.string());
        
        {
            Writer slot_writer(slot_path, true);
            slot_writer.write(slot_buf.data(), slot_size);
        }
        
        // append banks to the slot file
        logger->info("Adding {} banks into the slot", allocated_banks);
        
        Writer slot_append(slot_path, false);
        buf_t bank_buf;
        for (const auto& [bank_id, bank_info] : m_bank_id_to_bank) {
            off_t slot_offset = slot->bankInfos[bank_id].offset;
            uint32_t bank_size = slot->bankInfos[bank_id].size;
            
            logger->debug("Adding bank {:02x}: slot offset {:x}, size {:x}", 
                         bank_id, slot_offset, bank_size);
            
            // read bank from source file
            bank_buf.resize(bank_size);
            m_reader.read_at(bank_info.offset, bank_buf.data(), bank_size);
            
            slot_append.seek(slot_offset);
            slot_append.write(bank_buf.data(), bank_size);
        }
        
        logger->info("Slot created successfully at {}", slot_path.string());
    }
    
    for(const auto it : m_bank_usagecnt){
        if( it.second > 0 ) {
            std::string fname = gen_bank_fname(it.first);
            logger->debug("Removing bank {} with usage count {}", fname, it.second);
            std::filesystem::path out_fname = get_out_pathname(m_fname, fname);
            try {
                std::filesystem::remove(out_fname);
            } catch (...) {
                // ignore errors, we don't care if the file is not removed
            }
        }
    }
}

void ScannerV2::check_bank(const buf_t& buf, off_t file_offset, size_t pos) {
    const off_t bank_offset = file_offset + pos;
    // logger->trace("check_bank: file_offset: {:x}, pos: {:x}, bank_offset: {:x}", file_offset, pos, bank_offset);

    const CBank* bank = (CBank*)(buf.data() + pos);
    if(!bank->valid_fast()){
        return;

    }

    buf_t tmp;
    if( bank->size() + pos >= buf.size() ){
        tmp.resize(bank->size());
        m_reader.read_at(bank_offset, tmp.data(), bank->size());
        bank = (CBank*)tmp.data();
        if( !bank->valid_fast() ){ // may be invalid after reading, seen on bad ZFS array (CRC errors) on windows
            logger->warn_once("{:x}: Invalid Bank on 2nd read, but was valid on 1st", bank_offset);
            return;
        }
    }

    if (bank->is_encrypted()) {
        m_is_encrypted = true;
    }

    bool decrypted = false;
    buf_t decrypted_bank_raw;
    const CBank* bank_for_guess = bank;
    if (bank->is_encrypted() && !m_aes_ciphers.empty() && m_slots_map.empty() && !m_carve_mode) {
        decrypted_bank_raw.resize(bank->size());
        memcpy(decrypted_bank_raw.data(), bank, decrypted_bank_raw.size());

        auto* bank_mut = reinterpret_cast<CBank*>(decrypted_bank_raw.data());
        const auto* cipher = get_aes_cipher(bank_mut->header_page.keyset_id);
        if (cipher) {
            const auto encr_size = bank_mut->encr_size();
            std::vector<uint8_t> bank_data(reinterpret_cast<uint8_t*>(bank_mut->data_pages[0].data),
                                           reinterpret_cast<uint8_t*>(bank_mut->data_pages[0].data) + encr_size);
            try {
                cipher->decrypt(bank_data);
            } catch (const std::exception& e) {
                logger->error("Failed to decrypt Bank @ {:12x} keyset {}: {}", bank_offset, bank_mut->header_page.keyset_id, e.what());
                bank_data.clear();
                //decryption failed, but the bank is likely valid and corrupted since it passed valid_fast()
                m_current_bank_id++;
                return;
            }

            if (!bank_data.empty()) {
                const auto padding_size = encr_size - bank_data.size();
                memcpy(bank_mut->data_pages[0].data, bank_data.data(), bank_data.size());
                memset(bank_mut->data_pages[0].data + bank_data.size(), 0, padding_size);
                bank_mut->header_page.encr_size = 0;
                bank_mut->header_page.keyset_id = digest_t(0);
                decrypted = true;
                bank_for_guess = bank_mut;
            }
        } else {
            logger->warn("No keyset found for Bank @ {:12x} keyset {}", bank_offset, bank->header_page.keyset_id);
        }
    }

    if (decrypted) {
        if (!bank_for_guess->valid_fast() || !bank_for_guess->valid_slow(decrypted_bank_raw.size())) {
            return;
        }
    } else {
        if (!bank->valid_slow(tmp.empty() ? (buf.size() - pos) : tmp.size())) {
            return;
        }
    }

    found("banks");

    uint32_t crc = calc_bank_crc(buf, file_offset, pos);

    uint32_t size = bank->size();
    uint64_t bank_id = ((uint64_t)crc << 32) | (uint64_t)size;
    if (m_seen_bank_ids.count(bank_id)) {
        logger->debug("Skipping duplicate/mirror Bank at {:012x}, crc {:08x}, size {:7x}", bank_offset, crc, size);
        m_checked_offsets.insert(bank_offset);
        return;
    }
    m_seen_bank_ids.insert(bank_id);

    if (!m_carve_mode && m_slots_map.empty() && (!bank->is_encrypted() || decrypted) && !m_failed_guess){
        // try to infer bank id when we're scanning without slots
        if (m_seen_bank_crcs.find(crc) != m_seen_bank_crcs.end()) { // This should never happen in normal circumstances, because of the check before.
            logger->info("Found bank[{:02x}] mirror at {:12x}, crc {:08x}, size {:7x}", m_bank_crc_to_bank_id[crc], bank_offset, crc, bank->size());
        } else {
            auto inferred_bank_id = guess_bank_id(bank_for_guess, crc);
            if (inferred_bank_id < m_current_bank_id){
                logger->warn("Inferred bank ID {:02x} is less than current bank ID {:02x}, failed to guess id's.", inferred_bank_id, m_current_bank_id);
                m_failed_guess = true;
            } else{
                m_current_bank_id = inferred_bank_id + 1;
                m_seen_bank_crcs.insert(crc);
                logger->info("Found bank[{:02x}] at {:12x}, crc {:08x}, size {:7x}", inferred_bank_id, bank_offset, crc, bank->size());
                m_bank_id_to_bank[inferred_bank_id] = {crc, bank_offset, bank->size()};
                m_bank_crc_to_bank_id[crc] = inferred_bank_id;
            }
            }
            
    } else {
        logger->info("Found Bank at {:12x}, crc {:08x}, size {:7x} {}", bank_offset, crc, bank->size(), process_bank(bank, crc, bank_offset));
    }
    
    save_bank({crc, bank_offset, bank->size()});
    m_checked_offsets.insert(bank_offset);
}

// - updates .slot files with matching bank
// - returns string with bank description
std::string ScannerV2::process_bank(const CBank* bank, uint32_t bank_crc, off_t bank_offset) {
    std::string s;
    if (bank->is_encrypted())
        s += fmt::format("{}[encrypted]{}", ANSI_COLOR_YELLOW, ANSI_COLOR_RESET);

    int nfound = 0;
    // first pass tries to find exact match, and marks it as found
    // second pass ignores bank offset/crc, and does not mark bank as found
    for( const bool exact : {true, false} ){
        for(auto& [slot_offset, slot_map] : m_slots_map){
            auto it = slot_map.crc_map.find(bank_crc); // find bank by crc
            if( it != slot_map.crc_map.end() ){
                size_t sbi_idx = it->second;
                SlotBankInfo& sbi = m_sbis[sbi_idx];
                if( exact ){
                    if( sbi.info.offset == bank_offset ) {
                        s += fmt::format("{}[bank {:2x} of slot {:012x}]{}", ANSI_COLOR_GREEN, sbi.idx, slot_offset, ANSI_COLOR_RESET);
                        sbi.found = true;
                        nfound++;
                        update_file(slot_offset, ".slot", sbi.info.offset, bank_offset, sbi.info.size);
                        m_checked_offsets.insert(sbi.info.offset);
                        increment_bank_usagecnt(sbi.info);
                    }
                } else {
                    if( !sbi.found && sbi.info.offset != bank_offset ) {
                        if( nfound < 2 ){
                            s += fmt::format("[bank {:2x} of slot {:012x}]", sbi.idx, slot_offset);
                        }
                        nfound++;
                        update_file(slot_offset, ".slot", sbi.info.offset, bank_offset, sbi.info.size);
                        increment_bank_usagecnt(sbi.info);
                    }
                }
            } else {
                // not found by crc, try to find by offset
                auto it = slot_map.offset_map.find(bank_offset);
                if( it != slot_map.offset_map.end() ){
                    size_t sbi_idx = it->second;
                    SlotBankInfo& sbi = m_sbis[sbi_idx];
                    if( !exact ){
                        if( !sbi.found && sbi.info.crc != bank_crc ) {
                            if( nfound < 2 ){
                                s += fmt::format("[bank {:2x} of slot {:012x}]", sbi.idx, slot_offset);
                            }
                            nfound++;
                            update_file(slot_offset, ".slot", sbi.info.offset, bank_offset, sbi.info.size);
                            // not updating usage count here, because we are not sure if this is the same bank
                        }
                    }
                }
                
            }
        }
    }
    if( nfound > 2 ){
        s += fmt::format(" and {} more", nfound - 2);
    }
    return s;
}

void ScannerV2::check_slot(const buf_t& buf, off_t file_offset, size_t pos) {
    const off_t slot_offset = file_offset + pos;
    // logger->trace("check_slot: file_offset: {:x}, pos: {:x}, slot_offset: {:x}", file_offset, pos, slot_offset);

    const CSlot* slot = (const CSlot*)(buf.data() + pos);
    if( !slot->valid_fast() )
        return;

    // logger->trace("file_offset: {:x}, pos: {:x}, slot_offset: {:x}, slot_size: {:x}, slot: {}", file_offset, pos, slot_offset, slot->size(), slot->to_string());

    buf_t slot_buf;
    if( slot->size() + pos >= buf.size() ){
        // TODO: make a test case
        slot_buf.resize(slot->size());
        m_reader.read_at(slot_offset, slot_buf.data(), slot->size());
        slot = (const CSlot*)slot_buf.data();
        if( !slot->valid_fast() ){ // may be invalid after reading, seen on bad ZFS array (CRC errors) on windows
            logger->warn_once("{:x}: Invalid Slot on 2nd read, but was valid on 1st", slot_offset);
            return;
        }
    }
    // logger->trace("file_offset: {:x}, pos: {:x}, slot_offset: {:x}, slot_size: {:x}, slot: {}", file_offset, pos, slot_offset, slot->size(), slot->to_string());
    if( slot->valid_crc() ){

        uint64_t fingerprint = calc_slot_fingerprint(slot);
        auto it = m_seen_slot_fingerprints.find(fingerprint);
        if (it != m_seen_slot_fingerprints.end()) {
            logger->info(
                "Skipping duplicate slot at {:012x} (identical to {:012x})",
                slot_offset,
                it->second
            );
            m_checked_offsets.insert(slot_offset);
            return;
        }
        m_seen_slot_fingerprints[fingerprint] = slot_offset;

        m_checked_offsets.insert(slot_offset);
        found("slots");
        logger->info("Found Slot at {:12x}, {:7x} bytes", slot_offset, slot->size());
        logger->info("  {}", slot->to_string());
        logger->info("  {}", slot->snapshotDescriptor.to_string());
        for(uint32_t i=0; i<slot->allocated_banks; i++){
            logger->info("  bank {:02x}: {}", i, slot->bankInfos[i].to_string());
        }
        save_file(fmt::format("{:012x}.slot", slot_offset), slot_offset, slot->size());
        set_bitmap(slot_offset, slot->size());

        // save slot info for later use
        m_slots_map[slot_offset] = {};
        m_sbis.reserve(m_sbis.size() + slot->allocated_banks);
        buf_t bank_buf;
        for(uint32_t i=0; i<slot->allocated_banks; i++){
            m_slots_map[slot_offset].crc_map[slot->bankInfos[i].crc] = m_sbis.size();
            m_slots_map[slot_offset].offset_map[slot->bankInfos[i].offset] = m_sbis.size();
            m_sbis.push_back({i, slot->bankInfos[i]});
            // fast path: try to get bank by offset; skip bank validation bc bank finder might not support it (yet)
            SlotBankInfo& sbi = m_sbis.back();
            bank_buf.resize(sbi.info.size);
            m_reader.read_at(sbi.info.offset, bank_buf.data(), sbi.info.size);
            CBank* bank = (CBank*)bank_buf.data();
            if( bank->valid_fast() ){
                const uint32_t crc = bank->calc_crc();

                const uint32_t size = bank->size();

                if( crc == sbi.info.crc ){

                    uint64_t bank_uid = ((uint64_t)crc << 32) | (uint64_t)size;
                    if (m_seen_bank_ids.count(bank_uid)) {
                        logger->debug(
                            "Skipping duplicate/mirror Bank (via slot) at {:012x}, crc {:08x}, size {:7x}",
                            (uint64_t)sbi.info.offset, crc, size
                        );
                        m_checked_offsets.insert(sbi.info.offset);
                        continue;
                    }
                    m_seen_bank_ids.insert(bank_uid);
                    
                    logger->info("Found Bank at {:12x}, crc {:08x}, size {:7x} {}", (uint64_t)sbi.info.offset, crc, bank->size(), process_bank(bank, crc, sbi.info.offset));
                    save_bank({crc, sbi.info.offset, bank->size()});
                    m_checked_offsets.insert(sbi.info.offset);
                    // do not call found("banks") here because the bank was found implicitly
                }
            }
        }
    }
}

uint32_t ScannerV2::calc_bank_crc(const buf_t& buf, off_t file_offset, size_t buf_pos){
    buf_t tmp;
    CBank* bank = (CBank*)(buf.data() + buf_pos);
    if( bank->size() + buf_pos >= buf.size() ){
        tmp.resize(bank->size());
        m_reader.read_at(file_offset + buf_pos, tmp.data(), bank->size());
        bank = (CBank*)tmp.data();
    }
    return bank->calc_crc();
}

void ScannerV2::set_bitmap(off_t offset, size_t size) {
    if (!m_bitmap) {
        return; // bitmap not initialized
    }

    size_t start_block = offset / BITMAP_BLOCK_SIZE;
    size_t end_block = (offset + size - 1) / BITMAP_BLOCK_SIZE;
    m_bitmap->set_range(start_block, end_block + 1);
}

static inline bool is_zlib_header(const uint8_t* data);

bool ScannerV2::check_data(const buf_t& buf, off_t file_offset, size_t buf_pos) {

    if (check_data_lz4(buf, file_offset, buf_pos))
        return true;

    if (check_data_zlib(buf, file_offset, buf_pos))
        return true;

    if (check_data_xml(buf, file_offset, buf_pos))
        return true;


    // bruteforce through each keyset.
    if (!m_aes_ciphers.empty()) {
        for (const auto& [id, c] : m_aes_ciphers) {
            if (!c) continue;

            std::array<uint8_t, 16> dec{};
            std::memcpy(dec.data(), buf.data() + buf_pos, dec.size());
            try {
                c->decrypt(dec.data(), dec.size(), false);
            } catch (const std::exception&) {
                continue;
            }
            // check if we got any matches (for now only LZ4 encrypted blocks or uncompressed summary.xml is supported, otherwise the speed will be considerably slow because of false positives from the zlib check)
            const lz_hdr* plz = reinterpret_cast<const lz_hdr*>(dec.data());
            if (plz->valid() && check_data_lz4(buf, file_offset, buf_pos, c.get(), &id)) {
                return true;
            }

            static const std::string summary_head = "<OibSummary>";
            if (dec.size() >= summary_head.size() && std::memcmp(dec.data(), summary_head.data(), summary_head.size()) == 0) {
                if (check_data_xml(buf, file_offset, buf_pos, c.get(), &id)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static inline bool is_valid_xml_char(const uint8_t c) {
    return c >= 0x20 || c == 9 || c == 10 || c == 13;
}

// find uncompressed summary.xml
bool ScannerV2::check_data_xml(const buf_t& buf, off_t file_offset, size_t buf_pos, crypto::AES256 const* cipher, const Veeam::VBK::digest_t* keyset_id) {
    static const std::string summary_head = "<OibSummary>";
    static const std::string summary_tail = "</OibSummary>";

    const off_t data_offset = file_offset + buf_pos;

    const uint8_t* data_ptr = buf.data() + buf_pos;
    size_t data_size = buf.size() - buf_pos;
    buf_t tmp;

    if (cipher) {
        const size_t remaining = (static_cast<uint64_t>(data_offset) < m_reader.size())
            ? m_reader.size() - static_cast<size_t>(data_offset)
            : 0;
        size_t read_size = std::min(static_cast<size_t>(5 * 1024 * 1024), remaining);
        if (read_size < summary_head.size()) {
            return false;
        }
        tmp.resize(read_size);
        m_reader.read_at(data_offset, tmp.data(), read_size);
        cipher->decrypt(tmp, false, 0);
        data_ptr = tmp.data();
        data_size = tmp.size();
    }

    if (data_size < summary_head.size()) {
        return false;
    }

    if (memcmp(data_ptr, summary_head.data(), summary_head.size()) != 0) {
        return false; // not a summary.xml
    }

    auto it = std::search(data_ptr + summary_head.size(), data_ptr + data_size, summary_tail.begin(), summary_tail.end());

    if (it == data_ptr + data_size && std::all_of(data_ptr, data_ptr + data_size, is_valid_xml_char)) {
        logger->warn_once("{:x}: Found summary.xml without closing tag, TODO: read further", data_offset);
        return false;
    }

    if (it != data_ptr + data_size && std::all_of(data_ptr, it, is_valid_xml_char)) {
        int size = static_cast<int>(std::distance(data_ptr, it + summary_tail.size()));
        uint32_t crc = vcrc32(0, data_ptr, size);
        found("raw blocks");
        add_good_block(data_offset, size, size, m_md5.Calculate(data_ptr, size), crc, "NONE", keyset_id);
        set_bitmap(data_offset, size); // mark the block as occupied
        return true;
    }
    return false;
}


void ScannerV2::add_good_block(off_t offset, int comp_size, int raw_size, digest_t digest, uint32_t crc, const std::string& comp_type, const Veeam::VBK::digest_t* keyset_id) {
    // if second column equals 3rd column => data is not compressed
    // if second column is positive       => it's the compressed size
    // if it's negative                   => it's the LZ4 return code
    const std::string keyset_str = keyset_id ? fmt::format("{}", *keyset_id) : std::string();
    std::string line;

    line = fmt::format(
        "{:012x};{:06x};{:06x};{};{:08x}",
        offset, comp_size, raw_size, digest, crc
    );
    if (!comp_type.empty()) {
        line += fmt::format(";{}", comp_type);
    }
    if (!keyset_str.empty()) {
        line += fmt::format(";{}", keyset_str);
    }
    line += "\n";
    m_good_blocks_csv << line;
}
// lz_hdr is aligned on page boundary
bool ScannerV2::check_data_lz4(const buf_t& buf, off_t file_offset, size_t buf_pos, const crypto::AES256 * cipher, const Veeam::VBK::digest_t* keyset_id) {
    const off_t data_offset = file_offset + buf_pos;

    const lz_hdr* plz = (const lz_hdr*)(buf.data() + buf_pos);
    buf_t tmp;
    size_t input_size = 0;

    // decrypt first 16 bytes to validate header if encrypted
    if (cipher) {
        tmp.assign(buf.data() + buf_pos, buf.data() + buf_pos + 16);
        cipher->decrypt(tmp, false, 0);
        plz = reinterpret_cast<const lz_hdr*>(tmp.data());
    }

    if (!plz->valid()) {
        return false;
    }

    size_t max_comp_size = LZ4_COMPRESSBOUND(plz->srcSize);
    if (cipher) {
        max_comp_size = (max_comp_size + 15) & ~15;
    }
    input_size = max_comp_size;

    if (max_comp_size + buf_pos >= buf.size()) {
        tmp.resize(max_comp_size);
        m_reader.read_at(data_offset, tmp.data(), max_comp_size);
        if (cipher) {
            cipher->decrypt(tmp, false, 0);
        }
        plz = (const lz_hdr*)tmp.data();
        if (!plz->valid()) {
            logger->warn_once("{:x}: Invalid lz_hdr on 2nd read, but was valid on 1st", data_offset);
            return false;
        }
    } else if (cipher) {
        // no re-read; copy buffered ciphertext and decrypt it
        tmp.resize(max_comp_size);
        std::memcpy(tmp.data(), buf.data() + buf_pos, max_comp_size);
        cipher->decrypt(tmp, false, 0);
        plz = (const lz_hdr*)tmp.data();
    }

    int comp_size = 0;
    int lz4res = LZ4_decompress_safe_partial_ex(
        (const char*)(plz+1),
        (char*)m_decomp_buf.data(),
        input_size,
        plz->srcSize,
        plz->srcSize,
        &comp_size
        );

    uint32_t crc = vcrc32(0, m_decomp_buf.data(), plz->srcSize);
    // logger->trace("check_data: data_offset: {:x}, srcSize: {:x}, crc: {:08x}, plz->crc: {:08x}", data_offset, plz->srcSize, crc, plz->crc);
    if (crc == plz->crc && static_cast<uint32_t>(lz4res) == plz->srcSize) {
        found("lz4 blocks");
        add_good_block(data_offset, comp_size, plz->srcSize, m_md5.Calculate(m_decomp_buf.data(), plz->srcSize), plz->crc, "LZ4", keyset_id);
        set_bitmap(data_offset, comp_size + sizeof(lz_hdr)); // mark the block as occupied
        return true;
    }

    // only saving when (lz4res != plz->srcSize) bc if they are equal, but CRC is not, it means that the block is corrupted,
    // but somehow is still valid LZ4, so we can't pinpoint the end of the "truely valid" data
    if (static_cast<uint32_t>(lz4res) != plz->srcSize) {
        found("bad blocks");

        if (comp_size == 0 && lz4res < 0)
            comp_size = -lz4res;

        std::string line = fmt::format("{:012x};{:06x};{:06x};{:06x}\n", data_offset, plz->srcSize, comp_size, (uint32_t)lz4res);
        m_bad_blocks_csv << line;
    }
    return false;
}

// Basic zlib header validation
// First byte: compression method (lower 4 bits) and compression info (upper 4 bits)
// Second byte: flags including compression level, preset dictionary flag, etc.
inline bool is_zlib_header(const uint8_t* data) {
    const uint8_t first_byte = data[0];
    const uint8_t second_byte = data[1];
    return (first_byte & 0x0F) == 0x08 && // deflate compression method
           ((first_byte * 256 + second_byte) % 31) == 0 && // header checksum
           ((first_byte >> 4) & 0x0F) <= 7 && // window size <= 15 (32KB)
           (second_byte & 0x20) == 0; // no preset dictionary (Veeam doesn't use this)
}

bool try_inflate(const uint8_t* data, size_t data_size, std::vector<uint8_t>& out_buf, size_t& comp_size, size_t& decomp_size) {
    z_stream strm = {};
    strm.avail_in = data_size;
    strm.next_in = (Bytef*)data;
    strm.avail_out = out_buf.size();
    strm.next_out = (Bytef*)out_buf.data();

    // Use the same window size as Veeam (from ExtractContext.cpp)
    int ret = inflateInit2(&strm, 15);
    if (ret != Z_OK) {
        return false;
    }

    ret = inflate(&strm, Z_FINISH);
    int inflate_ret = ret;
    // check inflateEnd first to always have it called
    if (inflateEnd(&strm) == Z_OK && inflate_ret == Z_STREAM_END && strm.total_out > 0 && strm.total_out <= BLOCK_SIZE) {
        comp_size = data_size - strm.avail_in;
        decomp_size = strm.total_out;
        return true;
    }
    return false;
}

// Check for zlib compressed data blocks
// input: at least a PAGE_SIZE of data
bool ScannerV2::check_data_zlib(const buf_t& buf, off_t file_offset, size_t buf_pos) {
    const off_t data_offset = file_offset + buf_pos;
    
    if (!is_zlib_header(buf.data() + buf_pos)) {
        return false;
    }

    // Example of a valid block found in the wild:
    // <BlockDescriptor location=4, usageCnt=1, offset=ecb0df4000, allocSize=101000, dedup=1, digest=d4a8c2b2c4f5600939f2409e8eed185f, compType=4, compSize=100146, srcSize=100000>
    // zlib-compressed, compSize is greater than srcSize and BLOCK_SIZE
    const size_t max_comp_size = BLOCK_SIZE + 0x200;
    size_t actual_comp_size = 0, decomp_size = 0;
    if( max_comp_size + buf_pos >= buf.size() ){
        buf_t tmp;
        tmp.resize(max_comp_size);
        m_reader.read_at(data_offset, tmp.data(), max_comp_size);
        if (!is_zlib_header(tmp.data())) {
            logger->warn_once("{:x}: Invalid zlib hdr on 2nd read, but was valid on 1st", data_offset);
            return false;
        }
        if( try_inflate(tmp.data(), max_comp_size, m_decomp_buf, actual_comp_size, decomp_size) ){
            found("zlib blocks");
            add_good_block(data_offset, actual_comp_size, decomp_size, m_md5.Calculate(m_decomp_buf.data(), decomp_size), 0, "ZLIB");
            set_bitmap(data_offset, actual_comp_size); // mark the compressed block as occupied
            return true;
        }
        return false;
    }

    if( try_inflate(buf.data() + buf_pos, max_comp_size, m_decomp_buf, actual_comp_size, decomp_size) ){
        found("zlib blocks");
        add_good_block(data_offset, actual_comp_size, decomp_size, m_md5.Calculate(m_decomp_buf.data(), decomp_size), 0, "ZLIB");
        set_bitmap(data_offset, actual_comp_size); // mark the compressed block as occupied
        return true;
    }
    
    return false;
}