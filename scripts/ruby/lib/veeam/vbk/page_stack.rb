#coding: binary

module Veeam
  class VBK

    # stglib::tstg::CPageStack
    class PageStack
      attr_accessor :bank_id, :page_id, :slot

      def initialize bank_id:, page_id:, slot:
        @bank_id = bank_id
        @page_id = page_id
        @data = nil
        @slot = slot
      end

      def to_a
        a = []
        data.unpack('q*').each_with_index do |x, idx|
          next if idx % 512 == 0 # skip page pointers
          next if idx == 1       # skip pointer to self
          next if x == -1

          a << PhysPageId.from_int64(x)
        end
        a
      end

      def data
        @data ||= read_pages
      end

      def read_pages
        page_tables = ''
        bank_id = @bank_id
        page_id = @page_id
        loop do
          break if bank_id == -1 || page_id == -1

          pagedata = slot.banks[bank_id].pages[page_id].data
          if page_tables.empty?
            page_id, bank_id = pagedata[8,8].unpack('L2')
            msg = "PageStack first record (%04x:%04x) should point to itself (%04x:%04x)" % [page_id, bank_id, @page_id, @bank_id]
            raise msg if page_id != @page_id || bank_id != @bank_id
          end
          page_tables << pagedata
          #@index.append(pagedata[8..-1].unpack('q*'))
          page_id, bank_id = pagedata.unpack('l2')
        end
        page_tables
      end

      # original logic from CPageStack::get_page_ppi, identical for all T classes
      def get_page_ppi(page_idx)
        reqTableNum = 1
        reqTableNum *= 4 while page_idx+1 > 510*reqTableNum

        table_idx, table_ofs = (reqTableNum+page_idx).divmod(511)
        offset = PAGE_SIZE*table_idx + table_ofs*8 + 8
        data[offset, 8].unpack('l2')
      end
    end

  end
end
