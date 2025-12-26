# This script is based on the C++ source with props dictionary implementation from ruby script

import struct
import sys

BLOCK_SIZE = 0x100000

class PhysPageId:
    def __init__(self, page_id, bank_id):
        self.page_id = page_id
        self.bank_id = bank_id

    def empty(self):
        # {-1, -1} means no page (empty reference/null pointer)
        return self.page_id == -1 and self.bank_id == -1

    def zero(self):
        # {0, 0} is an end marker or empty page reference
        return self.page_id == 0 and self.bank_id == 0

    def valid(self):
        # valid pages have non-negative ids
        return self.page_id >= 0 and self.bank_id >= 0
    
    def valid_or_empty(self):
        # valid or explicitly empty
        return self.valid() or self.empty()
    
    def __repr__(self):
        return f"PhysPageId(bank_id={self.bank_id}, page_id={self.page_id})"


class MetaVec:
    def __init__(self, vbk_parser, root_ppi):
        self.parser = vbk_parser
        self.root_ppi = root_ppi
    
    @staticmethod
    def is_metavec_start(page_data, page_id):
        if len(page_data) < 12:
            return False
        p0, p1, p2 = struct.unpack_from('<iii', page_data, 0)
        return p2 == page_id and not (p0 == -1 and p1 == -1)
    
    def read_pages(self):
        pages = []
        current_ppi = self.root_ppi

        while current_ppi.valid():
            page_data = self.parser.get_page(current_ppi)
            if page_data is None:
                break

            i = 0x10
            while i < 0x1000:
                if (i - 0x10) % (512 * 8) == 0 and i != 0x10:
                    i += 8
                    continue
                page_id, bank_id = struct.unpack_from('<ii', page_data, i)
                if not (page_id == -1 and bank_id == -1):
                    if page_id >= 0 and bank_id >= 0:
                        pages.append(PhysPageId(page_id, bank_id))
                i += 8

            next_page_id, next_bank_id = struct.unpack_from('<ii', page_data, 4)
            current_ppi = PhysPageId(next_page_id, next_bank_id)
            if current_ppi.empty():
                break

        return pages


class MetaVec2:
    def __init__(self, vbk_parser, root_ppi):
        self.parser = vbk_parser
        self.root_ppi = root_ppi
    
    @staticmethod
    def is_metavec2_start(page_data, page_id):
        if len(page_data) < 12:
            return False
        p0, p1, p2 = struct.unpack_from('<iii', page_data, 0)
        return p0 == -1 and p1 == -1 and p2 == page_id
    
    def read_pages(self):
        pages = []
        current_ppi = self.root_ppi
        is_root = True

        while current_ppi.valid():
            page_data = self.parser.get_page(current_ppi)
            if page_data is None:
                break

            start_offset = 0x14 if is_root else 0x10
            i = start_offset
            
            while i < 0x1000:
                if (i - start_offset) % (512 * 8) == 0 and i != start_offset:
                    i += 8
                    continue
                page_id, bank_id = struct.unpack_from('<ii', page_data, i)
                if not (page_id == -1 and bank_id == -1):
                    if page_id >= 0 and bank_id >= 0:
                        pages.append(PhysPageId(page_id, bank_id))
                i += 8

            if is_root:
                next_page_id, next_bank_id = struct.unpack_from('<ii', page_data, 0x10)
            else:
                next_page_id, next_bank_id = struct.unpack_from('<ii', page_data, 4)
            
            current_ppi = PhysPageId(next_page_id, next_bank_id)
            is_root = False
            if current_ppi.empty():
                break

        return pages


class FileHeader:
    def __init__(self, f):
        f.seek(0)
        data = f.read(0x1000)
        
        version, inited, digest_type_len = struct.unpack_from('<III', data, 0)
        digest_type_bytes = data[0xC:0xC + digest_type_len]
        digest_type = digest_type_bytes.decode()
        
        # format info at offset 0x107
        slot_fmt, standard_block_size, cluster_align = struct.unpack_from('<III', data, 0x107)

        self.version = version
        self.inited = inited  # should be 1 for valid files
        self.digest_type_len = digest_type_len
        self.digest_type = digest_type  # usually "md5"
        self.slot_fmt = slot_fmt 
        self.standard_block_size = standard_block_size
        self.cluster_align = cluster_align

    def max_banks(self):
        # different slot formats support different bank counts
        if self.slot_fmt == 0:
            return 0xF8
        elif self.slot_fmt in (5, 9):
            return 0x7F00
        return 0

    def slot_size(self):
        # calculate slot size based on max banks (ensure alignment)
        return ((((self.max_banks() * 0x10) & 0xFFFFFFF0) + 120) & 0xFFFFF000) + 0x1000

    def valid(self):
        # basic sanity checks for header fields
        return (
            self.inited == 1 and
            self.version != 0 and
            self.digest_type_len == len(self.digest_type) and self.digest_type_len <=  250 and self.standard_block_size != 0 and self.standard_block_size % 512 == 0 and self.cluster_align != 0 and self.slot_fmt <= 9
        )
    
    def assert_valid(self):
        assert self.inited == 1, f"FileHeader: not initialized (inited={self.inited})"
        assert self.version != 0, "FileHeader: version is 0"
        assert self.digest_type_len == len(self.digest_type), f"FileHeader: digest_type_len mismatch ({self.digest_type_len} != {len(self.digest_type)})"
        assert self.digest_type_len <= 250, f"FileHeader: digest_type_len too large ({self.digest_type_len})"
        assert self.standard_block_size != 0, "FileHeader: standard_block_size is 0"
        assert self.standard_block_size % 512 == 0, f"FileHeader: standard_block_size not 512 aligned ({self.standard_block_size})"
        assert self.cluster_align != 0, "FileHeader: cluster_align is 0"
        assert self.slot_fmt <= 9, f"FileHeader: invalid slot_fmt ({self.slot_fmt})"

