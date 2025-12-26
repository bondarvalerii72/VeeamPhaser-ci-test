#include <gtest/gtest.h>
#include "test_utils.hpp"
#include "utils/common.cpp"

class OutDirTest : public ::testing::Test {
protected:
    void SetUp() override {
        register_program_args(program);
    }
};

TEST_F(OutDirTest, explicitly_specified) {
    program.parse_args({"./app", "-o", "tmp/explicit_out_dir"});
    ASSERT_EQ(get_out_dir("foo", program), "tmp/explicit_out_dir");
}

TEST_F(OutDirTest, auto_already_in_out_dir) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("tmp.out/filename.bin", program), "tmp.out");
}

TEST_F(OutDirTest, auto_md_file) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("tmp/filename.md", program), "tmp");
}

TEST_F(OutDirTest, auto_md_file_in_cur_dir) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("filename.md", program), "");
}

TEST_F(OutDirTest, auto_slot_file) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("tmp/filename.slot", program), "tmp");
}

TEST_F(OutDirTest, auto_slot_file_in_cur_dir) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("filename.slot", program), "");
}

TEST_F(OutDirTest, auto_bank_file) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("tmp/filename.bank", program), "tmp");
}

TEST_F(OutDirTest, auto_bank_file_in_cur_dir) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("filename.bank", program), "");
}

TEST_F(OutDirTest, auto_metadata) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("tmp/filename_METADATA.bin", program), "tmp");
    ASSERT_EQ(get_out_dir("tmp/filename_METADATASCAN.bin", program), "tmp");
    ASSERT_EQ(get_out_dir("tmp/METADATA.bin", program), "tmp");
    ASSERT_EQ(get_out_dir("tmp/METADATASCAN.bin", program), "tmp");
}

TEST_F(OutDirTest, auto_no_metadata) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("tmp/filename.vbi", program), "tmp/filename.vbi.out");
    ASSERT_EQ(get_out_dir("tmp/filename.vbk", program), "tmp/filename.vbk.out");
    ASSERT_EQ(get_out_dir("tmp/filename", program), "tmp/filename.out");
}

TEST_F(OutDirTest, physicaldrive) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("\\\\.\\PhysicalDrive0", program), "PhysicalDrive0.out");
}

TEST_F(OutDirTest, dev) {
    program.parse_args({"./app"});
    ASSERT_EQ(get_out_dir("/dev/sda1", program), "sda1.out");
}

TEST_F(OutDirTest, template_expansion) {
    program.parse_args({"./app", "-O", "./tmp/%f/%b/%d/%p/%P"});
    ASSERT_EQ(get_out_dir("/path/to/src.ext", program), "./tmp/src.ext/src/to//path/to//path/to/src.ext");
}

/// expand_dir_tpl

TEST(expand_dir_tpl, f) {
    ASSERT_EQ(expand_dir_tpl("%f", "/path/to.1/src.ext"), "src.ext");
}

TEST(expand_dir_tpl, b) {
    ASSERT_EQ(expand_dir_tpl("%b", "/path/to.1/src.ext"), "src");
}

TEST(expand_dir_tpl, d) {
    ASSERT_EQ(expand_dir_tpl("%d", "/path/to.1/src.ext"), "to.1");
}

TEST(expand_dir_tpl, p) {
    ASSERT_EQ(expand_dir_tpl("%p", "/path/to.1/src.ext"), "/path/to.1");
}

TEST(expand_dir_tpl, P) {
    ASSERT_EQ(expand_dir_tpl("%P", "/path/to.1/src.ext"), "/path/to.1/src.ext");
}

TEST(expand_dir_tpl, percent) {
    ASSERT_EQ(expand_dir_tpl("foo%%bar", "/path/to.1/src.ext"), "foo%bar");
}

TEST(expand_dir_tpl, t) {
    // check it's a string like "20250111_095914"
    const std::string result = expand_dir_tpl("%t", "/path/to/src.ext").string();
    ASSERT_EQ(result.size(), 15);
    int nu = 0;
    for (const auto c : result) {
        if (c == '_'){
            nu++;
            continue;
        }
        ASSERT_TRUE(isdigit(c));
    }
    ASSERT_EQ(nu, 1);
}

