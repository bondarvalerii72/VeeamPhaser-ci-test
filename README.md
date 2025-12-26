# VeeamPhaser

A C++20 tool for processing and analyzing Veeam backup files (.vbk, .vib, .bank).

## Overview

VeeamPhaser is a rewrite of VeeamBlaster in modern C++20. It provides a command-line interface for working with Veeam backup formats, including:

- VBK/VIB file processing and extraction
- Metadata analysis and display
- Block extraction and validation
- File carving from corrupted backups
- Repository indexing and repair
- Data recovery operations

## Requirements

- C++20 compiler (GCC 10+, Clang 12+, MSVC 19.29+)
- CMake 3.14+
- Docker (recommended for builds)

Dependencies (auto-fetched by CMake): OpenSSL, zlib, zstd, lz4, spdlog, argparse, nlohmann_json, LMDB, Google Test

## Building

### Docker (Recommended)

```bash
# Linux/macOS
build/docker-build.sh all     # Build everything
build/docker-build.sh linux   # Linux binaries only
build/docker-build.sh windows # Windows binaries only
build/docker-build.sh docs    # Generate documentation
build/docker-build.sh clean   # Clean artifacts

# Windows
build\win-docker-build.cmd all
```

Output: `dist/build-linux/` and `dist/build-windows/`

### Native

```bash
# Linux/macOS
cmake -B build/build-linux && cmake --build build/build-linux -j4
# or
./build/docker-build.sh linux



# run tests
cd build/build-linux && ctest --output-on-failure

# cross-compile for Windows from Linux/macOS
cmake -B build/build-windows -DCMAKE_TOOLCHAIN_FILE=build/cmake/mingw_toolchain.cmake && cmake --build build/build-windows -j$(nproc)
#or 
./build/win-docker-build.cmd

```

## Documentation

Generate with Doxygen (included in Docker builds):

```bash
build/docker-build.sh docs  # or cd build && doxygen Doxyfile
```

Open `docs/html/index.html` for API reference, call graphs, and class diagrams.

## Usage

```bash
VeeamPhaser [options] <command> [command-options] <file>
```

### Examples

```bash
VeeamPhaser -o output_dir backup.vbk -x 0:10    # Extract file from VBK
VeeamPhaser md backup.vbk                        # Display metadata
VeeamPhaser toc backup.vbk                       # Show table of contents
VeeamPhaser scan backup.vbk                      # Scan for recoverable data
VeeamPhaser repo /path/to/repository             # Build repository index
```

### Commands

| Command | Description |
|---------|-------------|
| `vbk` | Process vbk/vib files (default) |
| `blocks` | Extract all blocks from VIB/VBK |
| `carve` | Block carver for data recovery |
| `crc32` | Calculate CRC32 checksum |
| `md` | Process metadata file |
| `meta_process` | Generate metadata |
| `repo` | Repository indexer, search and repair |
| `scan` | Scan for MD blocks |
| `toc` | Extract table of contents |
| `test` | Run built-in self-tests |

Run `VeeamPhaser <command> --help` for command-specific options.

### Global Options

```
-h, --help              Show help message
-v, --verbose           Increase verbosity (-vv, -vvv for more)
-f, --force             Continue on errors
-o, --out-dir DIR       Output directory
-L, --log FILE          Log file path
--version               Print version
```

## Testing

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run specific test
./dist/build-linux/tests/VBKCommand_test
```

Tests are in `tests/` with fixtures in `tests/fixtures/`.

## Project Structure

```
src/
├── commands/      # Command implementations
├── core/          # Veeam data structures
├── data/          # HashTable, RepoIndexer, caches
├── io/            # Reader, Writer, Logger
├── processing/    # Extraction and carving logic
├── scanning/      # Recovery algorithms
└── utils/         # Utilities

tests/             # Google Test suite
build/cmake/       # CMake modules and toolchains
```

## Code Style

**Naming Conventions:**
- Classes/structs: Start with capital letter (`CMeta`, `FileDescriptor`)
- Variables: Start with lowercase (`genBitmap`, `outFname`)
- Functions: Start with lowercase (`writeBlock`, `mdShowPage`)
- Constants/defines: All caps (`VMETA_SIZE`, `LZ_START_MAGIC`)

**Best Practices:**
- Avoid global variables
- Use `Logger` class instead of `printf` or `cout` for messages
- Don't wrap entire functions in try-catch blocks