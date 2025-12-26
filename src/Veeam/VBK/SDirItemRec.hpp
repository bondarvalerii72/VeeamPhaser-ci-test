#include <algorithm>
#include <cstdint>

namespace Veeam::VBK {
    // stglib::tstg::SDirItemRec
    struct __attribute__((packed, aligned(4))) SDirItemRec {
        EFileType type;
        uint32_t name_len;
        char name[0x80];
        PhysPageId props_loc;
        int f90;

        struct __attribute__((packed)) base_t {
            uint16_t update_in_progress;
            uint8_t f96;
            uint8_t flags;
            PhysPageId blocks_loc;
            uint64_t nBlocks;
            uint64_t fib_size;

            bool valid(const int32_t max_banks = 0) const {
                bool r = nBlocks > 0 && nBlocks <= fib_size && fib_size > 0 && blocks_loc.valid();
                if( max_banks ){
                    r = r && (blocks_loc.bank_id < max_banks);
                }
                return r;
            }

            std::string to_string() const {
                std::string result;
                if( update_in_progress ){
                    result += fmt::format("update_in_progress={:x} ", update_in_progress);
                }
                if( f96 ){
                    result += fmt::format("f96={:x} ", f96);
                }
                if( flags ){
                    result += fmt::format("flags={:x} ", flags);
                }
                result += fmt::format( "blocks_loc={} nBlocks={:x} fib_size={:x}", blocks_loc, nBlocks, fib_size);
                return result;
            }
        };

        struct __attribute__((packed)) fib_t : base_t {
            PhysPageId undir_loc;
            int64_t fb8;

            std::string to_string() const {
                auto result = base_t::to_string();
                if( !undir_loc.empty() ){
                    result += fmt::format(" undir_loc={}", undir_loc);
                }
                result += fmt::format(" fb8={:x}", fb8);
                return result;
            }
        };

        struct __attribute__((packed)) ext_fib_t : base_t {
            PhysPageId undir_loc; // not sure
            int state;
            int fbc;
        };

        struct __attribute__((packed)) inc_t : base_t {
            uint64_t inc_size;
            PhysPageId versions_loc;

            std::string to_string() const {
                auto result = base_t::to_string() + fmt::format(" inc_size={:x}", inc_size);
                if( !versions_loc.empty() ){
                    result += fmt::format(" versions_loc={}", versions_loc);
                }
                return result;
            }
        };

        struct __attribute__((packed)) dir_t {
            PhysPageId children_loc;
            int64_t children_num;
            uint64_t a, b, c;
            uint32_t d;

            bool valid(const int32_t max_banks = 0) const {
                bool r = children_loc.valid() && children_num > 0;
                if( max_banks ){
                    r = r && (children_loc.bank_id < max_banks);
                }
                return r;
            }

            std::string to_string() const {
                return fmt::format("children_loc={} children_num={:x} a={:x} b={:x} c={:x} d={:x}", children_loc, children_num, a, b, c, d);
            }
        };

        union __attribute__((packed)) {
            fib_t fib;
            inc_t inc;
            dir_t dir;
        } u;

        bool is_dir()  const { return type == FT_SUBFOLDER; }
        bool is_diff() const { return type == FT_INCREMENT || type == FT_PATCH; }

        std::string get_name() const {
            return std::string(name, std::min((size_t)name_len, sizeof(name)));
        }

        bool valid_name() const {
            if( name_len == 0 || name_len > sizeof(name) ){
                return false;
            }
            return std::all_of(name, name + name_len, [](char c){
                return c >= 0x20 && c < 0x7f; // XXX assume no unicode
            });
        }

        bool valid(const int32_t max_banks = 0) const {
            bool r = type >= FT_SUBFOLDER && type <= FT_INCREMENT && valid_name() && props_loc.valid_or_empty();
            if(!r) return r;

            if( max_banks ){
                r = (props_loc.empty() || props_loc.bank_id < max_banks);
                if(!r) return r;
            }

            // TODO: more precise validation
            switch( type ){
                case FT_SUBFOLDER:
                    r = u.dir.valid(max_banks);
                    break;
                case FT_EXT_FIB:
                    break;
                case FT_INT_FIB:
                    r = u.fib.valid(max_banks);
                    break;
                case FT_PATCH:
                case FT_INCREMENT:
                    break;
                default:
                    r = false;
                    break;
            }

            return r;
        }

        std::string to_string() const {
            std::string result = fmt::format("<SDirItemRec type={} name=\"{}\" ", type, get_name());
            if( !props_loc.empty() ){
                result += fmt::format("props={} ", props_loc);
            }
            if( f90 ){
                result += fmt::format("f90={:x} ", f90);
            }
            switch( type ){
                case FT_SUBFOLDER:
                    result += u.dir.to_string();
                    break;
                case FT_INCREMENT:
                    result += u.inc.to_string();
                    break;
                default:
                    result += u.fib.to_string();
                    break;
            }
            return result + ">";
        }
    };

    static_assert(sizeof(SDirItemRec) == 0xc0);
}
