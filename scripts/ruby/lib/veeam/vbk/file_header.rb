#coding: binary

module Veeam
  class VBK
    MAX_DIGEST_TYPE_LEN = 250
    MAX_SLOT_FMT = 9 # as of Veeam v12.2.0.334

    MIN_BLOCK_SIZE = 512       # for sure
    MAX_BLOCK_SIZE = 0x6400000 # not sure

    class FileHeader < IOStruct.new( "LLLA#{MAX_DIGEST_TYPE_LEN+1} LLC",
                                    :version, :inited, :digest_type_len, :digest_type,
                                    :slot_fmt,
                                    :std_block_size, # power of two?
                                    :cluster_align)  # 'algorithm' or 'logarithm' ?
      # also may have external_storage_id (UUID) @ 0x120

      def slot_size
        raise "invalid Bank::SIZE" if Slot::BankInfo::SIZE != 0x10
        # expression literally copied from reversed code
        ((((max_banks * Slot::BankInfo::SIZE) & 0xFFFFFFF0) + 120) & 0xFFFFF000) + PAGE_SIZE
      end

      def max_banks
        case slot_fmt
        when 0
          0xf8
        when 5, 9
          0x7f00
        else
          $stderr.printf "[?] Unknown slot_fmt: %02x\n", slot_fmt
          nil
        end
      end

      def self.read io
        super.tap do |r|
          r.digest_type = r.digest_type[0, r.digest_type_len.to_i] # cut garbage bytes
        end
      end

      def valid?
        # slot_fmt can be zero
        inited != 0 && version != 0 &&
          digest_type_len == digest_type.length && digest_type_len <= MAX_DIGEST_TYPE_LEN &&
          digest_type && digest_type =~ /\A[\x20-\x7e]+\z/ &&
          std_block_size != 0 && std_block_size % 512 == 0 &&
          cluster_align != 0 &&
          slot_fmt <= MAX_SLOT_FMT
      end
    end

  end
end
