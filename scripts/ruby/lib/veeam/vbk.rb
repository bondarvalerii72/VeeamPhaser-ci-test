#coding: binary

module Veeam
  class VBK
    # values hardcoded in veeam
    PAGE_SIZE = 0x1000        
    MAX_SLOTS = 2
    MAX_BANKS = 0xffa0 # per slot
    MAX_PAGE_ID = 0xffffff # not sure

    attr_accessor :sections, :io, :header, :slots

    def initialize io
      @io = io
      @header = FileHeader.read(io.read(PAGE_SIZE))
      @slots = MAX_SLOTS.times.map { Slot.new(io:) }
    end

    class PhysPageId < IOStruct.new('ll', :page_id, :bank_id)
      def valid?
        page_id >= 0 && page_id < MAX_PAGE_ID &&
          bank_id >= 0 && bank_id < MAX_BANKS
      end

      def inspect
        "%04x:%04x" % [bank_id, page_id]
      end

      def self.from_int64(i)
        read([i].pack('q'))
      end
    end

  end

  def int64_to_ppi(i)
    [ i >> 32, i & 0xffff_ffff ]
  end
end

require_relative 'vbk/pretty_format'
require_relative 'vbk/blocks'
require_relative 'vbk/file_header'
require_relative 'vbk/slot'
require_relative 'vbk/bank'
require_relative 'vbk/page'
require_relative 'vbk/page_stack'
require_relative 'vbk/dir_items'
require_relative 'vbk/meta_blob'
require_relative 'vbk/meta_vec2'
require_relative 'vbk/props_dictionary'