class BankInfo:
    def __init__(self, crc, offset, size):
        self.crc = crc
        self.offset = offset  # file offset where bank starts
        self.size = size      # bank size in bytes

    def valid(self, file_size):
        return (
            self.crc != 0 and
            self.offset > 0 and self.offset < file_size and
            self.size % 0x1000 == 0 and  # must be page-aligned
            self.size >= 0x22000 and     # reasonable minimum
            self.size <= 0x402000        # reasonable maximum
        )
    
    def assert_valid(self, file_size):
        assert self.crc != 0, f"BankInfo: CRC is 0"
        assert self.offset > 0, f"BankInfo: offset <= 0 ({self.offset})"
        assert self.offset < file_size, f"BankInfo: offset {self.offset} >= file_size {file_size}"
        assert self.size % 0x1000 == 0, f"BankInfo: size not page-aligned ({self.size:#x})"
        assert self.size >= 0x22000, f"BankInfo: size too small ({self.size:#x})"
        assert self.size <= 0x402000, f"BankInfo: size too large ({self.size:#x})"

class ObjRefs:
    def __init__(self, data, offset):
        def read_ppi(off):
            page_id, bank_id = struct.unpack_from('<ii', data, off)
            return PhysPageId(page_id, bank_id)

        # main directory tree
        self.MetaRootDirPage   = read_ppi(offset)
        self.children_num      = struct.unpack_from('<Q', data, offset + 8)[0]
        
        # block storage area
        self.DataStoreRootPage = read_ppi(offset + 16)
        self.BlocksCount       = struct.unpack_from('<Q', data, offset + 24)[0]
        
        # other root pointers
        self.free_blocks_root  = read_ppi(offset + 32)
        self.dedup_root        = read_ppi(offset + 40)
        _ = read_ppi(offset + 48)  # reserved/unused
        _ = read_ppi(offset + 56)  # reserved/unused

        self.CryptoStoreRootPage   = read_ppi(offset + 64)
        self.ArchiveBlobStorePage  = read_ppi(offset + 72)


class SnapshotDescriptor:
    def __init__(self, data, offset):
        version, storage_eof, nBanks = struct.unpack_from('<QQI', data, offset)
        self.version = version
        self.storage_eof = storage_eof  # end of used storage
        self.nBanks = nBanks
        self.objRefs = ObjRefs(data, offset + 20)
        _ = struct.unpack_from('<Q', data, offset + 100)[0]  # padding in some versions

class Slot:
    def __init__(self, f, offset, max_banks):
        f.seek(offset)
        data = f.read(0x80000)  # read entire slot

        # slot header
        crc, has_snapshot = struct.unpack_from('<II', data, 0)
        self.crc = crc
        self.has_snapshot = has_snapshot
        self.snapshot_desc = SnapshotDescriptor(data, 8)

        # bank allocation info
        _, max_banks, allocated_banks = struct.unpack_from('<QII', data, 0x6C)
        self.max_banks = max_banks
        self.allocated_banks = allocated_banks

        # read bank info table
        bank_infos = []
        i = 0
        while i < allocated_banks:  
            bi_off = 0x7C + i * 16  # each bank info is 16 bytes
            bi_crc, bi_offset_val, bi_size = struct.unpack_from('<IqI', data, bi_off)
            bank_infos.append(BankInfo(bi_crc, bi_offset_val, bi_size))
            i += 1

        self.bank_infos = bank_infos
        self.offset = offset

    def valid_fast(self):
        # quick check without deep validation
        return (
            self.crc != 0 and
            self.has_snapshot == 1 and
            self.max_banks > 0 and self.max_banks <= 0xFFA0 and
            self.allocated_banks <= self.max_banks
        )
    
    def assert_valid(self):
        assert self.crc != 0, f"Slot: CRC is 0"
        assert self.has_snapshot == 1, f"Slot: has_snapshot != 1 ({self.has_snapshot})"
        assert self.max_banks > 0, "Slot: max_banks is 0"
        assert self.max_banks <= 0xFFA0, f"Slot: max_banks too large ({self.max_banks:#x})"
        assert self.allocated_banks <= self.max_banks, f"Slot: allocated_banks ({self.allocated_banks}) > max_banks ({self.max_banks})"

    def size(self):
        return 8 + 108 + 16 + self.max_banks * 16

class BankHeader:
    def __init__(self, data):
        nPages, encr_mode, f3 = struct.unpack_from('<HBB', data, 0)
        self.nPages = nPages
        self.encr_mode = encr_mode
        _ = f3  # unused field
        self.free_pages = data[4:0x404]  # bitmap: 1 byte per page (1 = free)
        self.keyset_id_16 = data[0xC04:0xC04 + 16]
        self.encr_size = struct.unpack_from('<I', data, 0xC14)[0]

    def is_encrypted(self):
        return self.encr_size > 0 and self.keyset_id_16 != b'\x00' * 16

class Bank:
    def __init__(self, f, info):
        self.info = info
        f.seek(info.offset)
        header_data = f.read(0x1000)
        self.header = BankHeader(header_data)
        data_size = info.size - 0x1000  # subtract header size
        self.total_pages = data_size // 0x1000
        self.data = f.read(data_size)

    def get_page(self, page_id):

        if page_id < 0 or page_id >= self.header.nPages:
            return None
        # check if page is marked as free
        if self.header.free_pages[page_id] == 1:
            return None
        off = page_id * 0x1000
        return self.data[off:off + 0x1000]

