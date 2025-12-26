#coding: binary

module Veeam
  class VBK

    FT_SubFolder = 1
    FT_ExtFib    = 2
    FT_IntFib    = 3
    FT_Patch     = 4
    FT_Increment = 5

    # original Veeam struct name is SDirItemRec
    class SDirItemRec < IOStruct.new("LL A128 QL SCC QQQQQ",
                                     :type,
                                     :name_len, :name,
                                     :props_loc, # extended properties location, serialized CPropsDictionary
                                     :f90,       # always 0
                                     :update_in_progress,
                                     :f96,
                                     :flags,
                                     :blocks_loc, # MetaVec of SMetaTableDescriptor
                                     :nBlocks,
                                     :fib_size,
                                     :undir_loc,
                                     :fb8
                                    )
      include PrettyFormat

      def self.read io
        x = super
        x.name = x.name[0, x.name_len.to_i] # cut garbage bytes
        case x.type
        when FT_SubFolder
          SFolderRec.read(x.pack).tap do |y|
            y.__offset = x.__offset
          end
        when FT_Increment
          SIncrementRec.read(x.pack).tap do |y|
            y.__offset = x.__offset
          end
        else
          x
        end
      end

      def valid?
        type && type > 0 && type < 6 &&
          name_len && name_len > 0 && name_len < 128 &&
          name && name !~ /[\x00-\x1f]/ &&
          fib_size > 0 && nBlocks > 0 && nBlocks <= fib_size &&
          blocks_loc != 0
      end
    end

    class SIncrementRec < IOStruct.new("LL A128 QL SCC QQQQQ",
                                     :type,
                                     :name_len, :name,
                                     :props_loc,
                                     :f90,
                                     :update_in_progress,
                                     :f96,
                                     :flags,
                                     :blocks_loc, # MetaVec of SPatchBlockDescriptorV7
                                     :nBlocks,
                                     :fib_size,
                                     :inc_size, # the only difference?
                                     :fb8
                                    )
      include PrettyFormat

      def valid?
        type == FT_Increment &&
          name_len && name_len > 0 && name_len < 128 &&
          name && name !~ /[\x00-\x1f]/ &&
          fib_size > 0 && nBlocks >= 0 && nBlocks <= fib_size && # zero blocks are OK when VIB entry has no changes from the base
          blocks_loc != 0
      end
    end

    class SFolderRec < IOStruct.new("LL A128 QL QQ",
                                   :type,
                                   :name_len, :name,
                                   :props_loc,
                                   :f90,
                                   :children_loc,
                                   :children_num,
                                  )
      include PrettyFormat

      def valid?
        type == FT_SubFolder &&
          name_len && name_len > 0 && name_len < 128 &&
          name && name !~ /[\x00-\x1f]/ &&
          children_loc != 0 && children_num > 0
      end
    end

  end
end
