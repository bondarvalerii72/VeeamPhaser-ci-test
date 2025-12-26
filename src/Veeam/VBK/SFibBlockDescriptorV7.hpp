namespace Veeam::VBK {

// verbatim struct name
struct __attribute__((packed)) SFibBlockDescriptorV7 {
    uint32_t size;
    uint8_t type;
    digest_t digest;
    uint64_t id;
    uint8_t flags;
    digest_t keyset_id;

    bool valid() const {
        return size > 0 && size <= BLOCK_SIZE &&
            (type == 0 || type == 1) &&
            digest;
    }

    // for deep search to make less false positives
    bool valid_not_encrypted() const {
        return valid() && !keyset_id;
    }

    std::string to_string() const {
        std::string result = fmt::format("<SFibBlockDescriptorV7 size={:x}, type={:x}, digest={}", size, type, digest);
        if( id ){
            result += fmt::format(", id={:x}", id);
        }
        if( flags ){
            result += fmt::format(", flags={:02x}", flags);
        }
        if( keyset_id ){
            result += fmt::format(", keyset_id={}", keyset_id);
        }
        return result + ">";
    }
};

static_assert(sizeof(SFibBlockDescriptorV7) == 0x2e, "SFibBlockDescriptorV7 size mismatch");

} // namespace Veeam::VBK
