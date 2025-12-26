#!/usr/bin/env ruby
#coding: binary

require 'iostruct'
require 'zhexdump'
require 'stringio'
require 'awesome_print'
require 'optparse'

require_relative 'lib/veeam/vbk'

include Veeam

class String
  def indent(n)
    (" "*n) + self
  end
end

TABSIZE = 4

def show_item item, add_offset: 0, level: 0
  color = item.respond_to?(:valid?) ? (item.valid? ? :green : :red) : :to_s
  s = sprintf "%08x: %s", add_offset + item.__offset.to_i, item.inspect.send(color)
  puts s.indent(level*TABSIZE)
end

def print_nrep(nrep)
  printf(" [repeated %x times]".blue, nrep) if nrep > 1
  puts
end

def show_vec vec, level: 0
  prev = nil
  nrep = 1
  vec.each do |item|
    if item == prev
      nrep += 1
      next
    end

    if prev
      print_nrep(nrep)
      yield prev if block_given?
    end
    if @options[:show_offsets]
      printf("%08x: %s".indent(level*TABSIZE), item.__offset.to_i, item.inspect)
    else
      printf("%s".indent(level*TABSIZE), item.inspect)
    end
    
    prev = item
    nrep = 1
  end

  print_nrep(nrep)
  yield prev if block_given?
end

def show_bank bank_id
  bank = @slot.banks[bank_id]
  if @options[:verbosity] > 0
    printf("    Bank header:\n")
    ZHexdump.dump(bank.header_page.data, indent: 10, width: 32)
  end
  bank.pages.each_with_index do |page, page_id|
    next unless page

    show_page bank_id, page_id
  end
end

def show_dir_item item, add_offset: 0
  show_item(item, add_offset:)
  if @options[:verbosity] > 0
    if item.respond_to?(:props_loc)
      props_loc = VBK::PhysPageId.from_int64(item.props_loc)
      show_props(props_loc.bank_id, props_loc.page_id, indent: 4, show_title: false) if props_loc.valid?
    end

    blocks_loc = item.respond_to?(:blocks_loc) ? VBK::PhysPageId.from_int64(item.blocks_loc) : nil
    if blocks_loc&.valid?
      puts "=== Blocks ==="
      case item.type
      when VBK::FT_IntFib
        metaVec = VBK::MetaVec2.new(blocks_loc, slot: @slot, klass: VBK::SMetaTableDescriptor, size: item.nBlocks)
        show_vec(metaVec, level: 1) do |item|
          loc = VBK::PhysPageId.from_int64(item.loc)
          if item.nBlocks != 0 && loc.valid?
            show_vec VBK::MetaVec2.new(loc, slot: @slot, klass: VBK::SFibBlockDescriptorV7, size: item.nBlocks), level: 2
          end
        end
      when VBK::FT_Increment
        show_vec VBK::MetaVec2.new(blocks_loc, slot: @slot, klass: VBK::SPatchBlockDescriptorV7, size: item.nBlocks), level: 1
      end
      puts
    end
  end
rescue => e
  puts "[!] #{e}".red
end

def show_dir bank_id, page_id, size = nil
  printf "=== Dir @ %04x:%04x ===\n", bank_id, page_id
  page = @slot.banks[bank_id].pages[page_id]
  sio = StringIO.new(page.data)
  item = VBK::SDirItemRec.read(sio)
  if item.valid?
    loop do
      break if item.type == 0
      show_dir_item item, add_offset: page.offset
      item = VBK::SDirItemRec.read(sio)
    end
  else
    metaVec = VBK::MetaVec2.new(bank_id: bank_id, page_id: page_id, slot: @slot, klass: VBK::SDirItemRec, size:)
    metaVec.each do |item|
      break if item.type == 0
      show_dir_item item
    end
  end
rescue => e
  puts "[!] #{e}".red
end

def show_blocks bank_id, page_id, size = nil
  printf "=== Blocks @ %04x:%04x ===\n", bank_id, page_id
  metaVec = VBK::MetaVec2.new(bank_id: bank_id, page_id: page_id, slot: @slot, klass: VBK::SStgBlockDescriptorV7, size:)
  show_vec(metaVec, level: 1)
end

def show_keys bank_id, page_id, size = nil
  printf "=== Keys @ %04x:%04x ===\n", bank_id, page_id
  metaVec = VBK::MetaVec2.new(bank_id: bank_id, page_id: page_id, slot: @slot, klass: VBK::SKeySetRec, size:)
  show_vec(metaVec, level: 1)
end

def show_page bank_id, page_id
  puts @slot.banks[bank_id].pages[page_id].data
    .to_hexdump(indent: 10, width: 32)
    .sub(/\A {9}/, "%04x:%04x" % [bank_id, page_id])
    .rstrip
    .sub(/00001000:\z/, "")
    .rstrip
    .sub(/\*\z/, "")
    .rstrip
end

def show_all_pages
  @slot.banks.each_with_index do |bank, bank_id|
    bank.pages.each_with_index do |page, page_id|
      next unless page

      show_page bank_id, page_id
      puts
    end
  end
