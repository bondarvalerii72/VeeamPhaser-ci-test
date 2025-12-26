#include <cstdint>

namespace Veeam::VBK {

enum EEncryptionMode : uint8_t {
    EM_NONE = 0,
    EM_DATA = 1,
    EM_FULL = 2,
};

// not an actual struct name
struct __attribute__((packed)) SDedupRec {
    PhysPageId ppi;
    digest_t hash;
    int64_t refCnt;
};

static_assert(sizeof(SDedupRec) == 0x20);

// stglib::tstg::CBank
struct __attribute__((packed, aligned(8))) CBank {
    static const size_t V13_MIN_PAGES = 0x20;
    static const size_t V13_MAX_PAGES = 0x400;

    struct __attribute__((packed, aligned(8))) DataPage {
        char data[PAGE_SIZE];

        bool is_metavec2_start(int page_id) const {
            
            int* p = (int*)data;
            return p[0] == -1 && p[1] == -1 && p[2] == page_id;
        }

        bool is_dedup_idx() const {
            uint32_t nRecords = *(uint32_t*)data;
            if( nRecords < 10 || nRecords > (PAGE_SIZE-4)/sizeof(SDedupRec) ){
                return false;
            }
            SDedupRec* pRecs = (SDedupRec*)(data + 4);

            for(uint32_t i=0; i<nRecords-1; i++){
                const auto* rec = &pRecs[i];
                const auto* next = &pRecs[i+1];
                if( !rec->ppi.empty() || memcmp(&rec->hash, &next->hash, sizeof(rec->hash)) >= 0 ){
                    return false;
                }
            }

            return true;
        }

        template<class T>
            bool has_valid() const {
                const T* p = (T*)data;
                return p->valid();
            }
    };

    struct __attribute__((packed, aligned(8))) HeaderPage {
        uint16_t nPages;
        EEncryptionMode encr_mode;
        uint8_t f3;                        // unused?
        uint8_t free_pages[V13_MAX_PAGES]; // 0 = used, 1 = free
        uint8_t zeroes[V13_MAX_PAGES*2];   // 2048 bytes
                                           
        digest_t keyset_id;
        uint32_t encr_size;
        uint32_t fc18[8];                  // zeroed in .text:00F27C50 (VeeamAgent.exe v12.2.0.334), but not used anywhere
        char unused[0x3c8];

        bool valid() const {
            return nPages >= V13_MIN_PAGES && nPages <= V13_MAX_PAGES
                && free_pages_valid()
                && zeroes_are_zero()
                && valid_encr_config();
        }

        bool valid_encr_config() const {
            return (!keyset_id && encr_size == 0) ||
                (keyset_id && encr_size > 0 && encr_size <= (bank_size() - PAGE_SIZE));
        }

        bool is_encrypted() const {
            return valid_encr_config() && encr_size > 0;
        }

        bool zeroes_are_zero() const {
            uint64_t* p = (uint64_t*)zeroes;
            for(size_t i=0; i<V13_MAX_PAGES*2/sizeof(uint64_t); i++, p++){
                if(*p != 0){
                    return false;
                }
            }
            return true;
        }

        bool free_pages_valid() const {
            uint64_t* p = (uint64_t*)free_pages;
            bool was_occupied = false; // 00 marks occupied pages, so we need to check if there is at least one
            for(size_t i=0; i<V13_MAX_PAGES/sizeof(uint32_t); i++, p++){
                if((*p & 0xfefefefe) != 0){
                    return false;
                }
                if( *p != 0x01010101 ){
                    was_occupied = true;
                }
            }
            return was_occupied;
        }

        uint32_t bank_size() const {
            return (nPages+2) * PAGE_SIZE;
        }
    };

    HeaderPage header_page;
    DataPage data_pages[V13_MAX_PAGES];

    static_assert(sizeof(HeaderPage) == PAGE_SIZE);

    bool valid_fast() const {
        return header_page.valid();
    }

    // requires all data pages to be loaded
    bool valid_slow(const size_t data_size) const {
        if( is_encrypted() ){
            return true; // can't check anything without the key?
        }

        int nOK = 0;
        for( size_t page_id=0; page_id<header_page.nPages; page_id++ ){
            if( (page_id+1) * PAGE_SIZE >= data_size ){ // +1 for header page
                return false;
            }
            if( header_page.free_pages[page_id] ){
                continue;
            }
            const auto& page = data_pages[page_id];
            if( page_id == 0 && page.is_dedup_idx() ){
                return true;
            }
            if( page.is_metavec2_start(page_id) || page.has_valid<SKeySetRec>() ){
                nOK++;
                if( nOK >= 2 ){
                    return true;
                }
            }
        }

        return false;
    }

    uint32_t size() const {
        return header_page.bank_size();
    }

    bool is_encrypted() const {
        return header_page.is_encrypted();
    }

    size_t encr_size() const {
        return header_page.encr_size;
    }

    // XXX may segfault if size() is invalid
    uint32_t calc_crc() const {
        return vcrc32(0, this, size());
    }

    std::string to_string() const {
        return fmt::format("<CBank size={:6x} encrypted={} encr_size={:x}>", size(), (int)is_encrypted(), encr_size());
    }
};

}
