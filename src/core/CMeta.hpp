#pragma once
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "structs.hpp"
#include "utils/crypto.hpp"
#include "Veeam/VBK.hpp"
#include "io/Reader.hpp"

class CMeta {
    public:

    enum EMetaSource {
        MS_AUTO,
        MS_TOC,
        MS_BRUTEFORCE,
        MS_SLOT,
        MS_BANK,
    };

    struct __attribute__((packed)) VFileAttribs { // size = 0x34
        int dw0;
        int dw1;
        int dw2;
        PhysPageId ppi;
        int64_t nBlocks;
        int64_t filesize;
        int64_t vib_updsize;
        int64_t qw0;
    };

    struct VFile {
        Veeam::VBK::EFileType type;
        std::string name;
        VFileAttribs attribs;

        bool is_dir()  const { return type == Veeam::VBK::FT_SUBFOLDER; }
        bool is_diff() const { return type == Veeam::VBK::FT_INCREMENT || type == Veeam::VBK::FT_PATCH; }

        const char* type_str() const {
            switch(type){
                case Veeam::VBK::FT_SUBFOLDER:
                    return "Dir";
                case Veeam::VBK::FT_EXT_FIB:
                    return "ExtFib";
                case Veeam::VBK::FT_INT_FIB:
                    return "IntFib";
                case Veeam::VBK::FT_PATCH:
                    return "Patch";
                case Veeam::VBK::FT_INCREMENT:
                    return "Inc";
                default:
                    return "???";
            }
        }
    };

    struct __attribute__((packed)) RootPage {
        PhysPageId next_page_id;
        PhysPageId page_id; // this page id
        uint8_t data[PAGE_SIZE - sizeof(PhysPageId) * 2];

        size_t data_size() const {
            return sizeof(data);
        }

        std::string to_string() const {
            return fmt::format("RootPage(next_page_id={}, page_id={}, data_size={})", next_page_id, page_id, data_size());
        }
    };

    struct __attribute__((packed)) BranchPage {
        PhysPageId next_page_id;
        uint8_t data[PAGE_SIZE - sizeof(PhysPageId)];

        size_t data_size() const {
            return sizeof(data);
        }

        std::string to_string() const {
            return fmt::format("BranchPage(next_page_id={}, data_size={})", next_page_id, data_size());
        }
    };

    struct __attribute__((packed)) LeafPage {
        uint8_t data[PAGE_SIZE];

        size_t data_size() const {
            return sizeof(data);
        }

        std::string to_string() const {
            return fmt::format("LeafPage(data_size={})", data_size());
        }
    };

    CMeta(const std::filesystem::path& fname, bool ignore_errors=false, off_t offset=0, EMetaSource meta_src=MS_AUTO, const std::string& password="", bool dump_keysets=false, std::optional<std::filesystem::path> keysets_same_file = std::nullopt, bool dump_session_only=false);

    // if ignore_errors is true  => just log error message
    // if ignore_errors is false => throw std::runtime_error
    template <typename... Args>
    void log_or_die(const bool ignore_errors, fmt::format_string<Args...> format, Args... args) {
        const std::string msg = fmt::format(format, std::forward<Args>(args)...);
        logger->error("{}", msg);
        if( !ignore_errors ){
            throw std::runtime_error(msg);
        }
    }

    bool get_page(const PhysPageId ppi, buf_t& dst);
    CPageStack get_page_stack(PhysPageId ppi);
    BlockDescriptors read_datastore(PhysPageId ppi);

    bool vFetchMDPage(PhysPageId ppi, buf_t& Buf, bool ignore_errors=false);        // legacy, don't use
    bool vFetchMDPage(const PhysPageId* ppi, buf_t& Buf, bool ignore_errors=false); // legacy, don't use

    bool vLoadFile(SDirItemRec* dir_item, VFile& vFileDesc);
    VAllBlocks vGetAllBlocks(const buf_t& buf, const VFile& vfi); // legacy, don't use
    VAllBlocks get_file_blocks(const CMeta::VFile& vfi);

    void detect_version();

    typedef std::unordered_set<PhysPageId> ppi_set_t;

    typedef std::function<void(const std::string& fname, const VFile&)> file_cb_t;
    void for_each_file(file_cb_t cb);
    void read_dir(PhysPageId dir_ppi, file_cb_t cb, ppi_set_t* visited = nullptr);

    typedef std::function<void(const PhysPageId&, const uint8_t*)> page_cb_t;
    void for_each_page(page_cb_t cb);

    bool is_new_version();
    void set_version(int version){ m_new_version = version; }

    std::vector<VFile> deep_scan();
    void set_deep_scan(bool deep_scan) {
        m_deep_scan = deep_scan;
    }

    void dump_loaded_keysets() const;

    std::optional<crypto::aes_key> get_aes_key(const Veeam::VBK::digest_t& id) const;
    const crypto::AES256* get_aes_cipher(const Veeam::VBK::digest_t& id) const;

    private:
    void import_slot(Reader&, const off_t);
    void import_bank(Reader&, const off_t);
    void import_legacy(Reader&, const off_t);
    bool load_keysets_from_bank(const Veeam::VBK::CBank& bank, const std::string& password, bool is_bank_source);
    void process_dir_page(const void* pagedata, file_cb_t cb, ppi_set_t* visited = nullptr);

    int m_new_version = -1;
    bool m_ignore_errors = false;
    bool m_deep_scan = false;
    bool m_dump_keysets = false;
    std::filesystem::path m_source_path;
    std::optional<std::filesystem::path> m_keysets_same_file;
    EMetaSource m_meta_source = MS_AUTO;
    std::string m_password;
    std::map<Veeam::VBK::digest_t, crypto::aes_key> m_aes_keys;
    std::map<Veeam::VBK::digest_t, std::unique_ptr<crypto::AES256>> m_aes_ciphers;
    std::optional<Veeam::VBK::digest_t> m_session_key;
    bool m_dump_session_only = false;
    VMeta m_banks;
};
