VIB files are used to store incremental backups, while VBK files are used to store full backups.
## File Layout

```
0x0000      FileHeader (4KB)
0x1000      Slot 0
0x81000     Slot 1 (copy of Slot 0)
......      Banks (metadata pages) and data blocks 
```
Veeam uses 2 metadata structures which hold the other metadata.

**Slots:** Two redundant metadata headers (at 0x1000 and 0x81000) containing snapshot info, bank locations, and pointers to metadata roots. Each slot points to different copies of banks which are also redundant.

**Banks:** Variable-sized storage containers for metadata pages (directory root, and block descriptors). Each bank has a header page tracking allocated pages and up to 1024 data pages (4 kb each) (excluding the header, and alignment page). Two copies of each bank are stored in the vbk file, since the slots are mirrored.

The internal metadata pointers in veeam use a special pointer called `PhysPageId`, this specifies the bank id, as well as the page id for the stack of pages. 
```c++
struct PhysPageId(){
    int32_t page_id = -1;
    int32_t bank_id = -1;
}
```

A `PageStack` is a linked list of 4 kb pages. Used for storing the metadata inside the banks. The first page starts with two `PhysPageId` entries: `next` pointing to the next page, and `self` the self-reference pointer. The later pages in the `PageStack` only have the `next` pointer. The last page in the chain has `next = {-1, -1}`. `MetaVec<T>` wraps a `PageStack` and treats it as a contiguous array of `T` structs, and `MetaVec` is used for most of the variable length metadata in veeam.

Veeam uses **CRC32C** (Castagnoli polynomial: 0x1EDC6F41) for checksums, and supports zlib, zstd, rle, and lz4 for compression.

## `FileHeader`

At offset 0, size 0x1000 (4 kb).

```c
struct FileHeader {
    uint32_t version;            // 0x0: usually 0xd
    uint32_t inited;             // 0x4: 1 when initialized
    uint32_t digest_type_len;    // 0x8: length of digest string
    char digest_type[251];       // 0xC: "md5"
    uint32_t slot_fmt;           // 0x107: slot format version (0, 5, or 9)
    uint32_t std_block_size;     // 0x10B: 0x100000 (1MB)
    uint32_t cluster_align;      // 0x10F: cluster alignment
    // rest is padding
};
```

The slot format version determines max banks:
- 0 -> 0xf8 banks
- 5 or 9 -> 0x7f00 banks

Slot size is calculated as `((((max_banks * 0x10) & 0xFFFFFFF0) + 120) & 0xFFFFF000) + 0x1000`.

## `Slot`

Two `Slot`s at 0x1000 and 0x81000 (or wherever `slot_size` puts it). Each `Slot` contains snapshot metadata and bank locations. The second `Slot` is a redundant copy. 

```c
struct Slot {
    uint32_t crc;                         // 0x0: CRC of slot (excluding the first 4 bytes)
    uint32_t has_snapshot;                // 0x4: 1 if valid
    SnapshotDescriptor snapshot_desc;     // 0x8: snapshot descriptor
    uint32_t max_banks;                   // 0x68: max possible banks
    uint32_t allocated_banks;             // 0x6C: banks used
    BankInfo bank_infos[allocated_banks]; // 0x70: array of bank info
};
``` 

### `SnapshotDescriptor`

Describes the current backup state.

```c
struct SnapshotDescriptor {
    uint64_t version;       // 0x0: snapshot version
    uint64_t storage_eof;   // 0x8: file size
    uint32_t nBanks;        // 0x10 : number of banks
    ObjRefs objRefs;        // 0x14: Object References (metadata root pointers)
    uint64_t f64;           // 0x5C: unused
};
```

### `ObjRefs`

Root pointers to all major structures. Everything starts here.

```c
struct ObjRefs {
    PhysPageId MetaRootDirPage;     // 0x0: root file directory (typically 0000:0000)
    uint64_t children_num;          // 0x8 : files in root
    PhysPageId DataStoreRootPage;   // 0x10: block storage index (typically 0000:0001)
    uint64_t BlocksCount;           // 0x18: total blocks
    PhysPageId free_blocks_root;    // 0x20: free space manager (typically 0000:0002)
    PhysPageId dedup_root;          // 0x28: dedup metadata root
    PhysPageId unused;            
    PhysPageId unused2;             
    PhysPageId CryptoStoreRootPage; // 0x40 : encryption keys
    PhysPageId ArchiveBlobStorePage;// 0x48: archive blobs
};
```

