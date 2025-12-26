#include "DblBufScanner.hpp"
#include "Veeam/VBK.hpp"
#include "processing/MD5.hpp"
#include "data/BitFileMappedArray.hpp"
#include "utils/crypto.hpp"

#include <map>
#include <memory>

class ScannerV2 : public DblBufScanner {
    using BankInfo = Veeam::VBK::CSlot::BankInfo;
    using CBank = Veeam::VBK::CBank;

    public:
    ScannerV2(const std::string& fname, off_t start, bool find_data_blocks, bool carve_mode = false, const std::string& keysets_dump = {})
        : DblBufScanner(fname, start), m_find_blocks(find_data_blocks), m_carve_mode(carve_mode), m_keysets_dump(keysets_dump) {}
    uint32_t calc_bank_crc(const buf_t& buf, off_t file_offset, size_t buf_pos);

    void process_buf(const buf_t& buf, off_t file_offset) override;
    void check_bank(const buf_t& buf, off_t file_offset, size_t pos);
    void check_slot(const buf_t& buf, off_t file_offset, size_t pos);
    bool check_data(const buf_t& buf, off_t file_offset, size_t pos);
    bool check_data_lz4(const buf_t& buf, off_t file_offset, size_t pos, crypto::AES256 const* cipher = nullptr, const Veeam::VBK::digest_t* keyset_id = nullptr);
    bool check_data_zlib(const buf_t& buf, off_t file_offset, size_t pos);
    bool check_data_xml(const buf_t& buf, off_t file_offset, size_t pos, crypto::AES256 const* cipher = nullptr, const Veeam::VBK::digest_t* keyset_id = nullptr);
    bool check_encrypted_headers(const uint8_t* dec_head, size_t dec_size);

    private:
    bool load_keysets_dump(const std::filesystem::path& path);
    const crypto::AES256* get_aes_cipher(const digest_t& id) const;
    void add_good_block(off_t offset, int comp_size, int raw_size, digest_t digest, uint32_t crc, const std::string& comp_type = "", const Veeam::VBK::digest_t* keyset_id = nullptr);
    void set_bitmap(off_t offset, size_t size);
    std::string process_bank(const CBank*, uint32_t bank_crc, off_t bank_offset);
    void save_bank(const BankInfo& bi);
    void increment_bank_usagecnt(const BankInfo& bi);
    void start() override;
    void finish() override;
    uint32_t guess_bank_id(const CBank* bank, uint32_t bank_crc);

    struct SlotBankInfo {
        uint32_t idx;
        BankInfo info;
        bool found = false;
    };

    struct SlotInfo {
        std::unordered_map<uint32_t, size_t> crc_map;
        std::unordered_map<off_t, size_t> offset_map;
    };

    std::unordered_map<uint64_t /*slot_offset*/, SlotInfo> m_slots_map;
    std::vector<SlotBankInfo> m_sbis;
    std::unordered_set<uint64_t> m_checked_offsets;
    std::unordered_map<uint64_t, int> m_bank_usagecnt;
    std::unordered_set<uint64_t> m_seen_bank_ids;
    std::unordered_map<uint64_t, uint64_t> m_seen_slot_fingerprints;


    // blocks
    bool m_find_blocks = false;
    bool m_carve_mode = false;
    bool m_failed_guess = false;
    bool m_is_encrypted = false;
    uint32_t m_current_bank_id = 0;
    std::unordered_set<uint32_t> m_seen_bank_crcs;
    std::map<uint32_t, BankInfo> m_bank_id_to_bank;  // lightweight BankInfo instead of 4MB CBank
    std::unordered_map<uint32_t, uint32_t> m_bank_crc_to_bank_id;
    buf_t m_decomp_buf;
    MD5 m_md5;
    std::ofstream m_good_blocks_csv, m_bad_blocks_csv;

    std::string m_keysets_dump;
    std::map<Veeam::VBK::digest_t, crypto::aes_key> m_aes_keys;
    std::map<Veeam::VBK::digest_t, std::unique_ptr<crypto::AES256>> m_aes_ciphers;

    // bitmap
    std::unique_ptr<BitFileMappedArray> m_bitmap;
};
