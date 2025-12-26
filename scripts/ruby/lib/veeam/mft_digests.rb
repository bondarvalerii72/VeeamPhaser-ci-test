#!/usr/bin/env ruby
#coding: binary

require 'iostruct'

module Veeam
  class MftDigests
    DIGEST_SIZE = 0x10 # md5

    class Header < IOStruct.new('A8 LL QQ', :magic, :ver, :hdr_size, :bitmap_size, :digests_size)
      MAGIC = 'MftDigs'

      def valid?
        magic == MAGIC && ver == 1 && hdr_size == SIZE
      end
    end

    attr_accessor :header, :bitmap, :digests

    def initialize header, bitmap, digests
      @header = header
      @bitmap = bitmap
      @digests = digests
    end

    def inspect
      sprintf "#<%s header=%s bitmap=[%x bytes] digests=[%x]>", self.class, header.inspect, bitmap.size, digests.size/DIGEST_SIZE
    end

    def each
      return enum_for(:each) unless block_given?

      idx = 0
      digest_seq_idx = 0
      @bitmap.each_byte do |byte|
        8.times do
          if byte & 1 == 1
            yield @digests[digest_seq_idx*DIGEST_SIZE, DIGEST_SIZE], idx
            digest_seq_idx += 1
          end
          byte >>= 1
          idx += 1
        end
      end
    end

    def self.try_read io
      hdr = Header.read(io)
      if hdr.valid?
        bitmap = io.read(hdr.bitmap_size)
        digests = io.read(hdr.digests_size)
        new(hdr, bitmap, digests)
      else
        nil
      end
    end
  end
end

if __FILE__ == $0
  require 'zhexdump'

  verbose = ARGV.delete('-v')

  File.open(ARGV[0], 'rb') do |f|
    digests = Veeam::MftDigests.try_read(f)
    if digests
      unless f.eof?
        puts "[?] #{f.size - f.pos} bytes left unread"
        ZHexdump.dump f.read, indent: 4, width: 32
      end
      puts digests.inspect

      puts "=== Bitmap ==="
      ZHexdump.dump digests.bitmap, indent: 4, width: 32

      if verbose
        puts "=== Digests ==="
        ZHexdump.dump digests.digests, indent: 4, width: 32
      end

      digests.each do |digest, idx|
        printf "%4x: %s\n", idx, digest&.unpack1('H*')
      end
    end
  end
end
