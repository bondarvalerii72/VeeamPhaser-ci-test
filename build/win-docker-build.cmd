@echo off
setlocal enabledelayedexpansion

if "%~1"=="" (
    set args=all
) else (
    set args=%*
)

for %%a in (%args%) do (
    if "%%a"=="a" (
        call :build_image
        call :build_windows
        call :build_linux
        call :build_docs
    ) else if "%%a"=="all" (
        call :build_image
        call :build_windows
        call :build_linux
        call :build_docs
    ) else if "%%a"=="c" (
        call :clean
    ) else if "%%a"=="clean" (
        call :clean
    ) else if "%%a"=="d" (
        call :build_image
        call :build_docs
    ) else if "%%a"=="docs" (
        call :build_image
        call :build_docs
    ) else if "%%a"=="l" (
        call :build_image
        call :build_linux
    ) else if "%%a"=="linux" (
        call :build_image
        call :build_linux
    ) else if "%%a"=="w" (
        call :build_image
        call :build_windows
        call :test
    ) else if "%%a"=="windows" (
        call :build_image
        call :build_windows
        call :test
    ) else if "%%a"=="t" (
        call :test
    ) else if "%%a"=="test" (
        call :test
    ) else (
        echo Usage: %0 [all|clean|docs|linux|test|windows]..
    )
)

endlocal
exit /b

:build_image
rem for some reason 'docker build' may fail to pull the base image, pulling it explicitly always works
docker pull ubuntu:noble
docker build -f build\Dockerfile -t phaser-build .
if %ERRORLEVEL% neq 0 (
    exit %ERRORLEVEL%
)
exit /b

:build_windows
docker run --rm -v "%cd%":/src -w /src phaser-build sh -c "cmake -B build/build-windows -DCMAKE_TOOLCHAIN_FILE=build/cmake/mingw_toolchain.cmake && cmake --build build/build-windows -j4 && cmake --install build/build-windows --prefix dist/windows --component VeeamPhaser"
if %ERRORLEVEL% neq 0 (
    exit %ERRORLEVEL%
)
exit /b

:test
if exist build\build-windows\tests (
    for %%f in (build\build-windows\tests\*_test.exe) do (
        echo.
        rem Extract and echo the basename without the extension
        for %%b in (%%f) do echo %%~nf:

        %%f --gtest_brief=1
        if %ERRORLEVEL% neq 0 (
            %%f
            exit %ERRORLEVEL%
        )
    )
)
exit /b

:build_linux
docker run --rm -v "%cd%":/src -w /src phaser-build sh -c "cmake -B build/build-linux -DCMAKE_TOOLCHAIN_FILE=build/cmake/x64_toolchain.cmake && cmake --build build/build-linux -j4 --target all test && cmake --install build/build-linux --prefix dist/linux"
if %ERRORLEVEL% neq 0 (
    exit %ERRORLEVEL%
)
exit /b

:build_docs
echo [.] Generating documentation with Doxygen...
docker run --rm -v "%cd%":/src -w /src/build phaser-build doxygen Doxyfile
if %ERRORLEVEL% neq 0 (
    exit %ERRORLEVEL%
)
echo [.] Documentation generated in docs/html/index.html
exit /b

:clean
rem Clean build directories and generated files
for %%f in (build\build-windows build\build-linux dist\windows dist\linux build-windows build-linux build-test docs) do (
    if exist %%f (
        echo [.] removing %%f
        rmdir /s /q %%f
    )
)

rem Clean generated files in dist directory
for %%f in (dist\version.h) do (
    if exist %%f (
        echo [.] removing %%f
        del /q %%f
    )
)

rem Clean any remaining dist subdirectories that might contain output files
if exist dist (
    for /d %%d in (dist\*) do (
        if not "%%~nxd"=="build-windows" if not "%%~nxd"=="build-linux" if not "%%~nxd"=="windows" if not "%%~nxd"=="linux" (
            echo [.] removing dist\%%~nxd
            rmdir /s /q "dist\%%~nxd"
        )
    )
)
exit /b
