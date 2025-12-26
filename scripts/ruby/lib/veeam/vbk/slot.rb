#coding: binary

module Veeam
  class VBK

    class Slot
      attr_accessor :io, :offset, :banks, :header, :obj_refs, :banks_header, :snapshot_descriptor, :bank_infos

      def initialize io:, size: nil
        @io = io
        @offset = io.tell
        @header = Header.read(io)
        @snapshot_descriptor = SnapshotDescriptor.read(io)
        @obj_refs = ObjRefs.read(io)
        @banks_header = BanksHeader.read(io)
        @bank_infos = @snapshot_descriptor.nBanks.times.map { BankInfo.read(io) }
        @banks = @bank_infos.map.with_index { |info, id| Bank.new(id:, io:, info:) }
        io.seek(@offset + self.class.slot_size(@banks_header.max_banks))
      end

      def [](ppi)
        @banks[ppi.bank_id].pages[ppi.page_id]
      end

      def headers
        [ @header, @snapshot_descriptor, @obj_refs, @banks_header ] + @bank_infos
      end

      def self.slot_size max_banks
        raise "invalid Bank::SIZE" if Slot::BankInfo::SIZE != 0x10
        # expression literally copied from reversed code
        ((((max_banks * Slot::BankInfo::SIZE) & 0xFFFFFFF0) + 120) & 0xFFFFF000) + PAGE_SIZE
      end

      class Header < IOStruct.new("LL", :crc, :has_snapshot)
      end

      class ObjRefs < IOStruct.new("Q10",
                                   :MetaRootDirPage,
                                   :children_num,
                                   :DataStoreRootPage,
                                   :blocks_count,
                                   :free_blocks_root,
                                   :dedup_root,
                                   :f30,
                                   :f38,
                                   :CryptoStoreRootPage,
                                   :ArchiveBlobStorePage,
                                  )
        include PrettyFormat
      end

      SnapshotDescriptor = IOStruct.new("QQL", :version, :stg_eof, :nBanks)

      class BanksHeader < IOStruct.new("QLL", :unused, :max_banks, :allocated_banks)
        def valid?
          max_banks <= MAX_BANKS && allocated_banks <= max_banks
        end
      end

      class BankInfo < IOStruct.new("LQL", :crc, :offset, :size)
        attr_accessor :io_size

        def inspect
          "<BankInfo crc=%08x offset=%12x size=%6x>" % [crc, offset, size]
        end

        def self.read io
          super.tap do |x|
            x.io_size = io.size
          end
        end

        def valid?
          crc != 0 &&
            offset > __offset && offset < io_size &&
            size % PAGE_SIZE == 0 && size >= Bank::V13_MIN_PAGES*PAGE_SIZE && size <= Bank::V13_MAX_PAGES*PAGE_SIZE
        end
      end

      def self.read(io)
        hdr = Header.read(io)
        sd = SnapshotDescriptor.read(io)
        [ hdr, sd, ObjRefs.read(io), BanksHeader.read(io) ] + sd.nBanks.times.map { BankInfo.read(io) }
      end
    end

  end
end
