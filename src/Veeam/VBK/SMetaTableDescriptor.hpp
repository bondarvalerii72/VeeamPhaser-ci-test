namespace Veeam::VBK {

// verbatim name
struct __attribute__((packed)) SMetaTableDescriptor {
    PhysPageId ppi;
    int64_t block_size;
    int64_t nBlocks;

    static const int MAX_BLOCKS = 0x440;
    static const size_t CAPACITY = MAX_BLOCKS * BLOCK_SIZE;

    // <SMetaTableDescriptor ppi=-1:-1, block_size=1048576, nBlocks=0>        - skip / sparse
    // <SMetaTableDescriptor ppi=0002:0006, block_size=1048576, nBlocks=781>  - regular blocks
    // <SMetaTableDescriptor ppi=0000:0009, block_size=19955, nBlocks=1>      - last block
    // <SMetaTableDescriptor ppi=0000:0000, block_size=0, nBlocks=0>          - invalid / empty
    // XXX validation tuned up for default block size of 1mb
    bool valid() const {
        switch( nBlocks ){
            case 0:  // skip / sparse
                return is_sparse();
            case 1:  // last block
                return ppi.valid() && !ppi.zero() && block_size > 0 && block_size < BLOCK_SIZE;
            default: // regular blocks
                return ppi.valid() && !ppi.zero() && block_size == BLOCK_SIZE && nBlocks > 0 && nBlocks <= MAX_BLOCKS;
        }
    }

    size_t size() const {
        return is_sparse() ? CAPACITY : (size_t)(block_size*nBlocks);
    }

    bool empty() const {
        return ppi.zero() && block_size == 0 && nBlocks == 0;
    }

    bool is_sparse() const {
        return nBlocks == 0 && ppi.empty() && block_size == BLOCK_SIZE;
    }

    std::string to_string() const {
        return fmt::format("<SMetaTableDescriptor ppi={}, block_size={:x}, nBlocks={:x}>",
            ppi.to_string(), block_size, nBlocks);
    }
};

} // namespace Veeam::VBK