class SStgBlockDescriptorV7:
    SIZE = 0x3c  # 60 bytes
    
    # compression algorithms
    CT_NONE = 0xFF
    CT_RLE = 2
    CT_ZLIB_HI = 3
    CT_ZLIB_LO = 4
    CT_LZ4 = 7
    CT_ZSTD3 = 8
    CT_ZSTD9 = 9

    # block storage locations
    BL_NORMAL = 0
    BL_SPARSE = 1
    BL_RESERVED = 2
    BL_ARCHIVED = 3
    BL_BLOCK_IN_BLOB = 4  # most common case
    BL_BLOCK_IN_BLOB_RESERVED = 5

    def __init__(self, data, offset=0):

        location = struct.unpack_from('<B', data, offset)[0]
        ref_cnt = struct.unpack_from('<I', data, offset + 1)[0]
        off = struct.unpack_from('<Q', data, offset + 5)[0]
        alloc_size = struct.unpack_from('<I', data, offset + 0x0D)[0]
        dedup = struct.unpack_from('<B', data, offset + 0x11)[0]
        digest_low, digest_high = struct.unpack_from('<QQ', data, offset + 0x12)
        comp_type, _ = struct.unpack_from('<BB', data, offset + 0x22)
        comp_size = struct.unpack_from('<I', data, offset + 0x24)[0]
        src_size = struct.unpack_from('<I', data, offset + 0x28)[0]
        keyset_id_low, keyset_id_high = struct.unpack_from('<QQ', data, offset + 0x2C)

        # digest bytes need endianness swap (Veeam quirk)
        raw_digest = struct.pack('<QQ', digest_low, digest_high)
        digest_swapped = raw_digest[0:8][::-1] + raw_digest[8:16][::-1]

        self.location = location
        self.ref_cnt = ref_cnt  # reference count for deduplication
        self.offset = off       # file offset where compressed data lives
        self.alloc_size = alloc_size
        self.dedup = dedup
        self.digest_16 = digest_swapped
        self.comp_type = comp_type
        self.comp_size = comp_size  # compressed size
        self.src_size = src_size    # original size
        self.keyset_id_16 = struct.pack('<QQ', keyset_id_low, keyset_id_high)

    def has_digest(self):
        return self.digest_16 != b'\x00' * 16

    def digest_hex(self):
        return self.digest_16.hex()

    def comp_type_str(self):
        # human-readable compression type names
        t = {0xFF: "None", 2: "RLE", 3: "ZlibHi", 4: "ZlibLo", 7: "LZ4", 8: "Zstd3", 9: "Zstd9"}
        return t.get(self.comp_type, "Unknown(0x%02x)" % self.comp_type)

    def location_str(self):
        # human-readable location names
        m = {0: "Normal", 1: "Sparse", 2: "Reserved", 3: "Archived", 4: "BlockInBlob", 5: "BlockInBlobReserved"}
        return m.get(self.location, "Unknown(%d)" % self.location)
    
    def is_valid_comp_type(self):
        return (self.comp_type == self.CT_NONE or 
                (self.comp_type >= self.CT_RLE and self.comp_type <= self.CT_ZSTD9))
    
    def valid(self):
        # validate that a block descriptor makes sense
        return (
            self.location == self.BL_BLOCK_IN_BLOB and
            self.alloc_size != 0 and
            self.alloc_size >= self.comp_size and
            (
                # either it has real data
                (self.has_digest() and self.comp_size != 0 and self.src_size != 0 and 
                 self.is_valid_comp_type()) or
                # or it's empty
                (not self.has_digest() and self.comp_size == 0 and self.src_size == 0 and 
                 self.comp_type == 0 and self.dedup == 0)
            )
        )
    
    def is_empty(self):
        # check for empty block patterns
        zero_block = (self.location == 0 and self.ref_cnt == 0 and self.offset == 0 and
                      self.alloc_size == 0 and not self.has_digest())
        ff_check = (self.location == 0xff and self.ref_cnt == 0xffffffff)
        return zero_block or ff_check
    
    def assert_valid(self):
        assert self.location == self.BL_BLOCK_IN_BLOB, f"SStgBlockDescriptorV7: invalid location ({self.location})"
        assert self.alloc_size != 0, "SStgBlockDescriptorV7: alloc_size is 0"
        assert self.alloc_size >= self.comp_size, f"SStgBlockDescriptorV7: alloc_size ({self.alloc_size}) < comp_size ({self.comp_size})"
        
        if self.has_digest():
            assert self.comp_size != 0, "SStgBlockDescriptorV7: has digest but comp_size is 0"
            assert self.src_size != 0, "SStgBlockDescriptorV7: has digest but src_size is 0"
            assert self.is_valid_comp_type(), f"SStgBlockDescriptorV7: invalid comp_type (0x{self.comp_type:02x})"
        else:
            assert self.comp_size == 0, f"SStgBlockDescriptorV7: no digest but comp_size != 0 ({self.comp_size})"
            assert self.src_size == 0, f"SStgBlockDescriptorV7: no digest but src_size != 0 ({self.src_size})"
            assert self.comp_type == 0, f"SStgBlockDescriptorV7: no digest but comp_type != 0 (0x{self.comp_type:02x})"
            assert self.dedup == 0, f"SStgBlockDescriptorV7: no digest but dedup != 0 ({self.dedup})"

class SMetaTableDescriptor:
    SIZE = 0x18  # 24 bytes
    MAX_BLOCKS = 0x440  # veeam's magic number for max blocks per descriptor
    BLOCK_SIZE = 0x100000
    
    def __init__(self, data, offset=0):
        loc, size, nBlocks = struct.unpack_from('<QQQ', data, offset)
        self.loc = loc 
        self.size = size    # block size 
        self.nBlocks = nBlocks  # number of blocks this describes

    def get_ppi(self):
        page_id = self.loc & 0xFFFFFFFF
        bank_id = (self.loc >> 32) & 0xFFFFFFFF
        return PhysPageId(page_id, bank_id)
    
    def is_empty(self):
        # all zeros = end of list marker
        ppi = self.get_ppi()
        return ppi.zero() and self.size == 0 and self.nBlocks == 0
    
    def is_sparse(self):
        # sparse blocks have empty ppi but standard block size
        ppi = self.get_ppi()
        return self.nBlocks == 0 and ppi.empty() and self.size == self.BLOCK_SIZE
    
    def valid(self):
        ppi = self.get_ppi()
        if self.nBlocks == 0:
            # must be sparse
            return self.is_sparse()
        elif self.nBlocks == 1:
            # partial block at end of file
            return ppi.valid() and not ppi.zero() and self.size > 0 and self.size < self.BLOCK_SIZE
        else:
            # normal full blocks
            return ppi.valid() and not ppi.zero() and self.size == self.BLOCK_SIZE and self.nBlocks <= self.MAX_BLOCKS
    
    def assert_valid(self):
        ppi = self.get_ppi()
        if self.nBlocks == 0:
            assert ppi.empty(), f"SMetaTableDescriptor: sparse block should have empty PPI, got {ppi.bank_id}:{ppi.page_id}"
            assert self.size == self.BLOCK_SIZE, f"SMetaTableDescriptor: sparse block size should be {self.BLOCK_SIZE}, got {self.size}"
        elif self.nBlocks == 1:
            assert ppi.valid(), f"SMetaTableDescriptor: partial block has invalid PPI {ppi.bank_id}:{ppi.page_id}"
            assert not ppi.zero(), f"SMetaTableDescriptor: partial block has zero PPI"
            assert self.size > 0, "SMetaTableDescriptor: partial block size is 0"
            assert self.size < self.BLOCK_SIZE, f"SMetaTableDescriptor: partial block size {self.size} >= BLOCK_SIZE {self.BLOCK_SIZE}"
        else:
            assert ppi.valid(), f"SMetaTableDescriptor: regular block has invalid PPI {ppi.bank_id}:{ppi.page_id}"
            assert not ppi.zero(), f"SMetaTableDescriptor: regular block has zero PPI"
            assert self.size == self.BLOCK_SIZE, f"SMetaTableDescriptor: regular block size should be {self.BLOCK_SIZE}, got {self.size}"
            assert self.nBlocks <= self.MAX_BLOCKS, f"SMetaTableDescriptor: nBlocks {self.nBlocks} > MAX_BLOCKS {self.MAX_BLOCKS}"

