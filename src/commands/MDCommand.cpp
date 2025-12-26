/**
 * @file MDCommand.cpp
 * @brief Implementation of the MDCommand for processing Veeam metadata files.
 *
 * This file provides comprehensive functionality for working with Veeam metadata files,
 * including listing files, extracting files, testing file integrity, reading raw pages,
 * processing page stacks, and handling encrypted metadata. It supports multiple metadata
 * sources (METADATA files, METADATASCAN, slots) and can work with external hash tables
 * for carved data reconstruction.
 */

#include "MDCommand.hpp"
#include "utils/common.hpp"
#include "utils/hexdump.hpp"
#include "processing/ExtractContext.hpp"
#include "io/Reader.hpp"
#include <zstd.h>
#include <memory>

extern int verbosity;
extern bool verbosity_changed;
extern bool g_force;

REGISTER_COMMAND(MDCommand);

/**
 * @brief Adds common command-line arguments shared across multiple commands.
 *
 * This function configures a standard set of arguments for metadata processing,
 * including options for listing files, extraction, testing, raw page access,
 * page stack reading, deep scanning, decompression, and external data sources.
 *
 * @param parser The argument parser to add arguments to.
 */
void MDCommand::add_common_args(argparse::ArgumentParser& parser){
    parser.add_argument("-l", "--list-files")
        .default_value(true)
        .implicit_value(true)
        .help("list files in metadata (default)");
    parser.add_argument("-x", "--extract")
        .nargs(argparse::nargs_pattern::any)
        .help("extract a file from metadata by its name/id, extract all files if no argument, extract multiple selected files ex: -x file1 -x file2...");
    parser.add_argument("-res", "--resume")
        .default_value(false)
        .help("Resume file extraction");
    parser.add_argument("-t", "--test")
        .nargs(argparse::nargs_pattern::any)
        .help("test a file integrity metadata by its name/id, test all files if no argument, test multiple selected files ex: -t file1 -t file2...");

    parser.add_argument("-j", "--json-file")
        .help("append test results to JSON file");

    parser.add_argument("-p", "--page")
        .help("MetaID:PageID - show raw data from metadata ('all' for all pages)");
    parser.add_argument("-S", "--stack")
        .help("MetaID:PageID - read PageStack starting at MetaID:PageID ('all' for all stacks)");

    parser.add_argument("-d", "--deep")
        .default_value(false)
        .implicit_value(true)
        .help("go over all PageStacks and try to interpret them as files");

    parser.add_argument("-D", "--decompress")
        .default_value(false)
        .implicit_value(true)
        .help("try to decompress [zstd] compressed pages");
    parser.add_argument("-w", "--write")
        .default_value("")
        .help("write raw data to file instead of stdout");

    parser.add_argument("--device")
        .append()
        .help("Device for extracting files from carved data.");
    parser.add_argument("--data")
        .append()
        .help("Carved offset file data.");
    parser.add_argument("--skip-read")
        .default_value(false)
        .implicit_value(true)
        .help("Do not read blocks when extracting/testing files, only check if it exists in the hash table.");

    parser.add_argument("--password")
        .default_value(std::string(""))
        .help("Password for decrypting encrypted vbk.");
    parser.add_argument("--dump-keysets")
        .default_value(false)
        .implicit_value(true)
        .help("Dump loaded AES keysets (uuid + key + iv).");
    parser.add_argument("--session")
        .default_value(false)
        .implicit_value(true)
        .help("When used with --dump-keysets, only dump session keysets. (used to encrypt data blocks)");
    parser.add_argument("--new-version")
        .help("skip version detect and force old (false) or new (true) metadata version");
}

