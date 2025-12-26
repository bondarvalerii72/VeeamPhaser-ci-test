#include "commands/Scan2Command.hpp"
#include "commands/MDCommand.hpp"
#include "test_utils.hpp"

#include <blake3z_file.hpp>
#include <nlohmann/json.hpp>

extern argparse::ArgumentParser program;

class MDCommandTest : public CmdTestBase<MDCommand> {
    protected:
    void SetUp() override {
        register_program_args(program);
        std::filesystem::remove_all(get_out_dir(vbk_fname_str()));
        ASSERT_EQ(1, blake3_open_cache(find_file("_deps/blake3z-src/blake3z.cache").string().c_str()));
    }

    // run scan2 to create 000000001000.slot & 000000081000.slot
    void run_scan2(const std::filesystem::path& fname, const std::string& extra="") const {
        Scan2Command scan_cmd;
        std::vector<std::string> args = {"unused", fname.string()};
        if (!extra.empty()) {
            args.push_back(extra);
        }
        scan_cmd.parser().parse_args(args);
        ASSERT_EQ(0, scan_cmd.run());
    }

    void run_scan2_custom(const std::vector<std::string>& args) const {
        Scan2Command scan_cmd;
        scan_cmd.parser().parse_args(args);
        ASSERT_EQ(0, scan_cmd.run());
    }

    void test_list(const std::initializer_list<VPathOrStr>& args) {
        run_scan2(vbk_fname());
        std::string output = capture_stdout([&](){
            run_cmd(args);
        });

        for_each_line( R"(
            0000:0003 Dir           5        6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)
            0000:0005 IntFib        1 25Kb   6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)/summary.xml
            0000:000b IntFib        1 6Kb    6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)/digest_66d23fcb-9393-439d-972a-972a132f2a9e
            0000:0010 IntFib    e8e0d 931Gb  6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)/5b5c13e8-c84b-40f7-aca6-beb67a10ff29
            0002:0011 IntFib        1 249    6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)/GuestMembers.xml
            0002:0015 IntFib        1 5Kb    6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)/BackupComponents.xml
        )", [&](const std::string& line) {
            EXPECT_THAT(output, HasSubstr(line));
        });
    }

    std::string stripLeadingDotsAndSlashes(const std::string& input) {
        size_t pos = 0;
        while (pos < input.size() && (input[pos] == '.' || input[pos] == '/')) {
            ++pos;
        }
        return input.substr(pos);
    }

    std::map<std::string, nlohmann::json> read_json_file(const fs::path& fname) {
        std::string line;
        std::map<std::string, nlohmann::json> finfos;

        std::ifstream json_file(fname);
        if (!json_file.is_open()) {
            throw std::runtime_error("Failed to open JSON file: " + fname.string());
        }

        while (std::getline(json_file, line)) {
            if (line.empty()) continue;
            
            auto j = nlohmann::json::parse(line);
            j["md_fname"] = stripLeadingDotsAndSlashes(j["md_fname"]);
            finfos[j["id"]] = j;
        }
        return finfos;
    }
};

TEST_F(MDCommandTest, registers_itself) {
    ASSERT_NE(Command::registry()["md"], nullptr);
}