class SparseBlockDescriptor:
    def __init__(self):
        self.size = 0
        self.loc_type = 0
        self.digest_16 = b'\x00' * 16
        self.id = 0
        self.flags = 0
        self.keyset_id_16 = b'\x00' * 16
    
    def digest_hex(self):
        return self.digest_16.hex()
    
    def valid(self):
        return False 
    
    def is_sparse(self):
        return True
    
    def is_encrypted(self):
        return False


class SFibBlockDescriptorV7:
    SIZE = 0x2e  # 46 bytes
    
    def __init__(self, data, offset=0):
        size, loc_type = struct.unpack_from('<IB', data, offset)
        raw_digest = data[offset + 5: offset + 21]

        # same digest endianness swap as storage blocks
        digest_swapped = raw_digest[0:8][::-1] + raw_digest[8:16][::-1]
        id_val, flags = struct.unpack_from('<QB', data, offset + 21)
        keyset_id_16 = data[offset + 30: offset + 46]

        self.size = size  # can be less than BLOCK_SIZE for final block
        self.loc_type = loc_type
        self.digest_16 = digest_swapped
        self.id = id_val  # index of storage block in datastore
        self.flags = flags
        self.keyset_id_16 = keyset_id_16

    def digest_hex(self):
        return self.digest_16.hex()
    
    def valid(self):
        return (
            self.size > 0 and self.size <= BLOCK_SIZE and
            self.loc_type in (0, 1) and
            self.digest_16 != b'\x00' * 16
        )
    
    def assert_valid(self):
        assert self.size > 0, "SFibBlockDescriptorV7: size is 0"
        assert self.size <= BLOCK_SIZE, f"SFibBlockDescriptorV7: size {self.size} > BLOCK_SIZE {BLOCK_SIZE}"
        assert self.loc_type in (0, 1), f"SFibBlockDescriptorV7: invalid loc_type ({self.loc_type})"
        assert self.digest_16 != b'\x00' * 16, "SFibBlockDescriptorV7: digest is all zeros"
    
    def is_sparse(self):
        return self.size == 0 and self.id == 0 and self.digest_16 == b'\x00' * 16
    
    def is_encrypted(self):
        return self.keyset_id_16 != b'\x00' * 16


class SPatchBlockDescriptorV7:
    def __init__(self, data, offset=0):
        size, loc_type = struct.unpack_from('<IB', data, offset)
        raw_digest = data[offset + 5: offset + 21]
        digest_swapped = raw_digest[0:8][::-1] + raw_digest[8:16][::-1]
        id_val, offset_val = struct.unpack_from('<QQ', data, offset + 21)
        keyset_id_16 = data[offset + 37: offset + 53]

        self.size = size
        self.loc_type = loc_type
        self.digest_16 = digest_swapped
        self.id = id_val  # datastore block ID
        self.offset = offset_val  # block offset in BLOCK_SIZE units
        self.keyset_id_16 = keyset_id_16

    def digest_hex(self):
        return self.digest_16.hex()

    def valid(self):
        return (
            self.size == BLOCK_SIZE and
            self.loc_type == 0 and
            self.digest_16 != b'\x00' * 16 and
            self.id >= 0 and
            self.offset >= 0
        )
    
    def assert_valid(self):
        assert self.size == BLOCK_SIZE, f"SPatchBlockDescriptorV7: size should be {BLOCK_SIZE}, got {self.size}"
        assert self.loc_type == 0, f"SPatchBlockDescriptorV7: loc_type should be 0, got {self.loc_type}"
        assert self.digest_16 != b'\x00' * 16, "SPatchBlockDescriptorV7: digest is all zeros"
        assert self.id >= 0, f"SPatchBlockDescriptorV7: id is negative ({self.id})"
        assert self.offset >= 0, f"SPatchBlockDescriptorV7: offset is negative ({self.offset})"
        assert self.keyset_id_16 == b'\x00' * 16, f"SPatchBlockDescriptorV7: keyset_id should be zeros, got {self.keyset_id_16.hex()}"


