namespace Veeam::VBK {

// verbatim struct name
struct __attribute__((packed)) SPatchBlockDescriptorV7 {
    uint32_t size;
    uint8_t type;
    digest_t digest;
    int64_t id;
    int64_t block_idx; // multiply by 0x100000 to get real offset
    digest_t digest2; // zero in regular files, maybe related to encryption or versioning

    off_t fib_offset() const {
        return block_idx * BLOCK_SIZE;
    }

    bool valid() const {
        return size == BLOCK_SIZE &&
            type == 0 && // not sure
            digest &&
            id >= 0 &&
            block_idx >= 0 &&
            !digest2;
    }

    std::string to_string() const {
        std::string result = fmt::format(
            "<SPatchBlockDescriptorV7 size={:x}, type={:x}, digest={}, id={:x}, block_idx={:x}",
            size, type, digest, id, block_idx
        );
        if( digest2 ){
            result += fmt::format(", digest2={}", digest2);
        }
        return result + ">";
    }
};

static_assert(sizeof(SPatchBlockDescriptorV7) == 0x35, "SPatchBlockDescriptorV7 size mismatch");

} // namespace Veeam::VBK