TEST_F(MDCommandTest, list_files_default) {
    test_list({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot")});
}

TEST_F(MDCommandTest, list_files_explicit) {
    test_list({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-l"});
}

TEST_F(MDCommandTest, test_all) {
    run_scan2(vbk_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-t"});
    });
    EXPECT_THAT(output, HasSubstr("OK_BLK"));
    EXPECT_THAT(output, HasSubstr("summary.xml"));
    EXPECT_THAT(output, HasSubstr("GuestMembers.xml"));
    EXPECT_THAT(output, HasSubstr("BackupComponents.xml"));
    EXPECT_THAT(output, HasSubstr("digest_66d23fcb-9393-439d-972a-972a132f2a9e"));
    EXPECT_THAT(output, HasSubstr("5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
    EXPECT_THAT(output, HasSubstr("100.00"));
}

TEST_F(MDCommandTest, test_all_json) {
    run_scan2(vbk_fname());
    const auto json_fname = get_out_pathname(vbk_fname_str(), "out.json");
    std::string output = capture_stdout([&](){
        run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-t", "-j", json_fname});
    });

    auto expected_finfos = read_json_file(find_fixture("AgentBack2024-09-16T163946.vbk.json"));
    ASSERT_GT(expected_finfos.size(), 0) << "Expected at least one file in the input JSON";

    auto actual_finfos = read_json_file(json_fname);

    ASSERT_EQ(expected_finfos.size(), actual_finfos.size());
    for (const auto& [id, expected_info] : expected_finfos) {
        ASSERT_TRUE(actual_finfos.contains(id)) << "Missing file with ID: " << id;
        const auto& actual_info = actual_finfos.at(id);
        ASSERT_EQ(actual_info, expected_info);
    }
}

TEST_F(MDCommandTest, extract_all) {
    run_scan2(vbk_fname());
    run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-x"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
    ASSERT_EQ("bb3bda81a664964f0bd3ea39f9231fba21dc8f8271f733ea3ce8092ce36bafaf", blake3z_calc_file_str(root / "GuestMembers.xml"));
    ASSERT_EQ("9bcb5c34055f04dcc5150a27d9e6348451ec8ed00789b0f69a46ee9acd31c8c0", blake3z_calc_file_str(root / "BackupComponents.xml"));
    ASSERT_EQ("91ccb7a919531839236883237f19e42f75eac56e09ed720301af3a261f2c72be", blake3z_calc_file_str(root / "digest_66d23fcb-9393-439d-972a-972a132f2a9e"));
    ASSERT_EQ("b1c2261fe9e9b7c16bd68dc02bc50aeb5988fccbc13d6a0fb1be06ade28b1071", blake3z_calc_file_str(root / "5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
}

TEST_F(MDCommandTest, extract_all__vbk_offset) {
    run_scan2(vbk_fname());

    {
        std::ifstream fi(vbk_fname(), std::ios::binary);
        std::ofstream fo(get_out_pathname(vbk_fname_str(), "copy.vbk"), std::ios::binary);
        fo.seekp(0x1000);
        fo << fi.rdbuf();
    }

    run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-x", "--vbk", get_out_pathname(vbk_fname_str(), "copy.vbk") , "--vbk-offset", "0x1000"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
    ASSERT_EQ("bb3bda81a664964f0bd3ea39f9231fba21dc8f8271f733ea3ce8092ce36bafaf", blake3z_calc_file_str(root / "GuestMembers.xml"));
    ASSERT_EQ("9bcb5c34055f04dcc5150a27d9e6348451ec8ed00789b0f69a46ee9acd31c8c0", blake3z_calc_file_str(root / "BackupComponents.xml"));
    ASSERT_EQ("91ccb7a919531839236883237f19e42f75eac56e09ed720301af3a261f2c72be", blake3z_calc_file_str(root / "digest_66d23fcb-9393-439d-972a-972a132f2a9e"));
    ASSERT_EQ("b1c2261fe9e9b7c16bd68dc02bc50aeb5988fccbc13d6a0fb1be06ade28b1071", blake3z_calc_file_str(root / "5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
}

TEST_F(MDCommandTest, extract_one_by_name) {
    run_scan2(vbk_fname());
    run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-x", "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)/summary.xml"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
}

TEST_F(MDCommandTest, extract_one_by_id) {
    run_scan2(vbk_fname());
    run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-x", "0000:0005"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
}

TEST_F(MDCommandTest, show_page) {
    run_scan2(vbk_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "--page", "1:0"});
    });
    EXPECT_THAT(output, HasSubstr("00000000: 43 00 00 00 ff ff ff ff ff ff ff ff 00 68 08 c1"));
}

TEST_F(MDCommandTest, show_all_pages) {
    run_scan2(vbk_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "--page", "all"});
    });
    EXPECT_THAT(output, HasSubstr("0001:0000\n    00000000: 43 00 00 00 ff ff ff ff  ff ff ff ff 00 68 08 c1"));
}

TEST_F(MDCommandTest, show_stack) {
    run_scan2(vbk_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "--stack", "0:1"});
    });
    EXPECT_THAT(output, HasSubstr("PageStack[2]{0000:0007, 0002:0010}"));
}