end

def show_blob bank_id, page_id, indent: 0, show_title: true
  metablob = VBK::MetaBlob.new bank_id:, page_id:, slot: @slot
  data = metablob.data
  printf("=== MetaBlob: 0x%x pages, 0x%x bytes\n", metablob.npages, metablob.size) if show_title
  Zhexdump.dump(data, width: 32, indent: )
end

def show_props bank_id, page_id, indent: 0, show_title: true
  metablob = VBK::MetaBlob.new bank_id:, page_id:, slot: @slot
  data = metablob.data
  if @options[:verbosity] > 2
    printf "=== MetaBlob: 0x%x pages, 0x%x bytes\n", metablob.npages, metablob.size
    Zhexdump.dump(data, width: 32)
  end
  printf("=== CPropsDictionary @ %04x:%04x ===\n", bank_id, page_id) if show_title
  dict = VBK::CPropsDictionary.read(StringIO.new(data))
  puts dict.inspect.indent(indent)
  if @options[:verbosity] > 1 && dict.valid? && dict['DefinedBlocksMask']
    mask = VBK::CDefinedBlocksMask.new(dict['DefinedBlocksMask'])
    puts mask.inspect.indent(indent+TABSIZE)
    ZHexdump.dump mask.data, indent: indent+TABSIZE, width: 32
  end
end

def show_stack bank_id, page_id
  stack = VBK::PageStack.new(bank_id:, page_id:, slot: @slot)
  ZHexdump.dump(stack.data, width: 32)
  p stack.to_a
end

def process_file fname
  puts fname.green
  File.open(ARGV[0], 'rb') do |f|
    @vbk = VBK.new(f)
    show_item @vbk.header
    @vbk.slots.each_with_index do |slot, slot_id|
      next if @options[:slot] && slot_id != @options[:slot]

      printf "\n=== Slot %d ===\n", slot_id
      @slot = slot
      slot.headers.each do |item|
        show_item item
        @headers[item.class] = item
      end

      if @actions.empty?
        if hdr=@headers[VBK::Slot::ObjRefs]
          @actions << [:show_dir, *int64_to_ppi(hdr.MetaRootDirPage), hdr.children_num]
        end
      end

      @actions.each do |a|
        puts
        send(*a)
      end
    end
  end
end

@headers = {}
@options = {
  bank: {},
  verbosity: 0,
}
@actions = []

op = OptionParser.new do |opts|
  opts.banner = "Usage: dissect_vbk.rb [options] file1 [file2 ...]"
  opts.on('-v', '--verbose', 'Increase verbosity') do
    @options[:verbosity] += 1
  end

  opts.on('-O', '--show-offsets', 'Show offsets') do
    @options[:show_offsets] = true
  end

  opts.on('--section SECTION', 'Section to dump') do |v|
    @options[:section] = v.to_i
  end
  opts.on('--slot SLOT', 'Slot to dump') do |v|
    @options[:slot] = v.to_i
  end
  opts.on('--bank BANK', 'Bank to dump') do |v|
    @actions << [:show_bank, v.to_i(0)]
  end
  opts.on('-p', '--page BANK:PAGE', 'Page to dump (can be used multiple times)') do |v|
    bank_id, page_id = v.split(':').map{ |x| x.to_i(16) }
    @actions << [:show_page, bank_id, page_id]
  end
  opts.on('-a', '--all-pages', 'Dump all pages)') do |v|
    @actions << [:show_all_pages]
  end
  opts.on('-d', '--dir BANK:PAGE', 'Directory to dump') do |v|
    bank_id, page_id = v.split(':').map{ |x| x.to_i(16) }
    @actions << [:show_dir, bank_id, page_id]
  end
  opts.on('--props BANK:PAGE', 'Properties to dump') do |v|
    bank_id, page_id = v.split(':').map{ |x| x.to_i(16) }
    @actions << [:show_props, bank_id, page_id]
  end
  opts.on('--stack BANK:PAGE', 'PageStack to dump') do |v|
    bank_id, page_id = v.split(':').map{ |x| x.to_i(16) }
    @actions << [:show_stack, bank_id, page_id]
  end
  opts.on('--blob BANK:PAGE', 'MetaBlob to dump') do |v|
    bank_id, page_id = v.split(':').map{ |x| x.to_i(16) }
    @actions << [:show_blob, bank_id, page_id]
  end
  opts.on('--blocks BANK:PAGE', 'Blocks to dump') do |v|
    bank_id, page_id = v.split(':').map{ |x| x.to_i(16) }
    @actions << [:show_blocks, bank_id, page_id]
  end
  opts.on('-k', '--keys BANK:PAGE', 'Keys to dump') do |v|
    bank_id, page_id = v.split(':').map{ |x| x.to_i(16) }
    @actions << [:show_keys, bank_id, page_id]
  end
end
op.parse!

ARGV.each do |fname|
  process_file(fname)
end