/// get_out_pathname

TEST_F(OutDirTest, get_out_pathname) {
    ASSERT_EQ(get_out_pathname("filename.md", "new_file"), "new_file");
}

/// sanitize_fname

TEST(test_sanitize_fname, normal) {
    ASSERT_EQ("filename.md", sanitize_fname(std::string("filename.md")));
    ASSERT_EQ("filename", sanitize_fname(std::string("filename")));
    ASSERT_EQ("/path/to/filename", sanitize_fname(std::string("/path/to/filename")));
    ASSERT_EQ("\\path\\to\\filename", sanitize_fname(std::string("\\path\\to\\filename")));
}

TEST(test_sanitize_fname, many_dots) {
    ASSERT_EQ("__filename.md", sanitize_fname(std::string("..filename.md")));
    ASSERT_EQ("__/filename.md", sanitize_fname(std::string("../filename.md")));
    ASSERT_EQ("filename__md", sanitize_fname(std::string("filename..md")));
    ASSERT_EQ("__/filename", sanitize_fname(std::string("../filename")));
    ASSERT_EQ("foo/___/filename", sanitize_fname(std::string("foo/.../filename")));
}

TEST(test_sanitize_fname, many_backslashes) {
    ASSERT_EQ("/filename", sanitize_fname(std::string("/filename")));
    ASSERT_EQ("/filename", sanitize_fname(std::string("//filename")));
    ASSERT_EQ("/filename", sanitize_fname(std::string("///filename")));
    ASSERT_EQ("path/filename", sanitize_fname(std::string("path///filename")));
}

TEST(test_sanitize_fname, many_slashes) {
    ASSERT_EQ("\\filename", sanitize_fname(std::string("\\filename")));
    ASSERT_EQ("\\filename", sanitize_fname(std::string("\\\\filename")));
    ASSERT_EQ("\\filename", sanitize_fname(std::string("\\\\\\filename")));
    ASSERT_EQ("path\\filename", sanitize_fname(std::string("path\\\\\\filename")));
}

TEST(test_sanitize_fname, bad_chars) {
    ASSERT_EQ("______ !@#$%^&_()_+___", sanitize_fname(std::string("\xff\x7f\r\n|? !@#$%^&*()_+<>\"")));
}

/// simple_glob_find

TEST(test_simple_glob_find, exact_fname) {
    auto vbk_pathname = find_fixture("AgentBack2024-09-16T163946.vbk");
    auto results = simple_glob_find(vbk_pathname.string());
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0], vbk_pathname);
}

TEST(test_simple_glob_find, glob_tail) {
    auto vbk_pathname = find_fixture("encrypted_bank_hdr.bin");
    auto results = simple_glob_find(vbk_pathname.string() + "*");
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0], vbk_pathname);
}

TEST(test_simple_glob_find, glob_cut_tail) {
    auto vbk_pathname = find_fixture("encrypted_bank_hdr.bin");
    std::string glob = vbk_pathname.string();
    glob = glob.substr(0, glob.size() - 1) + "*";
    auto results = simple_glob_find(glob);
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0], vbk_pathname);
}

TEST(test_simple_glob_find, glob_dir) {
    auto vbk_pathname = find_fixture("AgentBack2024-09-16T163946.vbk");
    std::string glob = vbk_pathname.string();
    glob = glob.replace(glob.find("fixtures"), 8, "fix*res");
    auto results = simple_glob_find(glob);
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0], vbk_pathname);
}

TEST(test_simple_glob_find, glob_dir_negative) {
    auto vbk_pathname = find_fixture("AgentBack2024-09-16T163946.vbk");
    std::string glob = vbk_pathname.string();
    glob = glob.replace(glob.find("fixtures"), 8, "fix*1res");
    auto results = simple_glob_find(glob);
    ASSERT_EQ(results.size(), 0);
}