TEST_F(MDCommandTest, patch) {
    run_scan2(vbk_fname());
    run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-x", "0000:0010"});

    run_scan2(vib_fname());
    const auto vib_slot_fname = get_out_pathname(vib_fname_str(), "000000001000.slot");

    program.parse_args(vpath2vstr({"unused", "-o", get_out_dir(vbk_fname_str())}));
    run_cmd({"unused", vib_slot_fname, "-x", "0000:0010"});

    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("019b464af23391c505ed5a93cec2c5027079f224cd80303911e913ee719b4ea8", blake3z_calc_file_str(root / "5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
}

std::string read_file(const fs::path& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}


TEST_F(MDCommandTest, extract_all__csv) {
    run_scan2(vbk_fname(), "--blocks");

    run_cmd({"unused", get_out_pathname(vbk_fname_str(), "000000001000.slot"), "-x", "--device", vbk_fname_str(), "--data", get_out_pathname(vbk_fname_str(), "carved_blocks.csv")});

    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    // summary.xml fails to extract bc it has non-compressed block
    // ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
    ASSERT_EQ("bb3bda81a664964f0bd3ea39f9231fba21dc8f8271f733ea3ce8092ce36bafaf", blake3z_calc_file_str(root / "GuestMembers.xml"));
    ASSERT_EQ("9bcb5c34055f04dcc5150a27d9e6348451ec8ed00789b0f69a46ee9acd31c8c0", blake3z_calc_file_str(root / "BackupComponents.xml"));
    ASSERT_EQ("91ccb7a919531839236883237f19e42f75eac56e09ed720301af3a261f2c72be", blake3z_calc_file_str(root / "digest_66d23fcb-9393-439d-972a-972a132f2a9e"));
    ASSERT_EQ("b1c2261fe9e9b7c16bd68dc02bc50aeb5988fccbc13d6a0fb1be06ade28b1071", blake3z_calc_file_str(root / "5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
}


TEST_F(MDCommandTest, extract_all_multiple_csvs) {
    auto hi_comp_vbk = find_fixture("hi_comp.vbk");
    auto hi_comp_csv1 = find_fixture("hi_comp_1.csv");
    auto hi_comp_csv2 = find_fixture("hi_comp_2.csv");
    run_scan2(hi_comp_vbk, "--blocks");

    run_cmd({"unused", get_out_pathname(hi_comp_vbk, "000000001000.slot"), "-x", "--device", hi_comp_vbk, "--data", hi_comp_csv1,"--device", hi_comp_vbk, "--data", hi_comp_csv2});
    const auto root = get_out_dir(hi_comp_vbk) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)";
    ASSERT_EQ("7c0b0ae630c894ea530df172225a30d8f8daca894624f1a8cfdc65367a133cb8", blake3z_calc_file_str(root / "BackupComponents.xml"));
    ASSERT_EQ("3ebebb74d87f3765569fb3ec6dc98c1201693ab26831f2fea1ff6d012b4c2102", blake3z_calc_file_str(root / "summary.xml"));
    ASSERT_EQ("9da797a430d8641ba6502abf7dc9f93ece007b9edee76910435993f1a596e16c", blake3z_calc_file_str(root / "4a30b6bf-7c1e-4a8f-a45a-c7badd0302ff"));
}

TEST_F(MDCommandTest, extract_missing_slot_file){
    auto slot_null_vbk = find_fixture("slots_null.vbk"); 

    run_scan2(slot_null_vbk);
    run_cmd({"unused", get_out_pathname(slot_null_vbk, "reconstructed_slot.slot"), "-x"});
    const auto root = get_out_dir(slot_null_vbk) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (0b3871d1-8111-40ae-8746-80e0a4093269)";
    ASSERT_EQ("7c0b0ae630c894ea530df172225a30d8f8daca894624f1a8cfdc65367a133cb8", blake3z_calc_file_str(root / "BackupComponents.xml"));
    ASSERT_EQ("3ebebb74d87f3765569fb3ec6dc98c1201693ab26831f2fea1ff6d012b4c2102", blake3z_calc_file_str(root / "summary.xml"));
    ASSERT_EQ("9da797a430d8641ba6502abf7dc9f93ece007b9edee76910435993f1a596e16c", blake3z_calc_file_str(root / "4a30b6bf-7c1e-4a8f-a45a-c7badd0302ff"));
}

TEST_F(MDCommandTest, encrypted_vib_end_to_end) {
    const auto vib = find_fixture("Agent Back2025-12-17T101320.vib");

    const auto out_dir = get_out_dir(vib);
    std::filesystem::remove_all(out_dir);

    run_scan2(vib);
    const auto slot_path = get_out_pathname(vib, "000000001000.slot");
    ASSERT_TRUE(std::filesystem::exists(slot_path));

    run_cmd({"unused", slot_path, "--dump-keysets", "--password", "12345678"});
    const auto keysets_path = slot_path.string() + ".keysets.bin";
    ASSERT_TRUE(std::filesystem::exists(keysets_path));

    run_scan2_custom({"unused", vib.string(), "--keysets", keysets_path, "--blocks"});

    const auto carved_csv = out_dir / "carved_blocks.csv";
    const auto json_out = out_dir / "carved-vib-test.json";
    run_cmd({"unused", slot_path, "--data", carved_csv, "--device", vib, "--password", "12345678", "--json-file", json_out, "--test"});
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


