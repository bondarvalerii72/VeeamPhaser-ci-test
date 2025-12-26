#pragma once
#include <sstream>
#include <string>
#include <vector>

namespace Veeam::VBK {

// stglib::tstg::CPageStack
class CPageStack {
    public:
    std::vector<PhysPageId> m_pageTables; // verbatim name
    std::vector<PhysPageId> m_pageIds;
    bool m_finalized = false;

    static constexpr size_t PPIS_PER_PAGE = PAGE_SIZE / sizeof(PhysPageId);

    bool valid() const {
        return m_finalized && !m_pageIds.empty();
    }

    // allow calls like if( pageStack ){ ... }
    explicit operator bool() const {
        return valid();
    }

    size_t size() const {
        if (!m_finalized) {
            throw std::runtime_error(fmt::format("{} is not finalized, cannot get size", to_string()));
        }
        return m_pageIds.size();
    }

    // original logic from CPageStack::get_page_ppi, identical for all T classes
    static inline constexpr size_t calc_idx(size_t page_idx) {
        size_t reqTableNum = 1;
        while (page_idx+1 > 510*reqTableNum)
            reqTableNum *= 4;
    
        reqTableNum += page_idx;
        const size_t tableIdx = reqTableNum / 511;
        const size_t tableOfs = reqTableNum % 511;
        const size_t idx = 512*tableIdx + tableOfs + 1;
        return idx;
    }

    CPageStack& finalize() {
        if (m_finalized) {
            throw std::runtime_error(fmt::format("{} is already finalized", to_string()));
        }
        m_pageIds.resize(m_pageTables.size());
        size_t i = 0;
        for(; i<m_pageTables.size(); ++i) {
            size_t idx = calc_idx(i);
            if (idx < m_pageTables.size()) {
                m_pageIds[i] = m_pageTables[idx];
            }
        }
        while (i>0 && !m_pageIds[i-1].valid()) {
            --i;
        }
        m_pageIds.resize(i);
        m_finalized = true;
        return *this;
    }

    // Iterator support for range-based for-loops
    auto begin() const {
        if (!m_finalized) {
            throw std::runtime_error(fmt::format("{} is not finalized, cannot iterate over pages", to_string()));
        }
        return m_pageIds.begin();
    }
    auto end() const { return m_pageIds.end(); }

    void add_page(const std::vector<uint8_t>& page) {
        if (page.size() != PAGE_SIZE) {
            throw std::invalid_argument("invalid page size");
        }
        if (m_finalized) {
            throw std::runtime_error(fmt::format("{} is finalized, cannot add more pages", to_string()));
        }
            
        size_t size0 = m_pageTables.size();
        m_pageTables.resize(size0 + PPIS_PER_PAGE);
        memcpy(m_pageTables.data() + size0, page.data(), PAGE_SIZE);
    }

    static void vec2str(std::ostringstream& oss, const std::vector<PhysPageId>& vec) {
        if( vec.size() < 10 ){
            for (size_t i = 0; i < vec.size(); ++i) {
                oss << vec[i].to_string();  // Assuming PhysPageId has to_string()
                if (i + 1 < vec.size()) {
                    oss << ", ";
                }
            }
        } else {
            oss
                << vec[0].to_string() << ", "
                << vec[1].to_string() << ", "
                << vec[2].to_string() << ", ... , "
                << vec[vec.size()-3].to_string() << ", "
                << vec[vec.size()-2].to_string() << ", "
                << vec[vec.size()-1].to_string();
        }
    }

    std::string to_string() const {
        std::ostringstream oss;

        if (m_finalized) {
            oss << "PageStack[" << size() << "]{";
            vec2str(oss, m_pageIds);
        } else {
            oss << "PageStack[RAW][" << m_pageTables.size() << "]{";
            vec2str(oss, m_pageTables);
        }

        oss << "}";
        return oss.str();
    }
};

}

// fmt formatter for CPageStack
template <>
struct fmt::formatter<Veeam::VBK::CPageStack> : fmt::formatter<std::string> {
    template <typename FormatContext>
    auto format(const Veeam::VBK::CPageStack& ppi, FormatContext& ctx) const {
        return fmt::formatter<std::string>::format(ppi.to_string(), ctx);
    }
};