class CPropsDictionary:

    PROPTYPE_INT = 1
    PROPTYPE_UINT64 = 2
    PROPTYPE_MBS = 3    # multi byte string (UTF-8)
    PROPTYPE_WCS = 4    # wide character string (UTF-16LE)
    PROPTYPE_BIN = 5    # binary data
    PROPTYPE_BOOL = 6
    
    def __init__(self, data, offset=0):
        self.properties = {}
        self._read(data, offset)
    
    def _read(self, data, offset):
        # parse the binary property format
        while offset < len(data):
            if offset + 4 > len(data):
                break
            
            prop_type = struct.unpack_from('<i', data, offset)[0]
            offset += 4
            
            if prop_type == -1:  # end marker
                break
            
            if prop_type < 1 or prop_type > 6:
                break
            
            # read key name
            if offset + 4 > len(data):
                break
            key_len = struct.unpack_from('<I', data, offset)[0]
            offset += 4
            
            if key_len > 0x100 or offset + key_len > len(data):
                break
            
            key = data[offset:offset + key_len].decode('utf-8', errors='ignore').rstrip('\x00')
            offset += key_len
            
            if not key or not all(0x20 <= ord(c) <= 0x7e for c in key):
                break
            
            if prop_type == self.PROPTYPE_INT:
                if offset + 4 > len(data):
                    break
                value = struct.unpack_from('<i', data, offset)[0]
                offset += 4
                self.properties[key] = value
                
            elif prop_type == self.PROPTYPE_UINT64:
                if offset + 8 > len(data):
                    break
                value = struct.unpack_from('<Q', data, offset)[0]
                offset += 8
                self.properties[key] = value
                
            elif prop_type == self.PROPTYPE_BOOL:
                if offset + 4 > len(data):
                    break
                value = struct.unpack_from('<i', data, offset)[0] != 0
                offset += 4
                self.properties[key] = value
                
            elif prop_type in (self.PROPTYPE_MBS, self.PROPTYPE_WCS, self.PROPTYPE_BIN):
                if offset + 4 > len(data):
                    break
                value_len = struct.unpack_from('<I', data, offset)[0]
                offset += 4
                
                if value_len > 0x100000 or offset + value_len > len(data):
                    break
                
                value = data[offset:offset + value_len]
                offset += value_len
                
                if prop_type == self.PROPTYPE_MBS:
                    try:
                        value = value.decode()
                    except:
                        pass
                elif prop_type == self.PROPTYPE_WCS:
                    try:
                        value = value.decode('utf-16le')
                    except:
                        pass
                
                self.properties[key] = value
            else:
                break
    
    def valid(self):
        return len(self.properties) > 0
    
    def __repr__(self):
        items = []
        for k, v in self.properties.items():
            if isinstance(v, bytes) and len(v) > 20:
                items.append(f"{k}=[{len(v)} bytes]")
            elif isinstance(v, str) and any(ord(c) < 0x20 or ord(c) > 0x7e for c in v):
                items.append(f"{k}=[{len(v)} chars]")
            else:
                items.append(f"{k}={repr(v)}")
        return "<CPropsDictionary " + ", ".join(items) + ">"

class MetaBlob:
    MAX_PAGES = 7999
    PAGE_PAYLOAD_SIZE = 0x1000 - 0xc  # page size minus 12 byte header
    MAX_PAYLOAD_SIZE = MAX_PAGES * PAGE_PAYLOAD_SIZE
    
    def __init__(self, parser, bank_id, page_id):
        self.parser = parser
        self.bank_id = bank_id
        self.page_id = page_id
        self.size = 0
        self.npages = 0
        self.data = None
    
    def get_data(self):
        if self.data is not None:
            return self.data
        
        # read first page to get total blob size
        first_page = self.parser.get_page(PhysPageId(self.page_id, self.bank_id))
        if not first_page or len(first_page) < 0xc:
            return None
        
        next_page_id, next_bank_id, size = struct.unpack_from('<iiI', first_page, 0)
        
        if size >= self.MAX_PAYLOAD_SIZE:
            return None
        
        self.size = size
        
        # calculate how many pages we need
        if size == 0:
            self.npages = 1
        elif size % self.PAGE_PAYLOAD_SIZE == 0:
            self.npages = size // self.PAGE_PAYLOAD_SIZE
        else:
            self.npages = size // self.PAGE_PAYLOAD_SIZE + 1
        
        # read all pages in the chain
        buffer = bytearray(size)
        pos = 0
        current_bank_id = self.bank_id
        current_page_id = self.page_id
        
        for i in range(self.npages):
            page_data = self.parser.get_page(PhysPageId(current_page_id, current_bank_id))
            if not page_data or len(page_data) < 0xc:
                return None
            
            next_page_id, next_bank_id, _ = struct.unpack_from('<iiI', page_data, 0)
            
            chunk_size = self.PAGE_PAYLOAD_SIZE
            if pos + self.PAGE_PAYLOAD_SIZE > size:
                chunk_size = size - pos
                if size == pos:
                    break
 
            buffer[pos:pos + chunk_size] = page_data[0xc:0xc + chunk_size]
            pos += chunk_size
            
            if next_page_id == -1 and next_bank_id == -1:
                break
            
            current_page_id = next_page_id
            current_bank_id = next_bank_id
        
        self.data = bytes(buffer)
        return self.data


