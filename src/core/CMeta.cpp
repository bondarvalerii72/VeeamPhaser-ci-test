/**
 * @file CMeta.cpp
 * @brief Implementation of the CMeta class for Veeam metadata management.
 *
 * This file provides comprehensive functionality for loading, parsing, and navigating
 * Veeam metadata structures. It supports multiple metadata sources (legacy files, slots,
 * banks), handles different metadata versions, manages page stacks, and provides file
 * enumeration capabilities. The metadata is organized in pages that form a hierarchical
 * structure for file and directory information.
 */

#include "CMeta.hpp"
#include "io/Reader.hpp"
#include "utils/common.hpp"
#include <unordered_set>
#include <optional>
#include <fstream>
#include <cerrno>
#include <iterator>
#include <array>
#include <spdlog/fmt/fmt.h>

#include <spdlog/fmt/bin_to_hex.h>

using namespace Veeam::VBK;
namespace fs = std::filesystem;

extern std::shared_ptr<Logger> logger;

bool vObtainMetaID(buf_t& meta, uint32_t& metaID);


/**
 * @brief Constructs a CMeta object and loads metadata from the specified source.
 *
 * Initializes the metadata loader and determines the source type (auto-detect, slot,
 * bank, or legacy) based on file extension or explicit parameter. The constructor
 * automatically imports the metadata using the appropriate method.
 *
 * @param fname Path to the metadata source file.
 * @param ignore_errors If true, continue processing despite validation errors.
 * @param offset File offset where metadata begins (for embedded metadata).
 * @param meta_src Metadata source type (MS_AUTO for auto-detection).
 */
CMeta::CMeta(const std::filesystem::path& fname, bool ignore_errors, off_t offset, EMetaSource meta_src, const std::string& password, bool dump_keysets, std::optional<std::filesystem::path> keysets_same_file, bool dump_session_only){
    m_ignore_errors = ignore_errors;
    m_password = password;
    m_dump_keysets = dump_keysets;
    m_dump_session_only = dump_session_only;
    m_source_path = fname;
    m_keysets_same_file = std::move(keysets_same_file);

    Reader reader(fname);

    if( meta_src == MS_AUTO ){
        if( fname.extension() == ".slot" )
            meta_src = MS_SLOT;
        else if( fname.extension() == ".bank" )
            meta_src = MS_BANK;
    }

    m_meta_source = meta_src;

    switch (meta_src) {
        case MS_SLOT:
            import_slot(reader, offset);
            break;
        case MS_BANK:
            import_bank(reader, offset);
            break;
        default:
            import_legacy(reader, offset);
            break;
    }

    logger->debug("Metadata loaded");
}

/**
 * @brief Imports metadata from a single bank structure.
 *
 * Reads a bank from the specified offset and validates it. This is primarily
 * used for debugging and testing single bank structures. Banks are Veeam's
 * organizational unit for metadata pages.
 *
 * @param reader Reader object positioned at the bank data.
 * @param offset File offset where the bank begins.
 */
void CMeta::import_bank(Reader& reader, const off_t offset) {
    logger->debug("MetaData is from Bank");
    size_t bank_size = reader.size() - offset;
    buf_t buf( bank_size );
    reader.read_at( offset, buf );
    m_banks.push_back(buf);

    int i = m_banks.size() - 1;
    CBank* bank = (CBank*)buf.data();
    int valid_fast = bank->valid_fast();
    int valid_slow = bank->valid_slow(bank_size);
    logger->debug("{}Bank[{}]: {} valid_fast={} valid_slow={}{}", (valid_fast && valid_slow) ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,i, bank->to_string(), valid_fast, valid_slow, ANSI_COLOR_RESET);

    if( m_dump_keysets && !bank->is_encrypted()){
        load_keysets_from_bank(*bank, m_password, true);
    }
}

/**
 * @brief Imports metadata from a slot structure.
 *
 * Reads a slot header and all associated banks from the VBK file. A slot represents
 * a complete backup snapshot with multiple banks containing the actual metadata pages.
 * This method validates the slot CRC and each bank's integrity before loading.
 *
 * @param reader Reader object for accessing the VBK file.
 * @param offset File offset where the slot begins.
 * @throws std::runtime_error If slot size is invalid or reading fails.
 */