/**
 * @brief Constructs an MDCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
MDCommand::MDCommand(bool reg) : Command(reg, "md", "process metadata file") {
    m_parser_ptr->add_argument("filenames")
        .nargs(argparse::nargs_pattern::at_least_one)
        .help("metadata file (METADATA/METADATASCAN/slot)");

    add_common_args(m_parser);

    m_parser_ptr->add_argument("--vbk")
        .help("VIB/VBK file for extracting files");
    m_parser_ptr->add_argument("--vbk-offset")
        .default_value((uint64_t)0ULL).scan<'x', uint64_t>()
        .help("VBK start offset (hex), i.e. when opening a physical drive");
    m_parser_ptr->add_argument("--no-vbk")
        .default_value(false)
        .implicit_value(true)
        .help("work without VBK file, for MD structure validation");
}

/**
 * @brief Creates and configures a CMeta object from a metadata file.
 *
 * Initializes a CMeta object with the specified metadata file and applies
 * configuration options such as version forcing and deep scan mode based
 * on command-line arguments.
 *
 * @param fname Path to the metadata file.
 * @return Configured CMeta object ready for use.
 */
CMeta MDCommand::create_meta(const fs::path& fname){
    std::string password = m_parser_ptr->get<std::string>("--password");
    const bool dump_keysets = m_parser_ptr->get<bool>("--dump-keysets");
    const bool session_only = m_parser_ptr->get<bool>("--session");
    CMeta meta(fname, g_force, m_meta_offset, m_meta_src, password, dump_keysets, m_keysets_same_file, session_only);
    if( m_parser_ptr->is_used("--new-version") ){
        meta.set_version(parse_bool(m_parser_ptr->get<std::string>("--new-version")) ? 1 : 0);
    }
    if( m_parser_ptr->is_used("--deep") ){
        meta.set_deep_scan(m_parser_ptr->get<bool>("--deep"));
    }
    return meta;
}

/**
 * @brief Lists all files contained in a metadata file with their attributes.
 *
 * Iterates through all files in the metadata and logs information including
 * physical page ID, file type, number of blocks, size, and full pathname.
 *
 * @param fname Path to the metadata file to list.
 * @return EXIT_SUCCESS on successful completion.
 */
int MDCommand::list_files(const fs::path& fname){
    auto meta = create_meta(fname);
    meta.for_each_file([](const std::string& pathname, const CMeta::VFile& vFile){
        std::string size_str;
        if( vFile.is_dir() ){
            size_str = "";
        } else if( vFile.attribs.filesize == -1 ){
            size_str = "?";
        } else {
            size_str = bytes2human(vFile.attribs.filesize);
        }
        logger->info("{} {:6} {:8x} {:6} {}", vFile.attribs.ppi, vFile.type_str(), vFile.attribs.nBlocks, size_str, pathname);
    });
    return EXIT_SUCCESS;
}

/**
 * @brief Loads an external hash table from carved data files.
 *
 * This function loads hash table data from CSV files containing carved block information
 * and their corresponding device files. It implements caching to speed up subsequent loads.
 * If loading fails, the program exits with an error. The hash table is used for
 * reconstructing files from carved data when the original VBK structure is unavailable.
 *
 * @param md_fname Path to the metadata file (used for cache file naming).
 */
void MDCommand::load_external_hashtable(const fs::path& md_fname) {
    auto csv_fnames = m_parser_ptr->get<std::vector<std::string>>("--data");
    auto device_fnames = m_parser_ptr->get<std::vector<std::string>>("--device");
    
    if (csv_fnames.size() != device_fnames.size()) {
        logger->critical("Mismatch between number of --data files ({}) and number of --device files ({})", csv_fnames.size(), device_fnames.size());
        exit(1);
    }
    
    const fs::path cache_fname = get_out_pathname(md_fname, "ht_cache.bin");
    bool load_from_cache = fs::exists(cache_fname);


    for (size_t i = 0; i < csv_fnames.size(); i++) {
        const auto& csv_fname = csv_fnames[i];
        if( load_from_cache && fs::exists(csv_fname) && fs::last_write_time(csv_fname) > fs::last_write_time(cache_fname) ){
            logger->info("exHT: {} is newer than {}, ignoring cache", csv_fname, cache_fname);
            load_from_cache = false;
            break;
        }
    }

    //check if cache is valid (hashes of data paths match + file sizes of devices match)
    
    if (load_from_cache && m_external_ht.loadFromCache(cache_fname,csv_fnames.size())) {
        logger->info("exHT: loaded {} entries from {}", m_external_ht.size(), cache_fname);
    } else {
        for (size_t i = 0; i < csv_fnames.size(); i++) {
            const auto& csv_fname = csv_fnames[i];
            logger->info("exHT: loading {} ...", csv_fname);

            if (m_external_ht.loadFromTextFile(csv_fname, static_cast<uint8_t>(i))){
                logger->info("exHT: loaded {} entries from {}", m_external_ht.size(), csv_fname);
            } else {
                logger->critical("exHT: Error loading {}", csv_fname);
                exit(1);
            }
        }
        if (!m_external_ht.sortEntries()) {
            logger->critical("exHT: Error sorting hashtable entries, no entries are present.");
            exit(1);
        }
        logger->info("exHT: total {} unique entries", m_external_ht.size());
        if (m_external_ht.saveToCache(cache_fname, csv_fnames.size())){
            logger->info("exHT: {} saved successfully!", cache_fname);
        } else {
            logger->error("exHT: Error saving {}", cache_fname);
        }
    }
}