class SDirItemRec:
    SIZE = 0xc0  # 192 bytes
    
    # file type constants
    FT_SUBFOLDER = 1
    FT_EXT_FIB = 2    # external file
    FT_INT_FIB = 3    # internal file
    FT_PATCH = 4      # incremental patch
    FT_INCREMENT = 5  # incremental backup

    def __init__(self, data, offset=0):
        file_type, name_len = struct.unpack_from('<II', data, offset)
        self.file_type = file_type
        self.name_len = name_len

        if file_type == 0:  # end marker
            self.name = ""
            self.props_loc = PhysPageId(-1, -1)
            self.blocks_loc = None
            self.nBlocks = 0
            self.fib_size = 0
            self.children_loc = None
            self.children_num = 0
            self.flags = 0
            self.inc_size = 0
            return

        name_bytes = data[offset + 8:offset + 8 + min(name_len, 0x80)]
        self.name = name_bytes.decode('utf-8', errors='ignore').rstrip('\x00')

        # properties location 
        props_page_id, props_bank_id = struct.unpack_from('<ii', data, offset + 0x88)
        self.props_loc = PhysPageId(props_page_id, props_bank_id)
        _ = struct.unpack_from('<i', data, offset + 0x90)[0]  # reserved

        self.blocks_loc = None
        self.nBlocks = 0
        self.fib_size = 0
        self.children_loc = None
        self.children_num = 0
        self.flags = 0
        self.inc_size = 0

        if file_type == self.FT_SUBFOLDER:
            # directory - has children
            children_page_id, children_bank_id = struct.unpack_from('<ii', data, offset + 0x94)
            self.children_loc = PhysPageId(children_page_id, children_bank_id)
            self.children_num = struct.unpack_from('<Q', data, offset + 0x9C)[0]
        else:
            _, _, flags = struct.unpack_from('<HBB', data, offset + 0x94)
            blocks_page_id, blocks_bank_id = struct.unpack_from('<ii', data, offset + 0x98)
            self.blocks_loc = PhysPageId(blocks_page_id, blocks_bank_id)
            self.nBlocks, self.fib_size = struct.unpack_from('<QQ', data, offset + 0xA0)
            self.flags = flags
            if file_type in (self.FT_INCREMENT, self.FT_PATCH):
                self.inc_size = struct.unpack_from('<Q', data, offset + 0xB0)[0]

    def is_dir(self):
        return self.file_type == self.FT_SUBFOLDER

    def is_increment(self):
        return self.file_type in (self.FT_INCREMENT, self.FT_PATCH)

    def type_name(self):
        names = {1: "Dir", 2: "ExtFib", 3: "IntFib", 4: "Patch", 5: "Increment"}
        return names.get(self.file_type, "Unknown(%d)" % self.file_type)
    
    def valid_name(self):
        if not self.name or len(self.name) == 0:
            return False
        return not any(ord(c) < 0x20 for c in self.name)
    
    def valid(self, max_banks=0):
        if self.file_type < self.FT_SUBFOLDER or self.file_type > self.FT_INCREMENT:
            return False
        if not self.valid_name():
            return False
        if self.name_len == 0 or self.name_len > 0x80:
            return False
        
        if self.file_type == self.FT_SUBFOLDER:
            if not self.children_loc.valid():
                return False
            if self.children_num == 0:
                return False
            if max_banks > 0 and self.children_loc.bank_id >= max_banks:
                return False
        else:
            if self.fib_size == 0:
                return False
            if self.nBlocks > self.fib_size:
                return False
            if self.blocks_loc and not self.blocks_loc.valid():
                return False
            if max_banks > 0 and self.blocks_loc and self.blocks_loc.bank_id >= max_banks:
                return False
        
        return True
    
    def assert_valid(self, max_banks=0):
        assert self.file_type >= self.FT_SUBFOLDER and self.file_type <= self.FT_INCREMENT, \
            f"SDirItemRec: invalid file_type ({self.file_type})"
        assert self.valid_name(), f"SDirItemRec: invalid name '{self.name}'"
        assert self.name_len > 0 and self.name_len <= 0x80, \
            f"SDirItemRec: invalid name_len ({self.name_len})"
        
        if self.file_type == self.FT_SUBFOLDER:
            assert self.children_loc.valid(), \
                f"SDirItemRec: directory has invalid children_loc {self.children_loc.bank_id}:{self.children_loc.page_id}"
            assert self.children_num > 0, f"SDirItemRec: directory '{self.name}' has 0 children"
            if max_banks > 0:
                assert self.children_loc.bank_id < max_banks, \
                    f"SDirItemRec: children_loc bank_id {self.children_loc.bank_id} >= max_banks {max_banks}"
        else:
            assert self.fib_size > 0, f"SDirItemRec: file '{self.name}' has 0 fib_size"
            assert self.nBlocks <= self.fib_size, \
                f"SDirItemRec: file '{self.name}' has nBlocks ({self.nBlocks}) > fib_size ({self.fib_size})"
            if self.blocks_loc:
                assert self.blocks_loc.valid(), \
                    f"SDirItemRec: file '{self.name}' has invalid blocks_loc {self.blocks_loc.bank_id}:{self.blocks_loc.page_id}"
                if max_banks > 0:
                    assert self.blocks_loc.bank_id < max_banks, \
                        f"SDirItemRec: blocks_loc bank_id {self.blocks_loc.bank_id} >= max_banks {max_banks}"


