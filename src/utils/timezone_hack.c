// compiling windows binary with mingw 12.0.0_1 and pre-built openssl 3.3.2 from https://github.com/viaduck/openssl-cmake
// fails with the following error:
//   libcrypto.a(libcrypto-lib-a_time.obj):a_time.c:(.text+0x1d23): undefined reference to `__imp__timezone'
// adding any of the *.a libs from mingw-w64/toolchain-x86_64/x86_64-w64-mingw32 either does not help,
// or (worst case) makes any call to lseek()/lseek64() silently terminate the app.
// so, this hack just adds the stub __imp__timezone() function to the binary, so it can be linked successfully.
// only static build of VeeamPhaser.exe is affected, the dynamic one works fine without the hack.

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

char * __imp__timezone(int zone, int dst){
    return (char*)0;
}

#pragma GCC diagnostic pop
