#!/usr/bin/env ruby
#coding: binary

require 'zlib'

module Veeam
  class VBK

    # keeping original veeam names for classes and also exception texts (besides the "CDefinedBlocksMask: " prefix)
    class CDefinedBlocksMask
      attr_accessor :data, :header

      # .text:00820B90
      Header = IOStruct.new("QLQ", :fib_size, :block_size, :nBlocks)

      def initialize data
        raw_size, comp_size = data.unpack('QQ')
        raise "CDefinedBlocksMask: Compressed data size is not valid." if comp_size != data.size - 8*2

        raw = Zlib.inflate(data[16..-1])
        raise "CDefinedBlocksMask: Decompressed data size is not valid." if raw.size != raw_size

        @header = Header.read(raw)
        @data = raw[Header::SIZE..-1]
      end

      def inspect
        sprintf("<CDefinedBlocksMask %s data=[%x bytes]>", header.inspect, data.size)
      end
    end

    # original C++ name: cnt::CPropsDictionary
    class CPropsDictionary < Hash
      PROPTYPE_INT    = 1
      PROPTYPE_UINT64 = 2
      PROPTYPE_MBS    = 3 # MultiByteString
      PROPTYPE_WCS    = 4 # WideCharString
      PROPTYPE_BIN    = 5
      PROPTYPE_BOOL   = 6

      def valid?
        (keys + values).compact.size > 1
      end

      def inspect
        "<CPropsDictionary " + map do |k, v|
          if v.is_a?(String) && v =~ /[^\x20-\x7e]/
            "#{k}=[#{v.size} bytes]"
          else
            "#{k}=#{v.inspect}"
          end
        end.join(", ") + ">"
      end

      def self.read(io)
        h = new
        loop do
          type = io.read(4)&.unpack1('l')
          break if type == -1 # proper end of dictionary
          break if type.nil? || type < 1 || type > 6

          key = read_string(io, max_len: 0x100)
          break if key.nil? || key =~ /[^\x20-\x7e]/ || key == ''

          case type
          when PROPTYPE_INT
            h[key] = io.read(4).unpack1('l')
          when PROPTYPE_UINT64
            h[key] = io.read(8).unpack1('Q')
          when PROPTYPE_BOOL
            h[key] = io.read(4).unpack1('l') != 0
          when PROPTYPE_MBS, PROPTYPE_WCS, PROPTYPE_BIN
            value = read_string(io) # TODO: proper encoding
            h[key] = value
            break if value.nil?
          else
            #raise "Unknown type #{type} for key #{key.inspect}"
            break
          end
        end
        h
      end

      def self.read_string(io, max_len: 0x100000) # pulled default max_len out of thin air
        len = io.read(4).unpack1('L')
        return nil if len > max_len

        io.read(len)
      end
    end

  end
end

if __FILE__ == $0
  File.open(ARGV[0], 'rb') do |f|
    f.seek(ARGV[1].to_i(16)) if ARGV[1]
    x = Veeam::VBK::CPropsDictionary.read(f)
    puts x.inspect
    if x['DefinedBlocksMask']
      require 'zhexdump'
      ZHexdump.dump(x['DefinedBlocksMask'])
      raw = Veeam::CDefinedBlocksMask.deserialize(x['DefinedBlocksMask'])
      puts "raw:"
      Zhexdump.dump(raw)
    end
  end
end
