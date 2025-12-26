#coding: binary

module Veeam
  class VBK

    class Bank
      V13_MIN_PAGES = 0x20
      V13_MAX_PAGES = 0x400

      attr_accessor :id, :info, :io

      def initialize id:, io:, info:
        @id = id
        @io = io
        @info = info
      end

      def header_page
        return @header_page if @header_page

        load
        @header_page
      end

      def pages
        return @pages if @pages

        load
        @pages
      end

      def offset
        @info.offset
      end

      def load
        @pages = []
        @header_page = Page.new(io: @io, offset: @info.offset)
        
        nPages = @header_page.data[0,2].unpack1('v')
        if nPages < V13_MIN_PAGES || nPages > V13_MAX_PAGES
          $stderr.printf "[!] Bank @ %08x: invalid nPages %x\n", offset, nPages
          return
        end

        @io.pread(nPages, offset+4).bytes.each_with_index do |b, page_idx|
          case b
          when 0
            # occupied page
            @pages[page_idx] = Page.new(io:, offset: offset + (page_idx+1) * PAGE_SIZE)
          when 1
            # free page
          else
            $stderr.printf "[!] Bank @ %08x: page %04x : invalid page flag %02x\n", offset, page_idx, b
          end
        end
      end

      # .text:00EA14E0
      def self.serialized_size ver, nPages
        if ( ver == 13 || ver == 0x10008 )
          size = nPages << 12
        else
          size = 0x400000
        end

        if ( ver == 11 || ver == 12 || ver == 13 || ver == 0x10008 )
          return size + 0x2000   # why 0x2000 and not 0x1000 ??
        else
          return size + 0x101000
        end
      end

      # .text:00EA1560
      def self.max_payload_size ver, nPages
        serialized_size(ver, nPages) - 0x1000
      end
    end

  end
end
