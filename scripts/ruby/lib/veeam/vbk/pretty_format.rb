#coding: binary

module Veeam::VBK::PrettyFormat
  ZERO_KEYSET_ID = "00000000000000000000000000000000"
  NULL_PPI = 0xffff_ffff_ffff_ffff

  def to_s
    "<#{self.class.to_s}" + to_h.map do |k, v|
      case v
      when NULL_PPI, ZERO_KEYSET_ID
        ""
      when Integer
        if k == :loc_type || k == :id || k =~ /idx$/
          " #{k}=%x" % v
        elsif k =~ /page|root|_loc$|^loc$/i
          " #{k}=%04x:%04x" % [v >> 32, v & 0xffff_ffff]
        elsif v == 0
          ""
        elsif v <= 9
          " #{k}=%x" % v
        else
          " #{k}=0x%x" % v
        end
      else
        " #{k}=#{v.inspect}"
      end
    end.join + ">"
  end
end
