#include "Command.hpp"
#include "core/CMeta.hpp"
#include "data/HashTable.hpp"
#include "data/lru_set.hpp"

#include <optional>

class MDCommand : public Command {
public:
    int run() override;

    int extract_file(const fs::path& fname, const std::string& xname, bool resume = false, bool test_only = false, bool verbosity_changed = false);
    int list_files(const fs::path& fname);
    int read_page(const fs::path& fname, const std::string& id, const fs::path&);
    int read_stack(const fs::path& fname, const std::string& id);
    int show_all_pages(const fs::path& fname);

    void set_meta_offset(off_t offset){ m_meta_offset = offset; }
    void set_meta_source(CMeta::EMetaSource src){ m_meta_src = src; }
    void set_parser(argparse::ArgumentParser* parser){ m_parser_ptr = parser; }
    void set_vbk_override(const fs::path& vbk_path){ m_vbk_override = vbk_path.string(); }

    static void add_common_args(argparse::ArgumentParser& parser);

protected:
    static MDCommand instance; // Static instance to trigger registration
    MDCommand(bool reg = false);
    CMeta create_meta(const fs::path& fname);

    void load_external_hashtable(const fs::path& md_fname);
    int process_md_file(const fs::path& md_fname);
    int process_md_files(const std::vector<fs::path>& md_fnames);

private:
    // holds a pointer to internal m_parser for normal MDCommand's and VBKCommand's m_parser if MDCommand is instantiated from VBKCommand
    argparse::ArgumentParser* m_parser_ptr = &m_parser;
    bool m_decompress = false;
    off_t m_meta_offset = 0;
    CMeta::EMetaSource m_meta_src = CMeta::MS_AUTO;
    HashTable m_external_ht;
    std::string m_vbk_override;

    lru_set<digest_t> m_cache {10*1024*1024};
    std::optional<fs::path> m_keysets_same_file;

    friend class MDCommandTest;
    friend class VBKCommand;
    friend class CmdTestBase<MDCommand>;
};
