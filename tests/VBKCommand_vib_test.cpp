#include <blake3z_file.hpp>
#include "commands/VBKCommand.hpp"
#include "test_utils.hpp"
#include <nlohmann/json.hpp>
extern argparse::ArgumentParser program;

class VBKCommandTest : public CmdTestBase<VBKCommand> {
    protected:
    void SetUp() override {
        register_program_args(program);
        std::filesystem::remove_all(get_out_dir(vbk_fname_str()));
        std::filesystem::remove_all(get_out_dir(vib_fname_str()));
        ASSERT_EQ(1, blake3_open_cache(find_file("_deps/blake3z-src/blake3z.cache").string().c_str()));
    }
};

std::string read_file(const fs::path& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST_F(VBKCommandTest, extract_all) {
    run_cmd({"unused", vib_fname_str(), "-x"});
    const auto root = get_out_dir(vib_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cb040bd2d7d65f8236c223b4c6a703d7e6a6f7d2aaf540c3b47e3a70030e5a0a", blake3z_calc_file_str(root / "summary.xml"));
    ASSERT_EQ("18d4f9825c091ae0d5bd938ac5f8bfbf2216a88d9f2fc168fd9513d3ebb5bc12", blake3z_calc_file_str(root / "digest_66d23fcb-9393-439d-972a-972a132f2a9e"));
    ASSERT_EQ("1b25ee4bdf3ed17c1cf2ea9dc84147719274b7dc8bce97c7bb1755e56cd1339b", blake3z_calc_file_str(root / "5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
    ASSERT_EQ("bb3bda81a664964f0bd3ea39f9231fba21dc8f8271f733ea3ce8092ce36bafaf", blake3z_calc_file_str(root / "GuestMembers.xml"));
    ASSERT_EQ("1f124606f97b817be6d04783a94c706288ca7f41216832567af53d5b80e5ab4a", blake3z_calc_file_str(root / "BackupComponents.xml"));
}

TEST_F(VBKCommandTest, patch) {
    run_cmd({"unused", vbk_fname_str(), "-x", "0000:0010"});

    program.parse_args({"unused", "-o", get_out_dir(vbk_fname_str()).string()});
    run_cmd({"unused", vib_fname_str(), "-x", "0000:0010"});

    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("019b464af23391c505ed5a93cec2c5027079f224cd80303911e913ee719b4ea8", blake3z_calc_file_str(root / "5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
}

std::filesystem::path no_dir_fname(const std::filesystem::path& vib_fname) {
    const auto copy_fname = get_out_pathname(vib_fname.string(), "no_dir.vib");
    std::filesystem::copy(vib_fname, copy_fname);

    char buf[0x1000];
    memset(buf, 0, sizeof(buf));

    std::ofstream f(copy_fname, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(0x108000);
    f.write(buf, sizeof(buf));
    f.seekp(0x14c000);
    f.write(buf, sizeof(buf));

    return copy_fname;
}

TEST_F(VBKCommandTest, dir_destroyed__list_files) {
    const auto copy_fname = no_dir_fname(vib_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", copy_fname, "-l", "--deep"});
    });

    for_each_line( R"(
        0000:0005 IntFib        1 25Kb   0000_0005.bin
        0000:000b IntFib        1 6Kb    0000_000b.bin
        0000:0010 Inc           8 931Gb  0000_0010.bin
        0000:0013 IntFib        1 249    0000_0013.bin
        0000:0017 IntFib        1 5Kb    0000_0017.bin
    )", [&](const std::string& line) {
        EXPECT_THAT(output, HasSubstr(line));
    });
}

TEST_F(VBKCommandTest, dir_destroyed__test_all) {
    const auto copy_fname = no_dir_fname(vib_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", copy_fname, "-t", "--deep"});
    });
    EXPECT_THAT(output, HasSubstr("OK_BLK"));
    EXPECT_THAT(output, HasSubstr("0000_0005.bin"));
    EXPECT_THAT(output, HasSubstr("0000_000b.bin"));
    EXPECT_THAT(output, HasSubstr("0000_0010.bin"));
    EXPECT_THAT(output, HasSubstr("0000_0013.bin"));
    EXPECT_THAT(output, HasSubstr("0000_0017.bin"));
    EXPECT_THAT(output, HasSubstr("100.00"));
}

TEST_F(VBKCommandTest, dir_destroyed__extract_all) {
    const auto copy_fname = no_dir_fname(vib_fname());
    
    run_cmd({"unused", copy_fname, "-x", "--deep"});

    const auto root = get_out_dir(vib_fname_str());
    ASSERT_EQ("cb040bd2d7d65f8236c223b4c6a703d7e6a6f7d2aaf540c3b47e3a70030e5a0a", blake3z_calc_file_str(root / "0000_0005.bin"));
    ASSERT_EQ("18d4f9825c091ae0d5bd938ac5f8bfbf2216a88d9f2fc168fd9513d3ebb5bc12", blake3z_calc_file_str(root / "0000_000b.bin"));
    ASSERT_EQ("1b25ee4bdf3ed17c1cf2ea9dc84147719274b7dc8bce97c7bb1755e56cd1339b", blake3z_calc_file_str(root / "0000_0010.bin"));
    ASSERT_EQ("bb3bda81a664964f0bd3ea39f9231fba21dc8f8271f733ea3ce8092ce36bafaf", blake3z_calc_file_str(root / "0000_0013.bin"));
    ASSERT_EQ("1f124606f97b817be6d04783a94c706288ca7f41216832567af53d5b80e5ab4a", blake3z_calc_file_str(root / "0000_0017.bin"));
}

TEST_F(VBKCommandTest, test_encrypted_vib) {
    const auto vib = find_fixture("Agent Back2025-12-17T101320.vib");

    const auto out_dir = get_out_dir(vib);
    std::filesystem::remove_all(out_dir);
    const auto json_out = out_dir / "enc-vib-test.json";

    run_cmd({"unused", vib.string(), "--password", "12345678", "--test", "--json-file", json_out.string()});
    ASSERT_TRUE(std::filesystem::exists(json_out));

    for_each_line(read_file(json_out), [&](const std::string& line) {
        if (line.empty()) {
            return;
        }
        const auto obj = nlohmann::json::parse(line);
        ASSERT_TRUE(obj.contains("percent"));
        EXPECT_DOUBLE_EQ(100.0, obj.at("percent").get<double>());
    });
}