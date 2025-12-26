#pragma once
#include <cstdint>
#include <spdlog/fmt/bundled/core.h>
namespace Veeam::VBK {

constexpr size_t PAGE_SIZE = 0x1000;
constexpr int64_t MAX_BANK_SIZE = (0x400 + 2) * PAGE_SIZE; // V13_MAX_PAGES + 2; defining it as signed to make less casts/warnings
constexpr size_t MAX_BANKS = 0xffa0; // hardcoded in VeeamAgent

constexpr int BLOCK_SIZE = 0x100000; // 1MB

// didnt found the real name of this struct
struct __attribute__((packed)) PhysPageId {
    int32_t page_id = -1;
    int32_t bank_id = -1;

    PhysPageId() = default;
    PhysPageId(int32_t bank_id, int32_t page_id)   : page_id(page_id), bank_id(bank_id) {}
    PhysPageId(uint32_t bank_id, uint32_t page_id) : page_id(page_id), bank_id(bank_id) {}
    PhysPageId(const std::string& str) {
        uint32_t bid = 0, pid = 0;
        sscanf(str.c_str(), "%x:%x", &bid, &pid); // scan to temp vars to prevent "taking address of packed member" warning

        page_id = (int32_t)pid;
        bank_id = (int32_t)bid;
    }

    // allow calls like if( ppi ){ ... }
    explicit operator bool() const {
        return valid();
    }

    bool empty() const {
        return bank_id == -1 && page_id == -1;
    }

    bool zero() const {
        return bank_id == 0 && page_id == 0;
    }

    bool valid() const {
        return bank_id > -1 && bank_id <= (int32_t)MAX_BANKS &&
            page_id > -1 && page_id <= 0x400; // V13_MAX_PAGES
    }

    bool valid_or_empty() const {
        return valid() || empty();
    }

    std::string to_string() const {
        return empty() ? "-1:-1" : fmt::format("{:04x}:{:04x}", bank_id, page_id);
    }

    bool operator<(const PhysPageId& other) const {
        if (bank_id != other.bank_id) {
            return bank_id < other.bank_id;
        }
        return page_id < other.page_id;
    }

    bool operator==(const PhysPageId& other) const {
        return page_id == other.page_id && bank_id == other.bank_id;
    }

    bool operator!=(const PhysPageId& other) const {
        return !(*this == other);
    }
};

const PhysPageId INVALID_PPI = PhysPageId(-1, -1); // verbatim: InvalidPageId
const PhysPageId DEFAULT_DATASTORE_PPI = PhysPageId(0, 1);

enum EFileType : int { // full verbatim
    FT_SUBFOLDER = 1,
    FT_EXT_FIB   = 2,
    FT_INT_FIB   = 3,
    FT_PATCH     = 4,
    FT_INCREMENT = 5,
};

} // namespace Veeam::VBK

// Specialize std::hash for PhysPageId
namespace std {
    template <>
    struct hash<Veeam::VBK::PhysPageId> {
        std::size_t operator()(const Veeam::VBK::PhysPageId& p) const {
            return std::hash<int64_t>{}(*(int64_t*)&p);
        }
    };
}

// fmt formatter for PhysPageId
template <>
struct fmt::formatter<Veeam::VBK::PhysPageId> : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(const Veeam::VBK::PhysPageId& ppi, FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(ppi.to_string(), ctx);
    }
};

template <>
struct fmt::formatter<Veeam::VBK::EFileType> : fmt::formatter<std::string_view> {
    template <typename FormatContext>
    auto format(const Veeam::VBK::EFileType& type, FormatContext& ctx) const {
        std::string_view name;
        switch (type) {
            case Veeam::VBK::FT_SUBFOLDER: name = "FT_SUBFOLDER"; break;
            case Veeam::VBK::FT_EXT_FIB:   name = "FT_EXT_FIB"; break;
            case Veeam::VBK::FT_INT_FIB:   name = "FT_INT_FIB"; break;
            case Veeam::VBK::FT_PATCH:     name = "FT_PATCH"; break;
            case Veeam::VBK::FT_INCREMENT: name = "FT_INCREMENT"; break;
            default: name = "Unknown EFileType"; break;
        }
        return fmt::formatter<std::string_view>::format(name, ctx);
    }
};

#include "VBK/digest_t.hpp"
#include "VBK/FileHeader.hpp"
#include "VBK/SDirItemRec.hpp"
#include "VBK/SKeySetRec.hpp"
#include "VBK/CSlot.hpp"
#include "VBK/CBank.hpp"
#include "VBK/CPageStack.hpp"
#include "VBK/SMetaTableDescriptor.hpp"
#include "VBK/SRestoreRecBlob.hpp"
#include "VBK/SFibBlockDescriptorV7.hpp"
#include "VBK/SPatchBlockDescriptorV7.hpp"
