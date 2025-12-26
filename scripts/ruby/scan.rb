#!/usr/bin/env ruby
#coding: binary

require 'iostruct'
require 'zhexdump'
require 'stringio'
require 'awesome_print'

require_relative 'lib/veeam/vbk'

include Veeam

def show_item item, add_offset: 0
  offset = add_offset
  offset += item.__offset.to_i if item.respond_to?(:__offset)
  printf "%08x: %s\n", offset, item.inspect
end

TYPES = {
  VBK::FileHeader       => 0,
  VBK::SDirItemRec      => 0,
  VBK::CPropsDictionary => 0xc,
  "DefinedBlocksMask"   => nil,
  "\x00\x00\x00\x2e\xca\x10\xa1" => "SKeySetRec magic",
}

def try_read_items sio, klass, pos
  was = false
  loop do
    item = klass.read(sio)
    if item.valid?
      was = true
      show_item item, add_offset: pos
    else
      break
    end
  end
  was
end

def process_file fname
  data = "\x00" * VBK::PAGE_SIZE
  puts fname.green
  File.open(fname, 'rb') do |f|
    while !f.eof?
      pos = f.tell
      f.read(VBK::PAGE_SIZE, data)
      
      sio = StringIO.new(data)
      TYPES.each do |k,v|
        if k.is_a?(String)
          if data.include?(k)
            show_item(v||k, add_offset: pos)
          end
        else
          sio.seek(v)
          break if try_read_items(sio, k, pos)
        end
      end
    end
  end
end

ARGV.each do |fname|
  process_file(fname)
end
