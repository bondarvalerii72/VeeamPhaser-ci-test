# reduce exe size
#include(CheckIPOSupported)
#check_ipo_supported()

#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto -ffunction-sections -fdata-sections")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -flto -ffunction-sections -fdata-sections")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto -Wl,--gc-sections")
#
## For static libraries built with LTO
#set(CMAKE_AR x86_64-w64-mingw32-gcc-ar)
#set(CMAKE_RANLIB x86_64-w64-mingw32-gcc-ranlib)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-attributes")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes")
