#coding: binary

module Veeam
  class VBK

    class SStgBlockDescriptorV7 < IOStruct.new( "CLQLC H32 CCLL H32",
                                               :type,
                                               :ref_cnt,
                                               :offset,
                                               :alloc_size,
                                               :dedup,
                                               :digest,
                                               :comp_type,
                                               :f23,
                                               :comp_size,
                                               :orig_size,
                                               :keyset_id
                                              )
      include PrettyFormat
    end

    class SMetaTableDescriptor < IOStruct.new("QQQ", :loc, :size, :nBlocks)
      include PrettyFormat
    end

    class SFibBlockDescriptorV7 < IOStruct.new("LC H32 Q C H32",
                                               :size,
                                               :loc_type,
                                               :digest,
                                               :id,      # index of the block in the DataStore
                                               :flags,
                                               :keyset_id
                                              )
      include PrettyFormat
    end

    class SPatchBlockDescriptorV7 < IOStruct.new("LC H32 QQ H32",
                                                 :size,
                                                 :loc_type, # seen values: 0, 1, 3
                                                 :digest,
                                                 :id,     # index of the block in the DataStore
                                                 :offset, # multiply by block_size to get real offset
                                                 :keyset_id
                                                )
      include PrettyFormat
    end

    class SKeySetRec < IOStruct.new("H32 L A512 L9 QQQ",
                                    f0: :uuid,
                                    f10: :algo,
                                    f14: :hint,
                                    f214: :role,
                                    f218: :magic,
                                    f238: :key_blobs_loc,
                                    f240: :restore_recs_loc,
                                    f248: :timestamp
                                   )
      include PrettyFormat
    end

  end
end