**Key pointers:**
- **`MetaRootDirPage`** - Directory tree root. Parse as `MetaVec<SDirItemRec>` to get files/folders.
- **`DataStoreRootPage`** - Block index root. Parse as `MetaVec<BlockDescriptor>`. Look up block IDs in the BlockDescriptor vector to find compressed data locations.

### `BankInfo`

Location of each bank in the file, along with it's size, and crc.

```c
struct BankInfo {
    uint32_t crc;     // 0x0: CRC of bank
    int64_t offset;   // 0x4: file offset
    uint32_t size;    // 0xC: bank size
};
```

Converting a `PhysPageId {bank_id, page_id}` pointer to a physical offset:

```
bank_offset = slot.bank_infos[bank_id].offset
page_offset = bank_offset + 0x1000 (header) + (page_id * 0x1000)
```

## Banks

Banks store metadata pages. Each bank starts with a header page followed by data pages.

```c
struct BankHeaderPage {
    uint16_t nPages;           // 0x000: number of pages (not including header)
    uint8_t encr_mode;         // 0x002: encryption (0=none, 1=data, 2=full)
    uint8_t f3;                // 0x003: reserved
    uint8_t free_pages[0x400]; // 0x004: bitmap (0=used, 1=free)
    uint8_t zeroes[0x800];     // 0x404: must be zero
    digest_t keyset_id;        // 0xC04: encryption key ID
    uint32_t encr_size;        // 0xC14: encrypted size
    uint32_t fc18[8];          // 0xC18: reserved
    uint8_t unused[0x3C8];     // 0xC38: padding
};
```

Total bank size = `(nPages + 2) * 0x1000`. Max 1024 data pages per bank. One page for header, and the second page for alignment padding. 

**free_pages bitmap:** each byte represents one page (0=used, 1=free). Check this before reading a page.

## `PageStack`and `MetaVec`

A `PageStack` is a linked list of 4 kb pages for storing variable length data as mentioned before.

**First page:**
```c
struct PageStackFirstPage {
    PhysPageId next;                    // 0x0: next page (-1:-1 if last)
    PhysPageId self;                    // 0x8: self reference
    uint8_t data[0xFF0];                // 0x10: data (4080 bytes)
};
```

**Subsequent pages:**
```c
struct PageStackPage {
    PhysPageId next;                    // 0x0: next page (-1:-1 if last)
    uint8_t data[0xFF8];                // 0x8: data (4088 bytes)
};
```

`MetaVec<T>` wraps a `PageStack` and treats it as a contiguous array of `T` structs. Used for all variable-length metadata arrays (directory items, block descriptors, etc). It automatically handles page boundaries and reserved slots.

## Directory Structure

File and folder entries in the root dir are stored are at `MetaRootDirPage`. Pointers to subfolders are stored in `children_loc` in `FT_SUBFOLDER` entries.

```c
struct SDirItemRec {                    // 0xC0 bytes
    uint32_t type;                      // 0x0: The type of item (dir, int fib, increment)
    uint32_t name_len;                  // 0x4: filename length
    char name[0x80];                    // 0x8: filename (128 bytes)
    PhysPageId props_loc;               // 0x88: properties blob location
    int32_t f90;                        // 0x90: unused
    
    union {                             // 0x94: depends on file type
        struct {                        // For directories (FT_SUBFOLDER)
            PhysPageId children_loc;    // 0x94: child files location
            int64_t children_num;       // 0x9C: number of children
            uint64_t a, b, c;           // 0xA4: unknown
            uint32_t d;                 // 0xBC: unknown
        } dir;
        
        struct {                        // For files (FT_INT_FIB)
            uint16_t update_in_progress; // 0x94: being updated?
            uint8_t f96;                // 0x96: unknown
            uint8_t flags;              // 0x97: file flags
            PhysPageId blocks_loc;      // 0x98: metatable descriptors location
            uint64_t nBlocks;           // 0xA0: number of blocks
            uint64_t fib_size;          // 0xA8: filesize
            PhysPageId undir_loc;       // 0xB0: unknown
            int64_t fb8;                // 0xB8: unknown
        } fib;
        
        struct {                        // For increments (FT_INCREMENT/FT_PATCH)
            uint16_t update_in_progress; // 0x94: being updated?
            uint8_t f96;                // 0x96: unknown
            uint8_t flags;              // 0x97: file flags
            PhysPageId blocks_loc;      // 0x98: patch block descriptors
            uint64_t nBlocks;           // 0xA0: number of patch blocks
            uint64_t fib_size;          // 0xA8: original file size
            uint64_t inc_size;          // 0xB0: increment size
            PhysPageId versions_loc;    // 0xB8: version info
        } inc;
    } u;
};
```

