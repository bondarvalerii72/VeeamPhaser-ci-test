#include <cstdint>
#include "CSlot.hpp"

namespace Veeam::VBK {
    const size_t MAX_DIGEST_TYPE_LEN = 250; // hardcoded
    const uint32_t MAX_SLOT_FMT = 9;        // as of Veeam v12.2.0.334
    const size_t MAX_SLOTS = 2;             // hardcoded

    struct __attribute__((packed)) FileHeader {
        uint32_t version;
        uint32_t inited;
        uint32_t digest_type_len;
        char digest_type[MAX_DIGEST_TYPE_LEN+1];
        uint32_t slot_fmt;
        uint32_t std_block_size;
        uint32_t cluster_align; // maybe 'char'

        // XXX also may have external_storage_id (UUID) @ 0x120

        size_t max_banks() const {
            switch(slot_fmt) {
                case 0: return 0xf8;
                case 5:
                case 9: return 0x7f00;
                default:
                        //logger->error("[?] Unknown slot_fmt: {:02x}", slot_fmt);
                        return 0;
            }
        }

        size_t slot_size() const {
            // expression literally copied from reversed code
            return (((max_banks() * sizeof(CSlot::BankInfo) & 0xFFFFFFF0) + 120) & 0xFFFFF000) + PAGE_SIZE;
        }

        std::string to_string() const {
            return fmt::format("<FileHeader version: {:x}, inited: {:x}, digest_type_len: {:x}, digest_type: \"{}\", slot_fmt: {:x}, std_block_size: {:x}, cluster_align: {:x}>",
                version, inited, digest_type_len, digest_type, slot_fmt, std_block_size, cluster_align);
        }

        bool valid() const {
            return inited <= 1 && version != 0 &&
                digest_type_len == 3 && memcmp(digest_type, "md5", 3) == 0 && // XXX simplified for now to handle "md5" only
                std_block_size != 0 && std_block_size % 512 == 0 &&
                cluster_align != 0 &&
                slot_fmt <= MAX_SLOT_FMT;
        }
    };
}
