#pragma once
#include <cstdint>

extern "C" {
    uint32_t vcrc32(uint32_t crc, const void *buf, unsigned int len);
}

namespace Veeam::VBK {

// stglib::tstg::CSlotHdr
struct __attribute__((packed, aligned(8))) CSlot {
    struct __attribute__((packed)) ObjRefs {
        PhysPageId MetaRootDirPage;
        uint64_t children_num;
        PhysPageId DataStoreRootPage;
        uint64_t BlocksCount;
        PhysPageId free_blocks_root;
        PhysPageId dedup_root;
        PhysPageId f30;
        PhysPageId f38;
        PhysPageId CryptoStoreRootPage;
        PhysPageId ArchiveBlobStorePage;

        std::string to_string() const {
            std::string result = "<ObjRefs ";

            auto append_if_valid = [&](const std::string& name, const PhysPageId& ppi) {
                if (ppi.valid()) {
                    result += fmt::format("{}={}, ", name, ppi);
                }
            };

            append_if_valid("MetaRootDirPage", MetaRootDirPage);
            result += fmt::format("children_num={:x}, ", children_num);
            append_if_valid("DataStoreRootPage", DataStoreRootPage);
            result += fmt::format("BlocksCount={:x}, ", BlocksCount);
            append_if_valid("free_blocks_root", free_blocks_root);
            append_if_valid("dedup_root", dedup_root);
            append_if_valid("f30", f30);
            append_if_valid("f38", f38);
            append_if_valid("CryptoStoreRootPage", CryptoStoreRootPage);
            append_if_valid("ArchiveBlobStorePage", ArchiveBlobStorePage);

            // Remove trailing comma and space if present
            if (result.size() > 9 && result.back() == ' ') {
                result.pop_back();
                result.pop_back();
            }

            result += ">";
            return result;
        }
    };

    struct __attribute__((packed)) SnapshotDescriptor {
        uint64_t version;
        uint64_t storage_eof;
        uint32_t nBanks;
        ObjRefs objRefs;
        uint64_t f64;    // unused?

        uint64_t get_storage_eof() const {
            return storage_eof;
        }

        std::string to_string() const {
            std::string result = fmt::format("<SnapshotDescriptor version={:x}, storage_eof={:x}, nBanks={:x}, objRefs={}", version, storage_eof, nBanks, objRefs.to_string());
            if( f64 ){
                result += fmt::format(", f64={:x}", f64);
            }
            result += ">";
            return result;
        }
    };

    struct __attribute__((packed)) BankInfo {
        uint32_t crc;
        int64_t  offset;
        uint32_t size;

        std::string to_string() const {
            return fmt::format("<BankInfo crc={:08x}, offset={:12x}, size={:7x}>", crc, offset, size);
        }
    };

    uint32_t crc;
    uint32_t has_snapshot;
    SnapshotDescriptor snapshotDescriptor;
    uint32_t max_banks; // <= MAX_BANKS
    uint32_t allocated_banks;

// disable warning: ISO C++ forbids zero-size array 'bank_infos'
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

    BankInfo bankInfos[0]; // max_banks

#pragma GCC diagnostic pop

    std::string to_string() const {
        return fmt::format("<CSlot crc={:08x}, has_snapshot={:x}, max_banks={:x}, allocated_banks={:x} size={:x}>", crc, has_snapshot, max_banks, allocated_banks, size());
    }

    bool valid_fast() const {
        return crc != 0
            && has_snapshot == 1 // XXX maybe other valid values?
            && max_banks > 0 && max_banks <= MAX_BANKS
            && allocated_banks <= max_banks;
    }

    // before calling this be sure that full slot (size()) has been read
    bool valid_crc() const {
        return vcrc32(0, &has_snapshot, size()-8) == crc;
    }

    size_t size() const {
        return sizeof(*this) + max_banks * sizeof(BankInfo);
    }
};

}