### File Types

```c
enum EFileType {
    FT_SUBFOLDER = 1,    // directory
    FT_EXT_FIB   = 2,    // external file
    FT_INT_FIB   = 3,    // internal file (VBK)
    FT_PATCH     = 4,    // patch
    FT_INCREMENT = 5,    // increment (VIB)
};
```

## Block Storage Hierarchy

### `SMetaTableDescriptor`

Groups file block descriptors together. Only used for full backups.

```c
struct SMetaTableDescriptor { //  24 bytes
    PhysPageId ppi;                     // 0x00: page with block descriptors
    int64_t block_size;                 // 0x08: size of blocks (or last block size if one block)
    int64_t nBlocks;                    // 0x10: number of blocks in this table
};
```

**Special cases:**
- `nBlocks=0, ppi={-1,-1}, block_size=0x100000` - sparse table (0x440 zero blocks are written)
- `nBlocks=1, block_size<0x100000` - last partial block  
- `nBlocks>1, block_size=0x100000` - regular full blocks
- `nBlocks=0, ppi={0,0}, block_size=0` - end marker

**Capacity:** Max 0x440 (1088) blocks per table (roughly 1gb of data)

For a full backup, `SDirItemRec.blocks_loc` points to `MetaVec<SMetaTableDescriptor>`. And each descriptor's `ppi` points to`MetaVec<SFibBlockDescriptorV7>`.

### `SFibBlockDescriptorV7` (Full Backup Blocks)

Describes a single file block. Used in full backups.

```c
struct SFibBlockDescriptorV7 { // 46 bytes
    uint32_t size;                      // 0x0: block size (≤ 0x100000)
    uint8_t type;                       // 0x4: location type (1 or 4)
    digest_t digest;                    // 0x5: MD5 of decompressed data
    uint64_t id;                        // 0x15: index into DataStore
    uint8_t flags;                      // 0x1D: flags
    digest_t keyset_id;                 // 0x1E: encryption key (or zeros)
};
```

The `id` field is an index into the datastore (at `DataStoreRootPage`). Look up `BlockDescriptor[id]` to find where the compressed data is stored in the file. 

### `SPatchBlockDescriptorV7` (Incremental Blocks)

Describes changed blocks. Used in incremental backups.

```c
struct SPatchBlockDescriptorV7 { // 53 bytes
    uint32_t size;                      // 0x00: block size (usually 0x100000)
    uint8_t type;                       // 0x04: location type (usually 0)
    digest_t digest;                    // 0x05: MD5 of decompressed data
    int64_t id;                         // 0x15: index into DataStore
    int64_t block_idx;                  // 0x1D: which 1 mb block in original file
    digest_t keyset_id;                 // 0x25: encryption key (or zeros)
};
```

**block_idx** tells which 1 mb chunk of the original file this replaces. File offset = `block_idx * 0x100000`.

For incremental backups, `SDirItemRec.blocks_loc` points directly to `MetaVec<SPatchBlockDescriptorV7>` (no `SMetaTableDescriptor` layer).

### `BlockDescriptor` (DataStore Entry)

Holds the actual block information. Vector pointer stored at `DataStoreRootPage`.

```c
struct BlockDescriptor { // 60 bytes
    uint8_t location;                   // 0x0: where it's stored (see EBlockLocation)
    uint32_t usageCnt;                  // 0x1: reference count (total references of this block)
    uint64_t offset;                    // 0x5: file offset to data
    uint32_t allocSize;                 // 0xD: allocated size
    uint8_t dedup;                      // 0x11: dedup flag
    digest_t digest;                    // 0x12: MD5 of compressed data
    uint8_t compType;                   // 0x22: compression type
    uint8_t unused;                     // 0x23: padding
    uint32_t compSize;                  // 0x24: compressed size
    uint32_t srcSize;                   // 0x28: original uncompressed size
    digest_t keysetID;                  // 0x2C: encryption key (or zeros)
};
```

**Block extraction:**
1. Get `BlockDescriptor` using `id` from `SFibBlockDescriptorV7`/`SPatchBlockDescriptorV7`
2. Check `location` - if `BL_BLOCK_IN_BLOB` (4), data is at `offset` in file
3. Read `compSize` bytes from `offset`
4. Decompress using `compType`
5. Verify size matches `srcSize` and MD5 matches file descriptor's digest

### Block Locations