class VBKParser:
    def __init__(self, filename):
        self.filename = filename
        self.file_type = "VIB" if filename.lower().endswith('.vib') else "VBK"

        self.file = open(filename, 'rb')
        self.file.seek(0, 2)
        self.file_size = self.file.tell()
        self.file.seek(0)

        # parse and validate file header
        self.header = FileHeader(self.file)
        self.header.assert_valid()
        
        self.slots = []
        self.banks = []

        if not self.header.valid():
            raise ValueError("Invalid file header")

        # read both slots (backup files have 2 for redundancy)
        i = 0
        while i < 2:
            offset = 0x1000 + i * 0x80000  # slots start after header
            slot = Slot(self.file, offset, self.header.max_banks())
            self.slots.append(slot)
            i += 1

        # pick the best slot to use
        best_slot = self.slots[0] if self.slots[0].valid_fast() else self.slots[1]
        best_slot.assert_valid()

        # load all the banks from the chosen slot
        banks_list = []
        for bank_info in best_slot.bank_infos:
            if bank_info.valid(self.file_size):
                bank_info.assert_valid(self.file_size)
                bank = Bank(self.file, bank_info)
                banks_list.append(bank)
            else:
                banks_list.append(None) 

        self.banks.append(banks_list)
        self.active_slot = best_slot

    def get_page(self, ppi):
        if not ppi.valid():
            return None
        if ppi.bank_id >= len(self.banks[0]):
            return None
        bank = self.banks[0][ppi.bank_id]
        if bank is None:
            return None
        return bank.get_page(ppi.page_id)

    def read_page_stack(self, ppi):
        page_data = self.get_page(ppi)
        if page_data is None:
            return []
        print(page_data)
        if MetaVec2.is_metavec2_start(page_data, ppi.page_id):
            print(f"Debug: Detected MetaVec2 format at {ppi.bank_id}:{ppi.page_id}")
            metavec = MetaVec2(self, ppi)
            return metavec.read_pages()
        elif MetaVec.is_metavec_start(page_data, ppi.page_id):
            print(f"Debug: Detected MetaVec format at {ppi.bank_id}:{ppi.page_id}")
            metavec = MetaVec(self, ppi)
            return metavec.read_pages()
        else:
            print(f"Warning: Unknown page stack format at {ppi.bank_id}:{ppi.page_id}")
            return []

    def read_dir_items(self, ppi):
        items = []
        pages = self.read_page_stack(ppi)

        for page_ppi in pages:
            page_data = self.get_page(page_ppi)
            if page_data is None:
                continue

            # each directory item is 192 bytes
            off = 0
            while off + 0xC0 <= 0x1000:
                item = SDirItemRec(page_data, off)
                if item.file_type == 0:  # end marker
                    break
                item.assert_valid(self.header.max_banks())
                items.append(item)
                off += 0xC0

        return items
    
    def read_dir_items_validated(self, ppi, max_banks=0):
        items = self.read_dir_items(ppi)
        for item in items:
            if not item.valid(max_banks):
                print(f"Warning: Invalid directory item '{item.name}' (type={item.file_type})")
        return items

    def read_block_descriptors(self, ppi, count=None):
        blocks = []
        pages = self.read_page_stack(ppi)

        for page_ppi in pages:
            page_data = self.get_page(page_ppi)
            if page_data is None:
                continue

            # each storage block descriptor is 60 bytes (0x3C)
            off = 0
            while off + 0x3C <= 0x1000:
                block = SStgBlockDescriptorV7(page_data, off)
                blocks.append(block)
                if count and len(blocks) >= count:
                    return blocks
                off += 0x3C

        return blocks

    def read_meta_table_descriptors(self, ppi, count=None):
        descriptors = []
        pages = self.read_page_stack(ppi)

        for page_ppi in pages:
            page_data = self.get_page(page_ppi)
            if page_data is None:
                continue

            off = 0
            while off + 0x18 <= 0x1000:
                desc = SMetaTableDescriptor(page_data, off)
                if desc.is_empty():  # end marker {0,0,0}
                    return descriptors
                descriptors.append(desc)
                if count and len(descriptors) >= count:
                    return descriptors
                off += 0x18

        return descriptors

    def read_file_block_descriptors(self, item):
        if not item.blocks_loc or not item.blocks_loc.valid():
            return []

        # first read the meta table descriptors
        meta_tables = self.read_meta_table_descriptors(item.blocks_loc, count=item.nBlocks)

        all_blocks = []
        for mt_desc in meta_tables:
            mt_ppi = mt_desc.get_ppi()
            
            if mt_desc.is_sparse():
                # sparse table - add empty blocks
                for _ in range(SMetaTableDescriptor.MAX_BLOCKS):
                    all_blocks.append(SparseBlockDescriptor())
                continue
            
            if not mt_ppi.valid():
                continue

            # read actual file block descriptors (46 bytes each)
            pages = self.read_page_stack(mt_ppi)
            for page_ppi in pages:
                page_data = self.get_page(page_ppi)
                if page_data is None:
                    continue

                blocks_per_page = 0x1000 // 0x2E 
                i = 0
                while i < blocks_per_page:
                    off = i * 0x2E
                    if off + 0x2E > 0x1000:
                        break
                    fib_block = SFibBlockDescriptorV7(page_data, off)
                    if fib_block.id == 0 and fib_block.size == 0:  # end marker
                        break
                    all_blocks.append(fib_block)
                    if len(all_blocks) >= item.nBlocks:
                        return all_blocks
                    i += 1

        return all_blocks

    def read_patch_block_descriptors(self, item):
        if not item.blocks_loc or not item.blocks_loc.valid():
            return []

        all_blocks = []
        pages = self.read_page_stack(item.blocks_loc)

        for page_ppi in pages:
            page_data = self.get_page(page_ppi)
            if page_data is None:
                continue

            blocks_per_page = 0x1000 // 0x35  # 53 bytes
            i = 0
            while i < blocks_per_page:
                off = i * 0x35
                if off + 0x35 > 0x1000:
                    break
                try:
                    patch_block = SPatchBlockDescriptorV7(page_data, off)
                    if patch_block.size == 0 and patch_block.id == 0:
                        return all_blocks if all_blocks else []
                    if patch_block.size > 0 and patch_block.size <= BLOCK_SIZE:
                        all_blocks.append(patch_block)
                        if len(all_blocks) >= item.nBlocks:
                            return all_blocks
                except:
                    return all_blocks
                i += 1

        return all_blocks

    def get_datastore_block(self, block_id):
        datastore_ppi = self.active_slot.snapshot_desc.objRefs.DataStoreRootPage
        all_blocks = self.read_block_descriptors(datastore_ppi, count=block_id + 1)
        if block_id < len(all_blocks):
            return all_blocks[block_id]
        return None

    def validate_file_blocks(self, item, indent=0):
        try:
            print("  " * indent + "block validation")
            if item.file_type in (SDirItemRec.FT_INCREMENT, SDirItemRec.FT_PATCH):
                # incremental backup blocks
                blocks = self.read_patch_block_descriptors(item)
                if blocks:
                    max_to_show = min(5, len(blocks))
                    print("  " * indent + f"showing {max_to_show} patch blocks")
                    for i in range(max_to_show):
                        pb = blocks[i]
                        pb.assert_valid()
                        print("  " * indent + f"- size: {pb.size} bytes")
                        print("  " * indent + f"  loc_type: {pb.loc_type}")
                        print("  " * indent + f"  digest: {pb.digest_hex()}")
                        print("  " * indent + f"  dsid: {pb.id}")
                        print("  " * indent + f"  block_off: {pb.offset} (x BLOCK_SIZE)")
                        # look up corresponding storage block
                        stg = self.get_datastore_block(pb.id)
                        if stg:
                            stg.assert_valid()
                            print("  " * indent + "  storage:")
                            print("  " * indent + f"    loc: {stg.location_str()} (0x{stg.location:x})")
                            print("  " * indent + f"    off: 0x{stg.offset:x}")
                            print("  " * indent + f"    comp: {stg.comp_type_str()} (0x{stg.comp_type:02x})")
                            print("  " * indent + f"    comp_size: {stg.comp_size} bytes")
                            print("  " * indent + f"    output size: {stg.src_size} bytes")
                            print("  " * indent + f"    digest: {stg.digest_hex()}")
            else:
                # regular file blocks
                blocks = self.read_file_block_descriptors(item)
                if blocks:
                    # filter out sparse blocks for cleaner display
                    non_sparse_blocks = [b for b in blocks if not (hasattr(b, 'is_sparse') and b.is_sparse())]
                    max_to_show = min(5, len(non_sparse_blocks))
                    print("  " * indent + f"showing {max_to_show} file blocks (total: {len(blocks)}, non-sparse: {len(non_sparse_blocks)})")
                    for i in range(max_to_show):
                        fb = non_sparse_blocks[i]
                        fb.assert_valid()
                        print("  " * indent + f"- size: {fb.size} bytes")
                        print("  " * indent + f"  loc_type: {fb.loc_type}")
                        print("  " * indent + f"  digest: {fb.digest_hex()}")
                        print("  " * indent + f"  dsid: {fb.id}")
                        print("  " * indent + f"  flags: 0x{fb.flags:02x}")
                        # look up storage block details
                        stg = self.get_datastore_block(fb.id)
                        if stg:
                            stg.assert_valid()
                            print("  " * indent + "  storage:")
                            print("  " * indent + f"    loc: {stg.location_str()} (0x{stg.location:x})")
                            print("  " * indent + f"    off: 0x{stg.offset:x}")
                            print("  " * indent + f"    comp: {stg.comp_type_str()} (0x{stg.comp_type:02x})")
                            print("  " * indent + f"    comp size: {stg.comp_size} bytes")
                            print("  " * indent + f"    output size: {stg.src_size} bytes")
                            print("  " * indent + f"    digest: {stg.digest_hex()}")
        except Exception as e:
            print("  " * indent + f"error: {e}")

    def read_props_dictionary(self, ppi):
        if not ppi or not ppi.valid():
            return None
        
        try:
            metablob = MetaBlob(self, ppi.bank_id, ppi.page_id)
            data = metablob.get_data()
            if data:
                return CPropsDictionary(data)
        except Exception as e:
            return None
        
        return None

    def dump(self, validate_blocks=False):
        print(f"{self.file_type} {self.filename}")
        print(f"size: {self.file_size} bytes ({self.file_size / (1024*1024):.1f} MB)")
        print()
        
        # File header info
        print("header")
        print(f"  version: 0x{self.header.version:x}")
        print(f"  inited: {self.header.inited}")
        print(f"  digest: {self.header.digest_type}")
        print(f"  slot_fmt: {self.header.slot_fmt}")
        print(f"  BLOCK_SIZE: 0x{self.header.standard_block_size:x} ({self.header.standard_block_size} bytes)")
        print(f"  max_banks: 0x{self.header.max_banks():x}")
        print(f"  slot_size: 0x{self.header.slot_size():x}")
        print()

        # Slot information
        for slot_idx, s in enumerate(self.slots):
            print(f"slot {slot_idx} @ 0x{s.offset:x}")
            print(f"  crc: 0x{s.crc:08x}")
            print(f"  has_snapshot: {s.has_snapshot}")
            print(f"  valid: {s.valid_fast()}")
            print(f"  snap_ver: 0x{s.snapshot_desc.version:x}")
            print(f"  storage_eof: 0x{s.snapshot_desc.storage_eof:x}")
            print(f"  banks: {s.allocated_banks}")

            # Object references
            o = s.snapshot_desc.objRefs
            print(f"  MetaRootDirPage: {o.MetaRootDirPage.bank_id:04x}:{o.MetaRootDirPage.page_id:04x}")
            print(f"  children: {o.children_num}")
            print(f"  DataStoreRootPage: {o.DataStoreRootPage.bank_id:04x}:{o.DataStoreRootPage.page_id:04x}")
            print(f"  blocks: {o.BlocksCount}")

            # Show first few banks
            show = min(5, len(s.bank_infos))
            for i in range(show):
                bi = s.bank_infos[i]
                print(f"    bank {i}: off=0x{bi.offset:x}, size=0x{bi.size:x}, crc=0x{bi.crc:08x}")
            if len(s.bank_infos) > 5:
                print(f"    ... {len(s.bank_infos) - 5} more")
            print()

        print("directory")
        root_ppi = self.active_slot.snapshot_desc.objRefs.MetaRootDirPage
        self.print_directory(root_ppi, indent=0, validate_blocks=validate_blocks)

    def print_directory(self, ppi, indent=0, path="", validate_blocks=False):
        items = self.read_dir_items(ppi)

        for item in items:
            if item.is_dir():
                print("  " * indent + f"[{item.type_name()}] {item.name}/")
                print("  " * indent + f"  loc: {item.children_loc.bank_id:04x}:{item.children_loc.page_id:04x}")
                print("  " * indent + f"  children: {item.children_num}")
                
                # show properties if available
                if item.props_loc and item.props_loc.valid():
                    props = self.read_props_dictionary(item.props_loc)
                    if props and props.valid():
                        print("  " * indent + f"  props:")
                        for key, value in props.properties.items():
                            if isinstance(value, bytes):
                                if len(value) > 50:
                                    print("  " * indent + f"    {key}: [{len(value)} bytes]")
                                else:
                                    print("  " * indent + f"    {key}: {value.hex()}")
                            elif isinstance(value, str) and len(value) > 100:
                                print("  " * indent + f"    {key}: [{len(value)} chars] {value[:50]}...")
                            else:
                                print("  " * indent + f"    {key}: {value}")
                
                # recurse into subdirectory
                if item.children_loc.valid():
                    self.print_directory(item.children_loc, indent + 1, path + item.name + "/", validate_blocks)
            else:
                # file entry - show size and block info
                size_mb = item.fib_size / (1024 * 1024)
                print("  " * indent + f"[{item.type_name()}] {item.name}")
                print("  " * indent + f"  size: {item.fib_size} bytes ({size_mb:.2f} MB)")
                print("  " * indent + f"  blocks: {item.nBlocks}")
                if item.blocks_loc:
                    print("  " * indent + f"  blocks_loc: {item.blocks_loc.bank_id:04x}:{item.blocks_loc.page_id:04x}")
                if item.inc_size > 0:
                    print("  " * indent + f"  inc_size: {item.inc_size} bytes")
                
                # show file properties if available
                if item.props_loc and item.props_loc.valid():
                    props = self.read_props_dictionary(item.props_loc)
                    if props and props.valid():
                        print("  " * indent + f"  props:")
                        for key, value in props.properties.items():
                            if isinstance(value, bytes):
                                if len(value)> 50:
                                    print("  " * indent + f"    {key}: [{len(value)} bytes]")
                                else:
                                    print("  " * indent + f"    {key}: {value.hex()}")
                            elif isinstance(value, str) and len(value) > 100:
                                print("  " * indent + f"    {key}: [{len(value)} chars] {value[:50]}...")
                            else:
                                print("  " * indent + f"    {key}: {value}")
                
                # validate file blocks
                if validate_blocks and item.blocks_loc and item.blocks_loc.valid():
                    self.validate_file_blocks(item, indent + 1)


if __name__ == "__main__": # intended to be used as a library so just the dump info if ran as main
    backup_file = sys.argv[1]
    validate_blocks = "--validate-blocks" in sys.argv
    parser = VBKParser(backup_file)
    parser.dump(validate_blocks=validate_blocks)


