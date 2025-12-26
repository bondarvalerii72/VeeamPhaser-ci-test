#coding: binary

module Veeam
  class VBK
    class Page
      attr_accessor :offset, :io

      def initialize io:, offset:
        @io = io
        @offset = offset
      end

      def data
        @data ||= @io.pread(PAGE_SIZE, @offset)
      end
    end
  end
end