```c
enum EBlockLocation {
    BL_NORMAL               = 0,  // unknown
    BL_SPARSE               = 1,  // all zeros, not stored
    BL_RESERVED             = 2,  // reserved
    BL_ARCHIVED             = 3,  // unknown
    BL_BLOCK_IN_BLOB        = 4,  // stored in file (most common)
    BL_BLOCK_IN_BLOB_RESERVED = 5 // reserved 
};
```

## Compression

```c
enum ECompType {
    CT_NONE    = 0xFF,   // no compression
    CT_RLE     = 2,      // run length encoding
    CT_ZLIB_HI = 3,      // zlib high
    CT_ZLIB_LO = 4,      // zlib low
    CT_LZ4     = 7,      // LZ4
    CT_ZSTD3   = 8,      // zstandard lvl 3
    CT_ZSTD9   = 9       // zstandard lvl 9
};
```

### LZ4 Header

 LZ4 compressed blocks have a 12 byte header:

```c
struct lz_hdr {
    uint32_t magic;                     // 0x0F800000F
    uint32_t crc;                       // CRC32 of decompressed data
    uint32_t srcSize;                   // original size
}; //compressed data follows immediately
```

Other compression types (zlib, zstd) have no custom header.

## Deduplication 

```c
struct SDedupRec {                      // 0x20 bytes
    PhysPageId ppi;                     // 0x00: block location
    digest_t hash;                      // 0x08: MD5 hash
    int64_t refCnt;                     // 0x18: reference count
};
```

`refCnt` indicates how many fib / patch block descriptors reference the block.

## Extracting a file

**For VBK (full backup):**
1. Read `FileHeader`, and load a valid `Slot`
2. Load banks from `slot.bank_infos`
3. Parse `SDirItemRec` entries at `MetaRootDirPage`
4. Find relevant file, and get it's `PhysPageId` (stored at `blocks_loc`)
5. Read the `MetaVec<SMetaTableDescriptor>` vector at `blocks_loc`
6. For each `SMetaTableDescriptor` entry:
    - Read `MetaVec<SFibBlockDescriptorV7>` at `ppi`
    - Special handling for some cases is mentioned above.
7. For each `SFibBlockDescriptorV7`entry:
    - If `location == BL_SPARSE`, write zeros
    - Otherwise look up `BlockDescriptor[id]` at `DataStoreRootPage`
    - Read `compSize` bytes from `offset`
    - Decompress using the algo specified in`compType`
    - Verify size and hash.

**For VIB (incremental):**
- Same as VBK but `blocks_loc` points directly to `MetaVec<SPatchBlockDescriptorV7>` (no `SMetaTableDescriptor` layer)
- Use `block_idx` to determine file offset: `block_idx * 0x100000`
- Apply changes to corresponding file extracted from the base VBK 

## Other Structures (not used in VeeamPhaser)
## `MetaBlob`

Variable length binary data. Max 7999 pages.

```c
struct MetaBlobPage {
    PhysPageId next;                    // 0x0: next page (-1:-1 if last)
    uint32_t size;                      // 0x8: total blob size (first page only)
    uint8_t data[0xFF4];                // 0xC: data (4084 bytes per page)
};
```

Used for file properties and other metadata.

## File Properties (`CPropsDictionary`)

Key-value dictionary stored as `MetaBlob`.

```c
struct PropEntry {
    int32_t type;                       // property type
    string key;                         // property name
    variant value;                      // value 
};
```

### Property Types

```c
enum PropType {
    PROPTYPE_INT    = 1,    // int32_t
    PROPTYPE_UINT64 = 2,    // uint64_t
    PROPTYPE_MBS    = 3,    // multi-byte string
    PROPTYPE_WCS    = 4,    // wide-char string
    PROPTYPE_BIN    = 5,    // binary data
    PROPTYPE_BOOL   = 6,    // boolean (int32_t)
};
```

Strings are stored as: uint32_t length + data bytes (no null terminator), and dictionary ends when type = -1.

## Encryption

```c
struct SKeySetRec { // 592 bytes
    digest_t uuid;                      // 0x00: keyset UUID
    uint32_t algo;                      // 0x10: algorithm ID
    char hint[0x200];                   // 0x14: password hint
    uint32_t role;                      // 0x214: key role
    uint64_t magic[4];                  // 0x218: magic values
    PhysPageId key_blobs_loc;           // 0x238: key blobs
    PhysPageId restore_recs_loc;        // 0x240: restore records
    uint64_t timestamp;                 // 0x248: creation time
};
```

Needs more research for proper support.
## Reference Code

The python parser `parse_vbk.py` shows how to parse VBK/VIB files without decompression or extraction.