void CMeta::import_slot(Reader& reader, const off_t offset) {
    logger->debug("MetaData is from Slot");
    buf_t buf( PAGE_SIZE );
    if (reader.read_at(offset, buf) != PAGE_SIZE) {
        throw std::runtime_error("Failed to read Slot page 0");
    }
    CSlot* slot = (CSlot*)buf.data();
    logger->trace("loading {}", slot->to_string());
    if( slot->size() < PAGE_SIZE ){
        throw std::runtime_error(fmt::format("Invalid Slot size: {:#x}", slot->size()));
    }
    buf.resize(slot->size()); // resize() may invalidate slot pointer
    slot = (CSlot*)buf.data();
    size_t to_read = slot->size() - PAGE_SIZE;
    if (reader.read_at(offset + PAGE_SIZE, buf.data()+PAGE_SIZE, to_read) != to_read) {
        throw std::runtime_error("Failed to read Slot page 1+");
    }

    int valid_fast = slot->valid_fast();
    int valid_crc = slot->valid_crc();
    logger->debug("{}Slot: {} valid_fast={} valid_crc={}{}",
        (valid_fast && valid_crc) ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,
        slot->to_string(), valid_fast, valid_crc,
        ANSI_COLOR_RESET
        );
    logger->debug("  {}", slot->snapshotDescriptor.to_string());

    m_banks.resize(slot->allocated_banks);

    for(uint32_t i=0; i<slot->allocated_banks; i++){
        if( slot->bankInfos[i].size == 0 ){
            continue;
        }
        m_banks[i].resize(slot->bankInfos[i].size);
        size_t nread = reader.read_at(slot->bankInfos[i].offset, m_banks[i]);
        if( nread != slot->bankInfos[i].size ){
            logger->error("Failed to read bank #{}: {:x} != {:x}", i, nread, (uint32_t)slot->bankInfos[i].size);
            continue;
        }
    }

    // Load encryption keys if present

    if (slot->snapshotDescriptor.objRefs.CryptoStoreRootPage.valid()) {
        logger->info("Slot indicates encrypted metadata (CryptoStoreRootPage={}) - attempting to load keysets.", slot->snapshotDescriptor.objRefs.CryptoStoreRootPage);

        const auto bank_id = slot->snapshotDescriptor.objRefs.CryptoStoreRootPage.bank_id;
        if(slot->snapshotDescriptor.objRefs.CryptoStoreRootPage.valid()){
            auto* bank = (CBank*)m_banks[bank_id].data();
            load_keysets_from_bank(*bank, m_password, false);
        } else {
            log_or_die(false, "Invalid CryptoStoreRootPage {}", bank_id, slot->snapshotDescriptor.objRefs.CryptoStoreRootPage.page_id);
        }
        for(uint32_t i=0; i<slot->allocated_banks; i++){
            if( slot->bankInfos[i].size == 0 ){
                continue;
            }
            CBank* bank = (CBank*)m_banks[i].data();
            int valid_fast = bank->valid_fast();
            if (valid_fast && bank->is_encrypted()){
                const auto* cipher = get_aes_cipher(bank->header_page.keyset_id);

                if( cipher ){
                    const auto encr_size = bank->encr_size();
                    std::vector<uint8_t> bank_data(reinterpret_cast<uint8_t*>(bank->data_pages[0].data), reinterpret_cast<uint8_t*>(bank->data_pages[0].data) + encr_size);
                    try
                    {
                        cipher->decrypt(bank_data);
                    }
                    catch(const std::exception& e)
                    {
                        logger->error("  Failed to decrypt Bank[{}] keyset {}: {}", i, bank->header_page.keyset_id, e.what());
                        continue;
                    }
                    //copy the decrypted data, and null out the padding bytes at the end
                    auto padding_size = encr_size - bank_data.size();
                    memcpy(bank->data_pages[0].data, bank_data.data(), bank_data.size());
                    memset(bank->data_pages[0].data + bank_data.size(), 0, padding_size);
                    bank->header_page.encr_size = 0;
                    bank->header_page.keyset_id = digest_t(0);
                } else {
                    logger->warn("  No keyset found for Bank[{}] keyset {}", i, bank->header_page.keyset_id);
                }
            }
        }

    }

    for(uint32_t i=0; i<slot->allocated_banks; i++){
        if( slot->bankInfos[i].size == 0 ){
            continue;
        }
        //bank is already loaded at this point, so just validate
        CBank* bank = (CBank*)m_banks[i].data();
        int valid_fast = bank->valid_fast();
        int valid_slow = bank->valid_slow(slot->bankInfos[i].size);
        std::string tail;
        if (!valid_fast || !valid_slow)
            tail = fmt::format(" bank_info: {}", slot->bankInfos[i].to_string());
        logger->debug("{}Bank[{}]: {} valid_fast={} valid_slow={}{}{}",
            (valid_fast && valid_slow) ? ANSI_COLOR_GREEN : ANSI_COLOR_RED,
            i, bank->to_string(), valid_fast, valid_slow, tail,
            ANSI_COLOR_RESET
            );
    }
}

