#coding: binary

module Veeam
  class VBK

    # referred as "CMetaBlobRW", "metablob" or "meta-blob" in veeam code
    # stored as a sequence of pages, each page has prefix of 0xc bytes (3 dwords)
    # first two dwords are PhysPageId of next page, or -1:-1 if last page
    # third dword only set in first page, and is a size of the blob in bytes.
    # max pages count is 7999, so max blob size is 7999*(0x1000-0xc) = 32_667_916 bytes = 31.15 MB
    # used in:
    #  - SDirItemRec properties (CPropsDictionary)
    #  - CryptoStore keys/repair records
    #  - IntFib "undir info" (CFibSrcRestoreInfo)
    #  - ExtFib "undir info" (CExtFibIdentity)
    class MetaBlob
      MAX_PAGES = 7999
      PAGE_PAYLOAD_SIZE = PAGE_SIZE-0xc
      MAX_PAYLOAD_SIZE = MAX_PAGES*PAGE_PAYLOAD_SIZE

      attr_accessor :bank_id, :page_id, :size, :npages, :data

      class Header < IOStruct.new("llL", :page_id, :bank_id, :size)
        def valid?(first:)
          (!first || size < MAX_PAYLOAD_SIZE) &&
            ((page_id == -1 && bank_id == -1) ||
             (page_id >= 0 && page_id < MAX_PAGE_ID && bank_id >= 0 && bank_id < MAX_BANKS)
            )
        end
      end

      def initialize bank_id:, page_id:, slot:
        @bank_id = bank_id
        @page_id = page_id
        @slot = slot
      end

      def data
        @data ||= _read_data
      end

      def _read_data
        pagedata = @slot.banks[@bank_id].pages[@page_id].data
        hdr = Header.read(pagedata)
        return nil unless hdr.valid?(first: true)

        @size = hdr.size

        # prealloc buffer
        data = "\x00" * hdr.size

        # logic copied 1:1 from decompiled code .text:00E9F500 CMetaBlobRW::read_data
        @size = hdr.size
        @npages = @size / PAGE_PAYLOAD_SIZE + 1
        @npages = @size / PAGE_PAYLOAD_SIZE if @size % PAGE_PAYLOAD_SIZE == 0
        @npages = 1 if @npages == 0
        # check blob size/npages here if blob reference has this info
        pos = 0
        @npages.times do |i|
          pagedata = @slot.banks[@bank_id].pages[@page_id].data
          hdr = Header.read(pagedata)
          return false unless hdr.valid?(first: pos==0) # extra check, not present in original code

          chunk_size = PAGE_PAYLOAD_SIZE
          if pos + PAGE_PAYLOAD_SIZE > @size
            chunk_size = @size - pos
            break if @size == pos
          end

          data[pos, chunk_size] = pagedata[Header::SIZE, chunk_size]
          pos += chunk_size
          break if hdr.page_id == -1 || hdr.bank_id == -1 # extra check, not present in original code
        end
        data
      end
    end

  end
end