/**
 * @brief Extracts or tests a file (or files) from metadata.
 *
 * This function handles file extraction and integrity testing from metadata files.
 * It supports extraction by filename, glob patterns, or physical page ID. The function
 * can work with VBK files directly or with external carved data via hash tables.
 * When test_only is true, files are validated but not extracted.
 *
 * @param md_fname Path to the metadata file.
 * @param xname Name, glob pattern, or physical page ID of file(s) to extract. Empty string extracts all files.
 * @param resume If true, resumes extraction of partially extracted files (default: false).
 * @param test_only If true, only test file integrity without extracting (default: false).
 * @param verbosity_changed If true, verbosity level was explicitly set by user (default: false).
 * @return EXIT_SUCCESS on success, EXIT_FAILURE if file not found or on error.
 */
int MDCommand::extract_file(const fs::path& md_fname, const std::string& xname, bool resume, bool test_only, bool verbosity_changed){
    PhysPageId needle_ppi;

    if( xname.find(':') != std::string::npos && xname.size() < 10 ){
        needle_ppi = PhysPageId(xname);
        if (needle_ppi.zero())
            needle_ppi = PhysPageId();
    }

    std::vector<std::unique_ptr<Reader>> device_files;
    if (m_parser_ptr->is_used("--device")) {
        auto devices = m_parser_ptr->get<std::vector<std::string>>("--device");
        for (const auto& dev : devices) {
            device_files.emplace_back(std::make_unique<Reader>(dev));
        }
    }

    fs::path vbk_fname;
    if( m_parser_ptr->present("--vbk") ){
        vbk_fname = m_parser_ptr->get<std::string>("--vbk");
    } else if( !m_vbk_override.empty() && !m_parser_ptr->get<bool>("--no-vbk") ){
        vbk_fname = m_vbk_override;
    } else {
        // guess vbk fname from path
        fs::path path(md_fname);
        for( int i=0; i<5; i++){
            if( path.empty() ){
                break;
            }
            if( path_ends_with(path, ".out"_n) ){
                fs::path fname = path.string().substr(0, path.string().size()-4);
                if( fs::exists(fname) ){
                    vbk_fname = fname;
                    break;
                }
            }
            path = path.parent_path();
        }
    }

    if( vbk_fname.empty() && device_files.empty() && !m_parser_ptr->get<bool>("--no-vbk") ){
        logger->critical("No --vbk nor --device specified and can't guess vbk filename from path");
        return EXIT_FAILURE;
    }

    std::unique_ptr<Reader> vbkf;
    if( !vbk_fname.empty()){
        vbkf = std::make_unique<Reader>(vbk_fname);
    }

    if( m_parser_ptr->get<bool>("--no-vbk") && !test_only ){
        logger->critical("No VBK file specified, can't extract files without it");
        return EXIT_FAILURE;
    }

    bool level_changed = false;
    const auto prev_level = logger->console_level();

    if( vbkf ){
        const size_t vbk_size = vbkf->size();
        logger->info("source vbk {} ({:x} = {})", vbk_fname, vbk_size, bytes2human(vbk_size));
    }

    if( test_only && !verbosity_changed ){
        // logger->warn("Applying flags \"-qqqf\" for the test duration. Set verbosity manually via -q/-v to disable this.");
        g_force = true;
        level_changed = true;
    }

    auto meta = create_meta(md_fname);
    ExtractContext ctx(meta, std::move(vbkf), m_external_ht, device_files, m_cache, prev_level, level_changed);

    if (m_parser_ptr->get<bool>("--skip-read")){
        logger->info("Disabled reading blocks when extracting/testing files, only checking if it exists in the hash table.");
        ctx.no_read = true;
    }

    ctx.md_fname = md_fname;
    ctx.needle_ppi = needle_ppi;
    ctx.test_only = test_only;
    ctx.have_vbk = (ctx.vbkf != nullptr);
    ctx.vbk_offset = m_parser_ptr->get<uint64_t>("--vbk-offset");
    ctx.xname = xname;
    ctx.xname_is_glob = is_glob(xname);
    ctx.xname_is_full = xname.find('/') != std::string::npos;
    if( m_parser_ptr->is_used("--json-file") ){
        ctx.json_fname = m_parser_ptr->get<std::string>("--json-file");
    }

    logger->with_console_level( level_changed ? spdlog::level::critical : prev_level, [&](){
        meta.for_each_file([&](const std::string& pathname, const CMeta::VFile& vFile){
            ctx.process_file(pathname, vFile, resume);
        });
    });

    if (!xname.empty() && !ctx.found) {
        if (needle_ppi.valid()){
            logger->error("File with id {} not found in metadata", xname, needle_ppi);
        } else {
            logger->error("File \"{}\" not found in metadata", xname);
        }
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/**
 * @brief Attempts to decompress a ZSTD-compressed metadata page.
 *
 * Checks if the provided data is ZSTD-compressed (via magic number) and attempts
 * decompression. The decompressed data is stored in the output buffer.
 *
 * @param data Pointer to potentially compressed data.
 * @param size Size of the input data.
 * @param out Output buffer that will contain decompressed data if successful.
 * @param pos File position for logging purposes.
 * @return True if decompression was successful, false otherwise.
 */
bool decompress_page(void* data, size_t size, buf_t& out, off_t pos) {
    if( size < PAGE_SIZE ){
        return false;
    }
    if( *(uint32_t*)data != ZSTD_MAGICNUMBER ){
        return false;
    }

    ZSTD_DCtx* const dctx = ZSTD_createDCtx();
//    const size_t comp_size = ZSTD_findFrameCompressedSize(data, PAGE_SIZE);
//    logger->debug("ZSTD_findFrameCompressedSize: {:x}", comp_size);

    out.clear();
    out.resize(0x20000); // ZSTD_LBMAX or other 128kb const
    ZSTD_inBuffer input = { data, size, 0 };
    ZSTD_outBuffer output = { out.data(), out.size(), 0 };
    size_t ret = ZSTD_decompressStream(dctx, &output, &input);
    ZSTD_freeDCtx(dctx);

    if( ret != 0 ){
        if( ZSTD_isError(ret) ){
            logger->error("ZSTD_decompressStream: {:x} isError={} : {}", ret, ZSTD_isError(ret), ZSTD_getErrorName(ret));
        } else {
            logger->warn("ZSTD_decompressStream: want {:x} more src bytes", ret);
        }
        logger->debug("{:012x}: zstd: {:4x}/{:4x} -> {:5x}/{:5x}", pos, input.pos, input.size, output.pos, output.size);
        return false;
    }
    logger->debug("{:012x}: zstd: {:4x} -> {:5x}", pos, input.pos, output.pos);
    out.resize(output.pos);

    return true;
}

/**
 * @brief Reads and displays a page stack from metadata.
 *
 * A page stack is a chain of metadata pages linked together. This function can either
 * display all page stacks in the metadata (when id="all") or display a specific stack
 * starting at the given physical page ID. In verbose mode, it also shows the raw data
 * of each page in the stack.
 *
 * @param fname Path to the metadata file.
 * @param id Either "all" to show all stacks, or a physical page ID string (e.g., "1:23").
 * @return EXIT_SUCCESS if successful, EXIT_FAILURE if the specified stack is not valid.
 */
int MDCommand::read_stack(const fs::path& fname, const std::string& id){
    auto meta = create_meta(fname);

    if( id == "all" ){
        meta.for_each_page([&](const PhysPageId& ppi, const uint8_t*){
            const auto ps = meta.get_page_stack(ppi);
            if( ps ){
                logger->info("{}: {}", ppi, ps);
            }
        });
        return EXIT_SUCCESS;
    } else {
        PhysPageId ppi(id);
        logger->info("reading PageStack {}", ppi);

        const auto ps = meta.get_page_stack(ppi);
        if( ps ){
            logger->info("{}", ps.to_string());
            if( logger->console_level() < spdlog::level::info ){
                buf_t buf;
                int idx = 0;
                for( const auto ppi : ps ){
                    if(meta.get_page(ppi, buf)){
                        logger->info("page {} ({}/{}){}", ppi, idx, ps.size(), to_hexdump(buf));
                    }
                    idx++;
                }
            }
            return EXIT_SUCCESS;
        } else {
            logger->error("no valid PageStack at {}", ppi);
            return EXIT_FAILURE;
        }
    }
}

/**
 * @brief Reads and displays a raw metadata page.
 *
 * Reads a single page from metadata at the specified physical page ID. Can optionally
 * decompress ZSTD-compressed pages. The page data can be displayed as a hex dump or
 * written to a file. When id="all", displays all pages in the metadata.
 *
 * @param fname Path to the metadata file.
 * @param id Physical page ID string (e.g., "1:23") or "all" to show all pages.
 * @param out_fname Optional output file path. If empty, displays hex dump to console.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE if page cannot be read.
 */
int MDCommand::read_page(const fs::path& fname, const std::string& id, const fs::path& out_fname){
    if( id == "all" ){
        return show_all_pages(fname);
    }
    PhysPageId ppi(id);
        
    auto meta = create_meta(fname);

    buf_t buf;
    logger->info("reading page {}", ppi);
    if(!meta.get_page(ppi, buf)){
        return EXIT_FAILURE;
    }

    if( *(uint32_t*)buf.data() == ZSTD_MAGICNUMBER ){
        if( m_decompress ){
            buf_t decompressed;
            if( decompress_page(buf.data(), buf.size(), decompressed, 0) ){
                logger->info("decompressed: {:x} -> {:x} bytes", buf.size(), decompressed.size());
                buf = decompressed;
            }
        } else {
            logger->warn("[ZSTD_MAGICNUMBER detected, but decompress is not enabled]");
        }
    }

    if( out_fname.empty() ){
        hexdump(buf, "    ");
    } else {
        std::ofstream of(out_fname, std::ios::binary);
        if( !of ){
            logger->critical("{}: {}", out_fname, strerror(errno));
            exit(1);
        }
        of.write((const char*)buf.data(), buf.size());
        logger->info("saved {} bytes to \"{}\"\n", buf.size(), out_fname);
    }
    return EXIT_SUCCESS;
}

/**
 * @brief Displays all metadata pages with their physical page IDs.
 *
 * Iterates through all pages in the metadata file and displays each page's
 * physical page ID along with a hex dump of its contents.
 *
 * @param fname Path to the metadata file.
 * @return EXIT_SUCCESS on completion.
 */
int MDCommand::show_all_pages(const fs::path& fname){
    auto meta = create_meta(fname);
    meta.for_each_page([](const PhysPageId& ppi, const uint8_t* data){
        logger->info("{}{}", ppi, to_hexdump(data, PAGE_SIZE));
    });
    return EXIT_SUCCESS;
}

/**
 * @brief Processes a single metadata file according to command-line options.
 *
 * This is the main dispatch function that routes to the appropriate operation
 * (list files, extract, test, read page, read stack) based on command-line arguments.
 * On Windows, it also handles glob pattern expansion for metadata filenames.
 *
 * @param md_fname Path to the metadata file to process.
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
 */
int MDCommand::process_md_file(const fs::path& md_fname){
    const fs::path out_fname = m_parser_ptr->get("--write");

#ifdef _WIN32
    // expand glob patterns on Windows bc cmd doesn't do it
    if (is_glob(md_fname)) {
        return process_md_files(simple_glob_find(md_fname));
    }
#endif

    init_log(md_fname);

    if( m_parser_ptr->get<bool>("--dump-keysets") && md_fname.extension() == ".bank"){ //dump the keysets, and return cleanly.
        (void)create_meta(md_fname);
        return EXIT_SUCCESS;
    }

    // load external HT just once
    if( m_parser_ptr->present("--device") && m_parser_ptr->present("--data") && !m_external_ht )
        load_external_hashtable(md_fname); // either loads or aborts

    if( logger->console_level() <= spdlog::level::info )   
        puts("");
    logger->info("processing {}", md_fname);
    if( auto page_id = m_parser_ptr->present("--page") ){
        return read_page(md_fname, *page_id, out_fname);
    }
    if( auto stack_page_id = m_parser_ptr->present("--stack") ){
        return read_stack(md_fname, *stack_page_id);
    }
    if( m_parser_ptr->is_used("--extract") ){
        auto files = m_parser_ptr->get<std::vector<std::string>>("--extract");
        bool resume = m_parser_ptr->is_used("--resume");

        if(files.empty()) {
            files.push_back("");
        }

        for(const auto& file : files) {
            int result = extract_file(md_fname, file, resume);
            if(result != EXIT_SUCCESS) return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;

    }

    if( m_parser_ptr->is_used("--test") ){
        auto files = m_parser_ptr->get<std::vector<std::string>>("--test");
    
        if(files.empty()) {
            files.push_back("");
        }

        for(const auto& file : files) {
            int result = extract_file(md_fname, file, false, true, verbosity_changed);
            if(result != EXIT_SUCCESS) return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    
    return list_files(md_fname);
}

/**
 * @brief Processes multiple metadata files sequentially.
 *
 * Iterates through a list of metadata files and processes each one. Returns
 * the last non-success exit code encountered, or EXIT_SUCCESS if all files
 * were processed successfully.
 *
 * @param md_fnames Vector of metadata file paths to process.
 * @return EXIT_SUCCESS if all files processed successfully, otherwise the last error code.
 */
int MDCommand::process_md_files(const std::vector<fs::path>& md_fnames) {
    const bool dump_keysets = m_parser_ptr->get<bool>("--dump-keysets");

    m_keysets_same_file.reset();
    if( dump_keysets && md_fnames.size() > 1 ){
        const fs::path out_dir = get_out_dir(md_fnames.front());
        m_keysets_same_file = out_dir / "all_keysets.bin";
        std::error_code ec;
        fs::remove(*m_keysets_same_file, ec);
    }

    int result = EXIT_SUCCESS;
    for( const auto& md_fname : md_fnames ){
        int r = process_md_file(md_fname);
        if (r != EXIT_SUCCESS) {
            result = r;
        }
    }

    m_keysets_same_file.reset();
    return result;
}

/**
 * @brief Executes the MD command with parsed command-line arguments.
 *
 * This is the main entry point for the md command. It parses the filenames
 * and decompression flag from command-line arguments and delegates to
 * process_md_files() for the actual work. This method is also called by
 * VBKCommand for integrated VBK/metadata processing.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on error.
 */
int MDCommand::run() {
    m_decompress = m_parser_ptr->get<bool>("--decompress");

    const std::vector<std::string> md_str_fnames = m_parser_ptr->get<std::vector<std::string>>("filenames");
    std::vector<fs::path> md_fnames {
        md_str_fnames.begin(), md_str_fnames.end()
    };
    return process_md_files(md_fnames);
}