bool CMeta::load_keysets_from_bank(const CBank& bank, const std::string& password, bool is_bank_source) {

    if( password.empty() ){
        log_or_die(false, "Need a password to decrypt this backup.");
        return false;
    }

    // crypto root lives at page 0 of the relevant bank in current format
    const auto* crypto_data_page = &bank.data_pages[0];
    if( !crypto_data_page->is_metavec2_start(0) ){
        log_or_die(false, "CryptoStoreRootPage isn't a metavec2 start on page 0.");
        return false;
    }

    const PhysPageId keyset_page_ppi = *(PhysPageId*)(crypto_data_page->data + 0x10);
    const int bank_index = keyset_page_ppi.bank_id;
    if( bank_index < 0 ){
        log_or_die(false, "Keyset points to bank {} which is invalid", bank_index);
        return false;
    }

    if( is_bank_source ){ // stash standalone bank so get_page can see it at the right index
        buf_t bank_buf(bank.size());
        memcpy(bank_buf.data(), &bank, bank_buf.size());
        if( m_banks.size() <= (uint32_t)bank_index ){
            m_banks.resize(bank_index + 1);
        }
        m_banks[bank_index] = std::move(bank_buf);
    }

    // keyset page is raw binary; let get_page feed it back untouched
    buf_t keyset_page;
    if( !get_page(keyset_page_ppi, keyset_page) ){
        log_or_die(false, "Couldn't read SKeySetRec page at {}", keyset_page_ppi);
        return false;
    }

    auto keyset_data = reinterpret_cast<const CBank::DataPage*>(keyset_page.data());
    if( !keyset_data->has_valid<SKeySetRec>() ){
        log_or_die(false, "SKeySetRec page didn't contain any valid entries.");
        return false;
    }

    std::map<SKeySetRec::EKeyRole, SKeySetRec> keysets;
    keyset_page.for_each<SKeySetRec>([&](SKeySetRec* rec) {
        if( rec->valid() ){
            keysets[rec->role] = *rec;
            logger->debug("  Keyset found: {}", rec->to_string());
        }
        return true;
    });

    if( keysets.empty() ){
        log_or_die(false, "No keysets found in bank {}", bank_index);
        return false;
    }

    auto bytes_to_aes = [&](const std::vector<uint8_t>& raw, const char* label) -> std::optional<crypto::aes_key> {
        if( raw.size() < 48 ){
            log_or_die(false, "{} decrypted data is too short ({} bytes)", label, raw.size());
            return std::nullopt;
        }
        crypto::aes_key key{};
        memcpy(key.key, raw.data() + raw.size() - 48, 32);
        memcpy(key.iv,  raw.data() + raw.size() - 16, 16);
        return key;
    };

    m_aes_keys.clear();
    m_aes_ciphers.clear();
    m_session_key.reset();

    if( keysets.count(SKeySetRec::KR_POLICY) ){
        logger->info("Decrypting keysets with KR_POLICY (RSA)");

        buf_t rsa_blob;
        if( !get_page(keysets[SKeySetRec::KR_POLICY].restore_rec_blobs_loc, rsa_blob) ){
            log_or_die(false, "Couldn't read KR_POLICY blob at {}", keysets[SKeySetRec::KR_POLICY].restore_rec_blobs_loc);
            return false;
        }
        const auto* rsa_live = reinterpret_cast<const SRestoreRecBlob*>(rsa_blob.data());
        if( !rsa_live->is_pbkdf2_derived() ){
            log_or_die(false, "RSA key blob isn't valid");
            return false;
        }

        const std::vector<uint8_t> rsa_salt(rsa_live->salt(), rsa_live->salt() + rsa_live->salt_size);
        const std::vector<uint8_t> encrypted_rsa_key(rsa_live->encrypted_key(), rsa_live->encrypted_key() + rsa_live->encrypted_key_size);
        logger->trace("RSA salt {}", spdlog::to_hex(rsa_salt));
        logger->trace("RSA encrypted key {}", spdlog::to_hex(encrypted_rsa_key));
        auto decrypted_rsa_key = crypto::decrypt_pbkdf2_data(password, rsa_salt, encrypted_rsa_key);
        logger->trace("RSA private key bytes: {}", spdlog::to_hex(decrypted_rsa_key));
        std::string private_key_pem(decrypted_rsa_key.begin() + 8, decrypted_rsa_key.end());

        if( !keysets.count(SKeySetRec::KR_AGENT) ){
            log_or_die(false, "No KR_AGENT keyset found");
            return false;
        }

        buf_t agent_blob;
        if( !get_page(keysets[SKeySetRec::KR_AGENT].restore_rec_blobs_loc, agent_blob) ){
            log_or_die(false, "Couldn't read KR_AGENT blob at {}", keysets[SKeySetRec::KR_AGENT].restore_rec_blobs_loc);
            return false;
        }
        const auto* agent_live = reinterpret_cast<const SRestoreRecBlob*>(agent_blob.data());

        std::vector<uint8_t> encrypted_agent_key(std::reverse_iterator<const uint8_t*>(agent_live->encrypted_key() + agent_live->encrypted_key_size),
                                                 std::reverse_iterator<const uint8_t*>(agent_live->encrypted_key()));

        auto decrypted_agent_key = crypto::rsa_decrypt(private_key_pem, encrypted_agent_key);
        logger->trace("KR_AGENT key bytes: {}", spdlog::to_hex(decrypted_agent_key));

        auto agent_aes_key = bytes_to_aes(decrypted_agent_key, "KR_AGENT");
        if( !agent_aes_key ){
            return false;
        }

        if( !keysets.count(SKeySetRec::KR_STORAGE) ){
            log_or_die(false, "No KR_STORAGE keyset found");
            return false;
        }

        buf_t storage_blob;
        if( !get_page(keysets[SKeySetRec::KR_STORAGE].restore_rec_blobs_loc, storage_blob) ){
            log_or_die(false, "Couldn't read KR_STORAGE blob at {}", keysets[SKeySetRec::KR_STORAGE].restore_rec_blobs_loc);
            return false;
        }
        const auto* storage_live = reinterpret_cast<const SRestoreRecBlob*>(storage_blob.data());

        std::vector<uint8_t> decrypted_storage_key(storage_live->encrypted_key(), storage_live->encrypted_key() + storage_live->encrypted_key_size);
        crypto::AES256 agent_cipher(agent_aes_key->key, agent_aes_key->iv);
        agent_cipher.decrypt(decrypted_storage_key);
        logger->trace("KR_STORAGE key bytes: {}", spdlog::to_hex(decrypted_storage_key));

        auto storage_aes_key = bytes_to_aes(decrypted_storage_key, "KR_STORAGE");
        if( !storage_aes_key ){
            return false;
        }
        const auto storage_id = Veeam::VBK::digest_t(keysets[SKeySetRec::KR_STORAGE].uuid);
        crypto::register_keyset(m_aes_keys, m_aes_ciphers, storage_id, *storage_aes_key);
        
        const auto* storage_cipher = get_aes_cipher(storage_id);
        if (!storage_cipher) {
            log_or_die(false, "Failed to register cipher for KR_STORAGE keyset {}", storage_id);
            return false;
        }

        if( keysets.count(SKeySetRec::KR_META) ){
            buf_t meta_blob;
            if( get_page(keysets[SKeySetRec::KR_META].restore_rec_blobs_loc, meta_blob) ){
                const auto* meta_live = reinterpret_cast<const SRestoreRecBlob*>(meta_blob.data());
                std::vector<uint8_t> decrypted_meta_key(meta_live->encrypted_key(), meta_live->encrypted_key() + meta_live->encrypted_key_size);
                storage_cipher->decrypt(decrypted_meta_key);
                logger->trace("KR_META key bytes: {}", spdlog::to_hex(decrypted_meta_key));

                if( auto meta_aes_key = bytes_to_aes(decrypted_meta_key, "KR_META") ){
                    const auto meta_id = Veeam::VBK::digest_t(keysets[SKeySetRec::KR_META].uuid);
                    crypto::register_keyset(m_aes_keys, m_aes_ciphers, meta_id, *meta_aes_key);

                    if( keysets.count(SKeySetRec::KR_SESSION) ){
                        buf_t session_blob;
                        if( get_page(keysets[SKeySetRec::KR_SESSION].restore_rec_blobs_loc, session_blob) ){
                            const auto* session_live = reinterpret_cast<const SRestoreRecBlob*>(session_blob.data());
                            std::vector<uint8_t> decrypted_session_key(session_live->encrypted_key(), session_live->encrypted_key() + session_live->encrypted_key_size);
                            storage_cipher->decrypt(decrypted_session_key);

                            if( auto session_aes_key = bytes_to_aes(decrypted_session_key, "KR_SESSION") ){
                                const auto session_id = Veeam::VBK::digest_t(keysets[SKeySetRec::KR_SESSION].uuid);
                                crypto::register_keyset(m_aes_keys, m_aes_ciphers, session_id, *session_aes_key);
                                m_session_key = session_id;
                            }
                        }
                    }
                }
            }
        }
    } else if( keysets.count(SKeySetRec::KR_STORAGE) ){
        logger->info("Decrypting keysets with KR_STORAGE (AES)");

        buf_t storage_blob;
        if( !get_page(keysets[SKeySetRec::KR_STORAGE].restore_rec_blobs_loc, storage_blob) ){
            log_or_die(false, "Couldn't read KR_STORAGE blob at {}", keysets[SKeySetRec::KR_STORAGE].restore_rec_blobs_loc);
            return false;
        }
        const auto* storage_live = reinterpret_cast<const SRestoreRecBlob*>(storage_blob.data());

        const std::vector<uint8_t> storage_salt(storage_live->salt(), storage_live->salt() + storage_live->salt_size);
        const std::vector<uint8_t> encrypted_storage_key(storage_live->encrypted_key(), storage_live->encrypted_key() + storage_live->encrypted_key_size);
        
        auto decrypted_storage_key = crypto::decrypt_pbkdf2_data(password, storage_salt, encrypted_storage_key);
        

        auto storage_aes_key = bytes_to_aes(decrypted_storage_key, "KR_STORAGE");
        if( !storage_aes_key ){
            return false;
        }

        const auto storage_id = keysets[SKeySetRec::KR_STORAGE].uuid;
        crypto::register_keyset(m_aes_keys, m_aes_ciphers, storage_id, *storage_aes_key);
        const auto* storage_cipher = get_aes_cipher(storage_id);
        if (!storage_cipher) {
            log_or_die(false, "Failed to register cipher for KR_STORAGE keyset {}", storage_id);
            return false;
        }
        logger->trace("KR_STORAGE key {}, iv {}", spdlog::to_hex(std::begin(storage_aes_key->key), std::end(storage_aes_key->key)), spdlog::to_hex(std::begin(storage_aes_key->iv), std::end(storage_aes_key->iv)));


        if( keysets.count(SKeySetRec::KR_META) ){
            buf_t meta_blob;
            if( get_page(keysets[SKeySetRec::KR_META].restore_rec_blobs_loc, meta_blob) ){
                const auto* meta_live = reinterpret_cast<const SRestoreRecBlob*>(meta_blob.data());
                std::vector<uint8_t> decrypted_meta_key(meta_live->encrypted_key(), meta_live->encrypted_key() + meta_live->encrypted_key_size);
                storage_cipher->decrypt(decrypted_meta_key);
                

                if( auto meta_aes_key = bytes_to_aes(decrypted_meta_key, "KR_META") ){
                    logger->trace("KR_META key {}, iv {}", spdlog::to_hex(std::begin(meta_aes_key->key), std::end(meta_aes_key->key)), spdlog::to_hex(std::begin(meta_aes_key->iv), std::end(meta_aes_key->iv)));
                    const auto meta_id = keysets[SKeySetRec::KR_META].uuid;
                    crypto::register_keyset(m_aes_keys, m_aes_ciphers, meta_id, *meta_aes_key);

                    if( keysets.count(SKeySetRec::KR_SESSION) ){
                        buf_t session_blob;
                        if( get_page(keysets[SKeySetRec::KR_SESSION].restore_rec_blobs_loc, session_blob) ){
                            const auto* session_live = reinterpret_cast<const SRestoreRecBlob*>(session_blob.data());
                            std::vector<uint8_t> decrypted_session_key(session_live->encrypted_key(), session_live->encrypted_key() + session_live->encrypted_key_size);
                            storage_cipher->decrypt(decrypted_session_key);

                            if( auto session_aes_key = bytes_to_aes(decrypted_session_key, "KR_SESSION") ){
                                const auto session_id = Veeam::VBK::digest_t(keysets[SKeySetRec::KR_SESSION].uuid);
                                crypto::register_keyset(m_aes_keys, m_aes_ciphers, session_id, *session_aes_key);
                                m_session_key = session_id;
                            }
                        }
                    }
                }
            }
        }
    } else {
        log_or_die(false, "Found neither KR_POLICY nor KR_STORAGE keysets");
        return false;
    }


    if( !m_aes_keys.empty() ){
        logger->info("Loaded {} encryption keyset{}.", m_aes_keys.size(), m_aes_keys.size() == 1 ? "" : "s");
        for( const auto& [uuid, aes] : m_aes_keys ){
            logger->trace("  keyset {} \n key {}\n iv {}\n",
                fmt::format("{}", uuid),
                spdlog::to_hex(std::begin(aes.key), std::end(aes.key)),
                spdlog::to_hex(std::begin(aes.iv), std::end(aes.iv)));
        }
        if( m_dump_keysets ){
            dump_loaded_keysets();
        }
    }

    return !m_aes_keys.empty();
}

