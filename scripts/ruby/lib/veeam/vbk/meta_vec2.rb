#coding: binary

module Veeam
  class VBK

    # original C++ name: stglib::tstg::CMetaVec2<T>
    # used if repo version is 12+, CMetaVec is used otherwise
    # known T classes are:                            size
    #  - stglib::tstg::SArchiveBlobDescriptorRaw<32>    dc
    #  - stglib::tstg::SArchiveBlobDescriptorRaw<64>   15c
    #  - stglib::tstg::SArchiveBlobDescriptorRaw<128>  25c
    #  - stglib::tstg::SArchiveBlobDescriptorRaw<256>  45c
    #  - stglib::tstg::SArchiveBlobDescriptorRaw<512>  85c
    #  - stglib::tstg::SDirItemRec                      c0
    #  - stglib::tstg::SFibBlockDescriptor              1e
    #  - stglib::tstg::SFibBlockDescriptorV7            2e
    #  - stglib::tstg::SKeySetRec                      250
    #  - stglib::tstg::SMetaTableDescriptor             18
    #  - stglib::tstg::SPatchBlockDescriptor            25
    #  - stglib::tstg::SPatchBlockDescriptorV7          35
    #  - stglib::tstg::SStgBlockDescriptor              2c
    #  - stglib::tstg::SStgBlockDescriptorV7            3c
    class MetaVec2
      MAX_PAGES = 520000

      attr_accessor :klass, :slot

      def initialize loc=nil, page_id: nil, bank_id: nil, size: nil, klass:, slot:
        raise ArgumentError, "Either loc or page_id and bank_id must be provided" if (loc && (page_id && bank_id)) || (loc.nil? && (page_id.nil? || bank_id.nil?))
        page_id ||= loc.page_id
        bank_id ||= loc.bank_id
        @size = size
        @klass = klass
        @slot = slot
        @page_stack = PageStack.new(bank_id:, page_id:, slot:)
      end

      def each
        return enum_for(:each) unless block_given?

        idx = 0
        @page_stack.to_a.each do |ppi|
          page = slot[ppi]
          sio = StringIO.new(page.data)
          (PAGE_SIZE / klass.size).times do
            record = klass.read(sio)
            record.__offset += page.offset
            yield record
            idx += 1
            break if @size && idx >= @size
          end
        end
      end
    end
  end
end