void CMeta::dump_loaded_keysets() const {
    const bool session_only = m_dump_session_only;

    auto should_dump = [&](const Veeam::VBK::digest_t& id) {
        return !session_only || (m_session_key && *m_session_key == id);
    };

    size_t dump_count = 0;
    for (const auto& [uuid, _] : m_aes_keys) {
        if (should_dump(uuid)) {
            ++dump_count;
        }
    }

    if( dump_count == 0 ){
        if (session_only) {
            logger->info("No session aes keysets loaded.");
        } else {
            logger->info("No aes keysets loaded.");
        }
        return;
    }

    fs::path out_path;
    const bool append_mode = (m_keysets_same_file && !m_keysets_same_file->empty());
    if( append_mode ){
        out_path = *m_keysets_same_file;
    } else {
        const fs::path source_name = m_source_path.empty() ? fs::path("metadata") : m_source_path.filename();
        const std::string base_name = sanitize_fname(source_name.string());
        const fs::path out_dir = get_out_dir(m_source_path.empty() ? fs::path(base_name) : m_source_path);
        out_path = out_dir / (base_name + ".keysets.bin");
    }

    if( !append_mode ){
        std::ofstream of(out_path, std::ios::binary | std::ios::trunc);
        if( !of ){
            logger->error("Couldn't open {} to write keysets: {}", out_path, strerror(errno));
            return;
        }

        const uint32_t count = static_cast<uint32_t>(dump_count);
        of.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for( const auto& [uuid, aes] : m_aes_keys ){
            if (!should_dump(uuid)) {
                continue;
            }
            const __uint128_t uuid_u = uuid.value;
            of.write(reinterpret_cast<const char*>(&uuid_u), sizeof(uuid_u));
            of.write(reinterpret_cast<const char*>(aes.key), sizeof(aes.key));
            of.write(reinterpret_cast<const char*>(aes.iv), sizeof(aes.iv));
        }
        return;
    }

    std::fstream f(out_path, std::ios::in | std::ios::out | std::ios::binary);
    if( !f ){
        std::ofstream init(out_path, std::ios::binary | std::ios::trunc);
        if( !init ){
            logger->error("Couldn't open {} to write keysets: {}", out_path, strerror(errno));
            return;
        }
        const uint32_t zero = 0;
        init.write(reinterpret_cast<const char*>(&zero), sizeof(zero));
        init.close();
        f.open(out_path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if( !f ){
        logger->error("Couldn't open {} to write keysets: {}", out_path, strerror(errno));
        return;
    }

    uint32_t count = 0;
    f.seekg(0);
    f.read(reinterpret_cast<char*>(&count), sizeof(count));

    f.seekp(0, std::ios::end);
    for( const auto& [uuid, aes] : m_aes_keys ){
        if (!should_dump(uuid)) {
            continue;
        }
        const __uint128_t uuid_u = uuid.value;
        f.write(reinterpret_cast<const char*>(&uuid_u), sizeof(uuid_u));
        f.write(reinterpret_cast<const char*>(aes.key), sizeof(aes.key));
        f.write(reinterpret_cast<const char*>(aes.iv), sizeof(aes.iv));
        count++;
    }

    f.seekp(0);
    f.write(reinterpret_cast<const char*>(&count), sizeof(count));
}

std::optional<crypto::aes_key> CMeta::get_aes_key(const Veeam::VBK::digest_t& id) const {
    auto it = m_aes_keys.find(id);
    if( it == m_aes_keys.end() ){
        return std::nullopt;
    }
    return it->second;
}

const crypto::AES256* CMeta::get_aes_cipher(const Veeam::VBK::digest_t& id) const {
    auto it = m_aes_ciphers.find(id);
    if (it == m_aes_ciphers.end()) {
        return nullptr;
    }
    return it->second.get();
}

/**
 * @brief Imports metadata from legacy format files.
 *
 * Handles older metadata file formats (METADATA, METADATASCAN) which don't use
 * the slot/bank structure. Legacy files have a simple header followed by one or
 * more banks. This method detects the format and loads banks sequentially.
 *
 * @param reader Reader object for the metadata file.
 * @param offset File offset where metadata begins (typically 0).
 */
void CMeta::import_legacy(Reader& reader, const off_t offset) {
    size_t fs = reader.size() - offset;

    size_t curBankSize = MAX_BANK_SIZE;
    int TOCMark = 0;
    buf_t buf;

    off_t pos = offset;
    if( fs % 2 == 1 ) {
        pos++;
        TOCMark = 1;
        fs--;
    }

    logger->debug("MetaData is from {}", TOCMark ? "TOC" : "bruteforcing");

    while( pos <= fs+TOCMark-1 ){
        uint16_t nPages = 0;
        reader.read_at(pos, &nPages, sizeof(nPages));
        curBankSize = (nPages+2) * PAGE_SIZE;
        buf.resize(curBankSize);
        reader.read_at(pos, buf);
        pos += curBankSize;

        if( TOCMark ){
            logger->debug("Loading Bank {:04x} Size {:6x} @ {:8x}", m_banks.size(), curBankSize, pos);
            m_banks.push_back(buf);
        } else {
            uint32_t pd = 0;
            vObtainMetaID(buf, pd);
            logger->debug("Loading Bank {:04x} Size {:6x} @ {:8x}", pd, curBankSize, pos);
            if( m_banks.size() <= pd ){
                m_banks.resize(pd+1);
            }
            m_banks[pd] = buf;
        }
    }
}

/**
 * @brief Checks if the loaded metadata uses the new format version.
 *
 * @return True if new metadata format is detected, false for legacy format.
 */
bool CMeta::is_new_version(){
    if( m_new_version == -1 ){
        detect_version();
    }
    return m_new_version == 1;
}

/**
 * @brief Auto-detects the metadata format version.
 *
 * Analyzes the loaded metadata to determine if it uses the old or new format.
 * The detection is based on patterns and structures present in the metadata pages.
 * Detection only runs once and caches the result.
 */
void CMeta::detect_version(){
    m_new_version = 0; // prevent infinite recursion
    logger->info("Fetching Page[0] - root dir for version detection");

    buf_t buf;
    if( !vFetchMDPage({0, 0}, buf, true) ){
        logger->warn("Failed to fetch root dir from Meta. assuming new_version=1");
        m_new_version = 1;
        return;
    }

    m_new_version = *(uint64_t*)(buf.data()+8) == 0;
    if( m_new_version ){
        logger->debug("NEW VEEAM FORMAT DETECTED");
//        const PhysPageId* ppi = (PhysPageId*)(buf.data()+0x10);
//        logger->debug("Redirecting to {}", *ppi);
//        if( !vFetchMDPage(ppi, buf, true) ){
//            logger->warn("Failed to fetch root dir from Meta");
//        }
    }
    logger->debug("[.] Root: {}", to_hexdump(buf));
}

/**
 * @brief Retrieves a metadata page by its physical page ID.
 *
 * Fetches the raw page data for the specified physical page ID. This is a low-level
 * function that provides direct access to metadata pages without interpretation.
 *
 * @param ppi Physical page identifier specifying bank and page numbers.
 * @param[out] dst Buffer that will receive the page data.
 * @return True if the page was successfully retrieved, false otherwise.
 */
bool CMeta::get_page(const PhysPageId ppi, buf_t& dst){
    bool result = false;
    dst.clear();

    while(true){
        if( m_banks.size() <= (uint32_t)ppi.bank_id )
            break;
        if( m_banks[ppi.bank_id].size() <= (ppi.page_id+1)*PAGE_SIZE )
            break;

        dst.resize(PAGE_SIZE);
        memcpy(dst.data(), m_banks[ppi.bank_id].data() + (ppi.page_id+1)*PAGE_SIZE, PAGE_SIZE);
        result = true;
        break;
    }

    if( result ){
        logger->trace("get_page({}) => {:n}", ppi, spdlog::to_hex(dst.begin(), dst.begin()+0x20));
    } else {
        logger->trace("get_page({}) => {}", ppi, result);
    }

    return result;
}

// see VeeamAgent's CPageStack::ctor() and PagesAllocator::read_pages()
CPageStack CMeta::get_page_stack(PhysPageId ppi0){
    logger->trace("get_page_stack({})", ppi0);
    CPageStack stack;

    PhysPageId ppi = ppi0;
    ppi_set_t visited_index_pages, visited_data_pages;
    bool first_page = true;

    buf_t page;
    while( ppi.valid() ){
        if (!get_page(ppi, page)) {
            logger->error("get_page_stack({}): failed to get page {}, stack truncated!", ppi0, ppi);
            break;
        }
        if (first_page) {
            first_page = false;
            RootPage* rp = (RootPage*)page.data();
            if( rp->page_id != ppi0 ){
                logger->error("get_page_stack({}): first page is not the root page: {} != {}", ppi0, rp->page_id, ppi0);
                break;
            }
        } else if( visited_index_pages.find(ppi) != visited_index_pages.end() ){
            logger->error("get_page_stack({}): circular reference: {}", ppi0, ppi);
            break;
        }
        visited_index_pages.insert(ppi);

        stack.add_page(page);
        ppi = *(PhysPageId*)page.data(); // next ppi is at the start of the page
    }

    return stack.finalize();
}

// legacy, don't use
bool CMeta::vFetchMDPage(const PhysPageId* ppi, buf_t& Buf, bool ignore_errors){
    if( !ppi ){
        logger->critical("vFetchMDPage: ppi=nullptr");
        return false;
    }   
    return vFetchMDPage(*ppi, Buf, ignore_errors);
}

// legacy, don't use
bool CMeta::vFetchMDPage(PhysPageId ppi, buf_t& Buf, bool ignore_errors){
    ignore_errors = ignore_errors || m_ignore_errors;
    logger->trace("vFetchMDPage({})", ppi);
    Buf.clear();

    bool is_first_page = true;
    bool done = false;
    ppi_set_t visited_pages;

    while( !done && ppi.valid() ){
        if( visited_pages.find(ppi) != visited_pages.end() ){
            log_or_die(ignore_errors, "[?] Circular reference detected: {}", ppi);
        }
        visited_pages.insert(ppi);

        if( m_banks.size() <= (uint32_t)ppi.bank_id ){
            log_or_die(ignore_errors, "[?] Meta does not contain Bank {:#x}", ppi.bank_id);
        }
        if( m_banks[ppi.bank_id].size() <= (ppi.page_id+1)*PAGE_SIZE ){
            log_or_die(ignore_errors, "[?] Bank {:#x} does not contain Page {:#x}", ppi.bank_id, ppi.page_id);
        }

        logger->trace("page data: {}", to_hexdump(m_banks[ppi.bank_id].data() + (ppi.page_id+1)*PAGE_SIZE, PAGE_SIZE));

        if( is_new_version() && is_first_page ){
            RootPage *rp = (RootPage*)&m_banks[ppi.bank_id][(ppi.page_id+1)*PAGE_SIZE];
            if( rp->page_id != ppi ){
                // case a: valid LeafPage
                // case b: corrupted RootPage

                // assume it's valid leaf page and return its data
                LeafPage* lp = (LeafPage*)rp;
                Buf.resize(lp->data_size());
                memset(Buf.data(), 0, 8);
                memcpy(Buf.data()+8, lp->data, lp->data_size()-8); // XXX this is wrong, but it's what the original code does
                done = true;
                break;
            }
        }

        Buf.resize(Buf.size() + PAGE_SIZE);
        memcpy(Buf.data() + Buf.size() - PAGE_SIZE + 8, m_banks[ppi.bank_id].data() + (ppi.page_id+1)*PAGE_SIZE + 8, PAGE_SIZE-8);

        PhysPageId* next_ppi = (PhysPageId*)(m_banks[ppi.bank_id].data() + (ppi.page_id+1)*PAGE_SIZE);
        logger->trace("vFetchMDPage(): next_ppi={}", *next_ppi);

        if( next_ppi->empty() ){
            done = true;
            break;
        } else if( next_ppi->valid() ){
            ppi = *next_ppi;
        } else {
            done = false;
            break;
        }

        is_first_page = false;
    }

    return done;
}

bool CMeta::vLoadFile(SDirItemRec* dir_item, VFile& vFileDesc){
    logger->trace("vLoadFile: {}", dir_item->to_string());
    int off;
    switch( dir_item->type ){
        case FT_INT_FIB:
        case FT_SUBFOLDER:
        case FT_INCREMENT:
            vFileDesc.type = dir_item->type;
            vFileDesc.name = dir_item->get_name();
            off = (vFileDesc.type == FT_INT_FIB || vFileDesc.type == FT_INCREMENT) ? 4 : 0;
            memcpy(&vFileDesc.attribs, (char*)dir_item+(sizeof(SDirItemRec)-0x38+off), sizeof(VFileAttribs));
            return true;

        case FT_EXT_FIB:
        case FT_PATCH:
            break;
    }
    if( (int)dir_item->type != 0 ){
        logger->error("vLoadFile: Unknown file type {:x} - {}", (int)dir_item->type, dir_item->to_string());
    }
    return false;
}

void CMeta::process_dir_page(const void* pagedata, file_cb_t cb, ppi_set_t* visited_pages){
    SDirItemRec* pDirItems = (SDirItemRec*)pagedata;
    for(size_t i=0; i<PAGE_SIZE/sizeof(SDirItemRec); i++){
        SDirItemRec* pDirItem = &pDirItems[i];
        if( !pDirItem->valid() ){
            if( pDirItem->valid_name() ){
                logger->debug("process_dir_page: invalid entry: {}", pDirItem->to_string());
            }
            break;
        }
        VFile vCurFile;
        if( vLoadFile(pDirItem, vCurFile)){
            cb(pDirItem->get_name(), vCurFile);
            if( pDirItem->is_dir() && pDirItem->valid() ){
                read_dir(pDirItem->u.dir.children_loc, std::function<void(const std::string, const VFile&)>([&](const std::string& dirname, const VFile& vfi){
                    cb(pDirItem->get_name() + "/" + dirname, vfi);
                }), visited_pages);
            }
        }
    }
}

/**
 * @brief Reads a directory and all its contents recursively.
 *
 * Fetches directory pages starting at the specified physical page ID and
 * processes all entries. Handles multi-page directories by following page chains.
 * Tracks visited pages to prevent infinite loops in corrupted metadata.
 *
 * @param dir_ppi Physical page ID of the directory to read.
 * @param cb Callback function invoked for each file/directory entry.
 * @param visited_pages Set for tracking visited pages (nullptr creates a new set).
 */
void CMeta::read_dir(PhysPageId dir_ppi, file_cb_t cb, ppi_set_t* visited_pages){
    logger->trace("read_dir({})", dir_ppi);
    const auto page_stack = get_page_stack(dir_ppi);
    logger->debug("read_dir({}): page_stack={}", dir_ppi, page_stack.to_string());
    if( page_stack ){
        buf_t page;
        for( const auto ppi : page_stack ){
            if(get_page(ppi, page)){
                if( visited_pages ){
                    if( visited_pages->find(ppi) != visited_pages->end() ){
                        continue;
                    }
                    visited_pages->insert(ppi);
                }
                process_dir_page(page.data(), cb, visited_pages);
            }
        }
    }
}

// find all files, including orphaned dirs
void CMeta::for_each_file(file_cb_t cb){
    ppi_set_t visited_pages;
    // try root dir first, it's SnapshotDescriptor.ObjRefs.MetaRootDirPage, typically 0:0
    read_dir({0, 0}, cb, &visited_pages);

    // enumerate all pages, try to find orphaned dirs
    for_each_page([&](const PhysPageId& ppi, const uint8_t* pdata){
        if( visited_pages.find(ppi) == visited_pages.end() ){
            // DO NOT update visited_pages
            bool was = false;
            process_dir_page(pdata, [&](const std::string& dirname, const VFile& vfi){
                if( !was ){
                    logger->info("found orphaned dir @ {}", ppi);
                    was = true;
                }
                cb(dirname, vfi);
            }, &visited_pages);
        }
    });

    if( m_deep_scan ){
        for( const auto& vfi : deep_scan() ){
            const auto ppi = vfi.attribs.ppi;
            if( visited_pages.find(ppi) == visited_pages.end() ){
                visited_pages.insert(ppi);
                cb(vfi.name, vfi);
            }
        }
    }
}

/**
 * @brief Iterates over all raw metadata pages calling a callback for each.
 *
 * Provides low-level access to all metadata pages in all banks. The callback
 * receives the physical page ID and raw page data for each page. Useful for
 * debugging and advanced metadata analysis.
 *
 * @param cb Callback function invoked with (PhysPageId, page_data) for each page.
 */
void CMeta::for_each_page(page_cb_t cb){
    const buf_t empty_page(PAGE_SIZE);
    for( uint32_t bank_id = 0; bank_id < m_banks.size(); bank_id++ ){
        for( uint32_t page_id = 0; page_id < m_banks[bank_id].size()/PAGE_SIZE-1; page_id++ ){
            const PhysPageId ppi( bank_id, page_id );
            const uint8_t* pdata = m_banks[bank_id].data() + (page_id+1)*PAGE_SIZE;
            if( pdata >= m_banks[bank_id].data() + m_banks[bank_id].size() ){
                break;
            }
            if( memcmp(pdata, empty_page.data(), PAGE_SIZE) == 0 ){
                continue;
            }

            cb(ppi, pdata);
        }
    }
}

BlockDescriptors CMeta::read_datastore(PhysPageId ppi0){
    BlockDescriptors bds;
    const auto page_stack = get_page_stack(ppi0);
    if( !page_stack ){
        logger->warn("read_datastore({}): empty PageStack", ppi0);
    }
    bds.reserve(page_stack.size() * (PAGE_SIZE / sizeof(BlockDescriptor)));
    buf_t page_buf;
    int page_idx = -1;
    for( const auto ppi : page_stack ){
        page_idx++;
        if( get_page(ppi, page_buf) ){
            const BlockDescriptor* bd = (const BlockDescriptor*)page_buf.data();
            for(size_t i=0; i<PAGE_SIZE/sizeof(BlockDescriptor); i++, bd++){
                if( bd->empty() ){
//                    if( page_idx != page_stack.size()-1 ){
//                        log_or_die(m_ignore_errors, "read_datastore({}): got empty BD on non-last page ({}/{}) {}", ppi0, page_idx, page_stack.size(), ppi);
//                    }
                } else if( bd->valid() ){
                    if( bd->digest ){ // don't insert zero-digest blocks in HT
                        const auto it = bds.find(bd->digest);
                        if( it != bds.end() && it->second != *bd ){
                            logger->warn("read_datastore({}): duplicate BD: old: {}", ppi0, it->second.to_string());
                            logger->warn("read_datastore({}): duplicate BD: new: {}", ppi0, bd->to_string());
                        }
                        bds[bd->digest] = *bd;
                    }
                } else {
                    log_or_die(m_ignore_errors, "read_datastore({}): invalid BD: {}", ppi0, bd->to_string());
                    if( bd->digest && bds.find(bd->digest) == bds.end() ){
                        // try to use it anyway, but not overwrite existing entry, if any
                        bds[bd->digest] = *bd;
                    }
                }
            }
        } else {
            log_or_die(m_ignore_errors, "read_datastore({}): failed to get page {}", ppi0, ppi);
        }
    }
    return bds;
}

// legacy, don't use
VAllBlocks CMeta::vGetAllBlocks(const buf_t& buf, const CMeta::VFile& vfi){
    const char zeroBuf[sizeof(VBlockDesc)] = {0};

    std::array<uint8_t, sizeof(VBlockDesc)> ffBuf;
    ffBuf.fill(0xff);

    uint32_t indx = 8;
    uint32_t x = 0;
    int64_t blocksTodo = vfi.attribs.nBlocks;
    VAllBlocks vAllB;

    if( buf.size() < indx + sizeof(SMetaTableDescriptor) ){
        logger->error("vGetAllBlocks: buf too small: {:x} < {:x}", buf.size(), indx + sizeof(SMetaTableDescriptor));
        return vAllB;
    }

    SMetaTableDescriptor* pMetaDesc = (SMetaTableDescriptor*) &buf[indx];
    while( blocksTodo > 0 ){
        x++;
        if( vfi.type == FT_INCREMENT ){
            if( (indx & 0xfff) > 0xfc4 ){
                indx = (indx & 0xfffff000) + 0x1008;
            }

            if( memcmp(&buf[indx], zeroBuf, sizeof(VBlockDesc)) == 0 || memcmp(&buf[indx], ffBuf.data(), sizeof(VBlockDesc)) == 0 ){
                // do nothing - dummy block
            } else {
                vAllB.push_back(*(VBlockDesc*) &buf[indx]);
            }

            indx += sizeof(VBlockDesc) + 7;
            if( (indx & 0xfff) > 0xfc4 ){
                indx = (indx & 0xfffff000) + 0x1008;
            }

            blocksTodo--;
        } else {
            if( pMetaDesc->ppi.valid() ){
                buf_t outBuf;
                if( !vFetchMDPage(pMetaDesc->ppi, outBuf) ){
                    log_or_die(m_ignore_errors, "vGetAllBlocks: failed to get {}", pMetaDesc->ppi);
                }

                if( is_new_version() ){
                    buf_t mBuf, nBuf;
                    if( outBuf.size() >= 0x10 + sizeof(PhysPageId) ){
                        for(PhysPageId *ppi = (PhysPageId*) &outBuf[0x10]; ppi->valid(); ppi++ ){
                            vFetchMDPage(ppi, nBuf);
                            mBuf.insert(mBuf.end(), nBuf.begin(), nBuf.end());
                        }
                    }
                    outBuf = std::move(mBuf);
                }

                for( int64_t i=0; i<pMetaDesc->nBlocks; i++ ){
                    size_t off = 8 + i*sizeof(VBlockDesc);
                    off += is_new_version() ? (i/0x59)*2 : (i/0x58)*0x30;
                    if( off >= outBuf.size() ){
                        // should only be here if previous vFetchMDPage() failed and m_ignore_errors is true
                        break;
                    }
                    vAllB.push_back(*(VBlockDesc*) &outBuf[off]);
                }

                blocksTodo -= pMetaDesc->nBlocks;
            } else {
                blocksTodo -= 0x440;
                vAllB.resize(vAllB.size() + 0x440);
            }

            indx += sizeof(SMetaTableDescriptor);
            if( (indx & 0xfff) == 0xff8 ){
                indx += 0x10;
            }
            if( (indx & 0xfff) > 0xff8 ){
                indx = (indx & 0xfffffff0) + 0x18;
            }
            pMetaDesc = (SMetaTableDescriptor*) &buf[indx];
        }
    }

    return vAllB;
}

VAllBlocks CMeta::get_file_blocks(const VFile& vfi){
    VAllBlocks blocks;
    blocks.reserve(vfi.attribs.nBlocks);

    buf_t buf1, buf2;
    auto ps1 = get_page_stack(vfi.attribs.ppi);
    size_t pos = 0;
    int idx = -1;
    for( const auto ppi1 : ps1 ){
        idx++;

        if( !ppi1.valid() ){
            logger->error("get_file_blocks({}): invalid ppi #{}: {}", vfi.attribs.ppi, idx, ppi1);
            continue;
        }
        if( !get_page(ppi1, buf1) ){
            continue;
        }

        if( vfi.type == FT_INCREMENT ){
            buf1.for_each<SPatchBlockDescriptorV7>([&](SPatchBlockDescriptorV7* pBlockDesc){
                logger->trace("get_file_blocks({}): {:12x}: {} total: {:x}", vfi.attribs.ppi, pos, pBlockDesc->to_string(), blocks.size());
                blocks.push_back(*(VBlockDesc*)pBlockDesc); // TODO: replace VBlockDesc with better type
                return (int64_t)blocks.size() < vfi.attribs.nBlocks;
            });
        } else {
            buf1.for_each<SMetaTableDescriptor>([&](SMetaTableDescriptor* pMetaDesc){
                if( !pMetaDesc->valid() ){
                    return false;
                }
                logger->trace("get_file_blocks({}): {:12x}: {} total: {:x}", vfi.attribs.ppi, pos, pMetaDesc->to_string(), blocks.size());
                pos += SMetaTableDescriptor::CAPACITY;

                if( pMetaDesc->is_sparse() ){
                    blocks.resize(blocks.size() + SMetaTableDescriptor::MAX_BLOCKS);
                } else {
                    int64_t nBlocks2 = 0;
                    for( auto ppi2 : get_page_stack(pMetaDesc->ppi)){
                        if( !get_page(ppi2, buf2) ){
                            continue;
                        }
                        buf2.for_each<SFibBlockDescriptorV7>([&](SFibBlockDescriptorV7* pBlockDesc){
                            logger->trace("get_file_blocks({}): {:12x}: {} total: {:x}", vfi.attribs.ppi, pos, pBlockDesc->to_string(), blocks.size());
                            blocks.push_back(*(VBlockDesc*)pBlockDesc); // TODO: replace VBlockDesc with better type
                            return ++nBlocks2 < pMetaDesc->nBlocks;
                        });
                    }
                }

                return true; // always continue to catch all blocks
            });
        }
    }

    while (blocks.size() > 0 && blocks.size() > vfi.attribs.nBlocks && blocks[blocks.size()-1].is_sparse()) {
        blocks.pop_back(); // remove extra trailing sparse blocks
    }

    return blocks;
}

// go over all PageStacks and try to interpret each as a file
std::vector<CMeta::VFile> CMeta::deep_scan(){
    std::vector<VFile> results;
    ppi_set_t all_visited_pages;
    buf_t buf1, buf2;

    // find FIB blocks first (present both in VBK and VIB files)
    for_each_page([&](const PhysPageId& ppi, const uint8_t*){
        const auto ps1 = get_page_stack(ppi);
        size_t nDescriptors = 0;
        size_t nBlocks = 0;
        size_t fibSizeD = 0;
        size_t fibSizeB = 0;

        std::vector<PhysPageId> visited_pages;
        visited_pages.reserve(ps1.size()+1);
        visited_pages.push_back(ppi);

        for( const auto ppi1 : ps1 ){
            if( !ppi1.valid() ){
                break;
            }
            if( !get_page(ppi1, buf1) ){
                break;
            }
            visited_pages.push_back(ppi1);
            buf1.for_each<SMetaTableDescriptor>([&](SMetaTableDescriptor* pMetaDesc){
                if( !pMetaDesc->valid() ){
                    return false;
                }
                nDescriptors++;
                fibSizeD += pMetaDesc->size();
                if( pMetaDesc->is_sparse() ){
                    nBlocks += SMetaTableDescriptor::MAX_BLOCKS;
                    fibSizeB += pMetaDesc->size();
                } else {
                    for( auto ppi2 : get_page_stack(pMetaDesc->ppi)){
                        if( !get_page(ppi2, buf2) ){
                            break;
                        }
                        visited_pages.push_back(ppi2);
                        buf2.for_each<SFibBlockDescriptorV7>([&](SFibBlockDescriptorV7* pBlockDesc){
                            if( pBlockDesc->valid() ){
                                nBlocks++;
                                fibSizeB += pBlockDesc->size;
                                return true;
                            }
                            return false;
                        });
                    }
                }
                return true;
            });
        }
        if( nBlocks > 0 ){
            // typically fibSizeD = fibSizeD, but for some reason in tests/fixtures/AgentBack2024-09-16T163946.vbk for 1st file fibSizeD < fibSizeB.
            logger->info("deep scan result @ {}: {} IntFib descriptor{} ({}) = {} block{} ({})", ppi,
                nDescriptors, nDescriptors == 1 ? "" : "s", bytes2human(fibSizeD, " bytes"),
                nBlocks, nBlocks == 1 ? "" : "s",           bytes2human(fibSizeB, " bytes")
            );

            VFile vfi;
            vfi.type = FT_INT_FIB;
            vfi.name = fmt::format("{:04x}_{:04x}.bin", ppi.bank_id, ppi.page_id);
            vfi.attribs = {};
            vfi.attribs.ppi = ppi;
            vfi.attribs.nBlocks = nBlocks;
            vfi.attribs.filesize = fibSizeB;
            results.push_back(vfi);

            all_visited_pages.insert(visited_pages.begin(), visited_pages.end());
        }
    });

    // find Patch blocks (only in VIB files)
    for_each_page([&](const PhysPageId& ppi, const uint8_t*){
        if( all_visited_pages.find(ppi) != all_visited_pages.end() ){
            return; // go next page in for_each_page() iteration
        }

        size_t nDescriptors = 0;
        off_t maxPatchOffset = 0;

        const auto ps1 = get_page_stack(ppi);
        for( const auto ppi1 : ps1 ) {
            if( !ppi1.valid() ){
                break;
            }
            if( !get_page(ppi1, buf1) ){
                break;
            }
            buf1.for_each<SPatchBlockDescriptorV7>([&](SPatchBlockDescriptorV7* pBlockDesc){
                if( pBlockDesc->valid() ){
                    nDescriptors++;
                    if( pBlockDesc->fib_offset() > maxPatchOffset ){
                        maxPatchOffset = pBlockDesc->fib_offset();
                    }
                    return true;
                } else {
                    return false;
                }
            });
        }

        if( nDescriptors > 0 ){
            std::string fname = fmt::format("{:04x}_{:04x}.bin", ppi.bank_id, ppi.page_id);
            logger->warn_once("deep scan cannot get VIB's original size - using max patch block offset ({:#x}) instead", maxPatchOffset);
            logger->warn_once("deep scan cannot get VIB's original filename - using \"{}\" instead", fname);

            logger->info("deep scan result @ {}: {} Increment descriptor{}", ppi,
                nDescriptors, nDescriptors == 1 ? "" : "s"
            );

            VFile vfi;
            vfi.type = FT_INCREMENT;
            vfi.name = fname;
            vfi.attribs = {};
            vfi.attribs.ppi = ppi;
            vfi.attribs.nBlocks = nDescriptors;
            vfi.attribs.filesize = maxPatchOffset + BLOCK_SIZE;
            results.push_back(vfi);
        }
    });

    std::sort(results.begin(), results.end(), [](const VFile& a, const VFile& b){
        return a.attribs.ppi < b.attribs.ppi;
    });

    return results;
}
