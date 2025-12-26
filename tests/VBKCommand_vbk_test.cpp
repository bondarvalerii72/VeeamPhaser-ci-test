#include <blake3z_file.hpp>
#include "commands/VBKCommand.hpp"
#include "test_utils.hpp"

extern argparse::ArgumentParser program;

class VBKCommandTest : public CmdTestBase<VBKCommand> {
    protected:
    void SetUp() override {
        register_program_args(program);
        std::filesystem::remove_all(get_out_dir(vbk_fname_str()));
        ASSERT_EQ(1, blake3_open_cache(find_file("_deps/blake3z-src/blake3z.cache").string().c_str()));
    }
};

TEST_F(VBKCommandTest, registers_itself) {
    ASSERT_NE(Command::registry()["vbk"], nullptr);
}

TEST_F(VBKCommandTest, default) {
    std::string output = capture_stdout([&](){
        run_cmd({"unused", vbk_fname_str()});
    });
    EXPECT_THAT(output, HasSubstr("<FileHeader version: d, inited: 1, digest_type_len: 3, digest_type: \"md5\", slot_fmt: 9, std_block_size: 100000, cluster_align: 3>"));
    EXPECT_THAT(output, HasSubstr("<CSlot crc=760ef101, has_snapshot=1, max_banks=7f00, allocated_banks=3 size=7f080>"));
    EXPECT_THAT(output, HasSubstr("<SnapshotDescriptor version=15, storage_eof=210b000, nBanks=3, objRefs=<ObjRefs MetaRootDirPage=0000:0000, children_num=1, DataStoreRootPage=0000:0001, BlocksCount=47, free_blocks_root=0000:0002, dedup_root=0001:0000>>"));
    EXPECT_THAT(output, HasSubstr("<BankInfo crc=bb94f308, offset=      101000, size=  22000>"));
    EXPECT_THAT(output, HasSubstr("<BankInfo crc=d925a0dd, offset=      123000, size=  22000>"));
    EXPECT_THAT(output, HasSubstr("<BankInfo crc=cf25f173, offset=     2078000, size=  42000>"));

    EXPECT_THAT(output, HasSubstr("<CSlot crc=e87e48c8, has_snapshot=1, max_banks=7f00, allocated_banks=3 size=7f080>"));
    EXPECT_THAT(output, HasSubstr("<BankInfo crc=bb94f308, offset=      145000, size=  22000>"));
    EXPECT_THAT(output, HasSubstr("<BankInfo crc=d925a0dd, offset=      167000, size=  22000>"));
    EXPECT_THAT(output, HasSubstr("<BankInfo crc=cf25f173, offset=     20ba000, size=  42000>"));
}

TEST_F(VBKCommandTest, list_files) {
    std::string output = capture_stdout([&](){
        run_cmd({"unused", vbk_fname_str(), "-l"});
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

TEST_F(VBKCommandTest, test_all) {
    std::string output = capture_stdout([&](){
        run_cmd({"unused", vbk_fname_str(), "-t"});
    });
    EXPECT_THAT(output, HasSubstr("OK_BLK"));
    EXPECT_THAT(output, HasSubstr("summary.xml"));
    EXPECT_THAT(output, HasSubstr("GuestMembers.xml"));
    EXPECT_THAT(output, HasSubstr("BackupComponents.xml"));
    EXPECT_THAT(output, HasSubstr("digest_66d23fcb-9393-439d-972a-972a132f2a9e"));
    EXPECT_THAT(output, HasSubstr("5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
    EXPECT_THAT(output, HasSubstr("100.00"));
}

TEST_F(VBKCommandTest, test_all_zlib) {
    std::string output = capture_stdout([&](){
        run_cmd({"unused", find_fixture("hi_comp.vbk"), "-t"});
    });
    EXPECT_THAT(output, HasSubstr("OK_BLK"));
    EXPECT_THAT(output, HasSubstr("summary.xml"));
    EXPECT_THAT(output, HasSubstr("4a30b6bf-7c1e-4a8f-a45a-c7badd0302ff"));
    EXPECT_THAT(output, HasSubstr("BackupComponents.xml"));
    EXPECT_THAT(output, HasSubstr("100.00"));
}

void check_all_files(const std::filesystem::path& root) {
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
    ASSERT_EQ("91ccb7a919531839236883237f19e42f75eac56e09ed720301af3a261f2c72be", blake3z_calc_file_str(root / "digest_66d23fcb-9393-439d-972a-972a132f2a9e"));
    ASSERT_EQ("b1c2261fe9e9b7c16bd68dc02bc50aeb5988fccbc13d6a0fb1be06ade28b1071", blake3z_calc_file_str(root / "5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
    ASSERT_EQ("bb3bda81a664964f0bd3ea39f9231fba21dc8f8271f733ea3ce8092ce36bafaf", blake3z_calc_file_str(root / "GuestMembers.xml"));
    ASSERT_EQ("9bcb5c34055f04dcc5150a27d9e6348451ec8ed00789b0f69a46ee9acd31c8c0", blake3z_calc_file_str(root / "BackupComponents.xml"));
}

TEST_F(VBKCommandTest, extract_all) {
    run_cmd({"unused", vbk_fname_str(), "-x"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    check_all_files(root);
}

TEST_F(VBKCommandTest, extract_all__first_slot_destroyed) {
    const auto copy_fname = get_out_pathname(vbk_fname_str(), "copy.vbk");
    std::filesystem::copy(vbk_fname_str(), copy_fname);

    {
    char buf[0x1000];
    memset(buf, 0, sizeof(buf));

    std::ofstream f(copy_fname, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(0x1000);
    f.write(buf, sizeof(buf));
    }
    
    run_cmd({"unused", copy_fname, "-x"});
    const auto root = get_out_dir(copy_fname.string()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    check_all_files(root);
}

TEST_F(VBKCommandTest, extract_all__second_slot_destroyed) {
    const auto copy_fname = get_out_pathname(vbk_fname_str(), "copy.vbk");
    std::filesystem::copy(vbk_fname_str(), copy_fname);

    {
    char buf[0x1000];
    memset(buf, 0, sizeof(buf));

    std::ofstream f(copy_fname, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(0x81000);
    f.write(buf, sizeof(buf));
    }
    
    run_cmd({"unused", copy_fname, "-x"});
    const auto root = get_out_dir(copy_fname.string()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    check_all_files(root);
}

TEST_F(VBKCommandTest, extract_all__both_slots_destroyed) {
    const auto copy_fname = get_out_pathname(vbk_fname_str(), "copy.vbk");
    std::filesystem::copy(vbk_fname_str(), copy_fname);

    {
    char buf[0x1000];
    memset(buf, 0, sizeof(buf));

    std::ofstream f(copy_fname, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(0x1000);
    f.write(buf, sizeof(buf));
    f.seekp(0x81000);
    f.write(buf, sizeof(buf));
    }
    
    std::string output = capture_stdout([&](){
        run_cmd({"unused", copy_fname, "-x"}, 1);
    });

    EXPECT_THAT(output, HasSubstr("no valid slots found"));
}

///////////////////////////////////////////////////////////////////////////////

std::filesystem::path no_dir_fname(const std::filesystem::path& vbk_fname) {
    const auto copy_fname = get_out_pathname(vbk_fname.string(), "no_dir.vbk");
    std::filesystem::copy(vbk_fname, copy_fname);

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
    const auto copy_fname = no_dir_fname(vbk_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", copy_fname, "-l", "--deep"});
    });

    for_each_line( R"(
        0000:0005 IntFib        1 25Kb   0000_0005.bin
        0000:000b IntFib        1 6Kb    0000_000b.bin
        0000:0010 IntFib    e8e0d 931Gb  0000_0010.bin
        0002:0011 IntFib        1 249    0002_0011.bin
        0002:0015 IntFib        1 5Kb    0002_0015.bin
    )", [&](const std::string& line) {
        EXPECT_THAT(output, HasSubstr(line));
    });
}

TEST_F(VBKCommandTest, dir_destroyed__test_all) {
    const auto copy_fname = no_dir_fname(vbk_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", copy_fname, "-t", "--deep"});
    });
    EXPECT_THAT(output, HasSubstr("OK_BLK"));
    EXPECT_THAT(output, HasSubstr("0000_0005.bin"));
    EXPECT_THAT(output, HasSubstr("0000_000b.bin"));
    EXPECT_THAT(output, HasSubstr("0000_0010.bin"));
    EXPECT_THAT(output, HasSubstr("0002_0011.bin"));
    EXPECT_THAT(output, HasSubstr("0002_0015.bin"));
    EXPECT_THAT(output, HasSubstr("100.00"));
}

TEST_F(VBKCommandTest, dir_destroyed__extract_all) {
    const auto copy_fname = no_dir_fname(vbk_fname());
    
    run_cmd({"unused", copy_fname, "-x", "--deep"});

    const auto root = get_out_dir(vbk_fname_str());
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "0000_0005.bin"));
    ASSERT_EQ("91ccb7a919531839236883237f19e42f75eac56e09ed720301af3a261f2c72be", blake3z_calc_file_str(root / "0000_000b.bin"));
    ASSERT_EQ("b1c2261fe9e9b7c16bd68dc02bc50aeb5988fccbc13d6a0fb1be06ade28b1071", blake3z_calc_file_str(root / "0000_0010.bin"));
    ASSERT_EQ("bb3bda81a664964f0bd3ea39f9231fba21dc8f8271f733ea3ce8092ce36bafaf", blake3z_calc_file_str(root / "0002_0011.bin"));
    ASSERT_EQ("9bcb5c34055f04dcc5150a27d9e6348451ec8ed00789b0f69a46ee9acd31c8c0", blake3z_calc_file_str(root / "0002_0015.bin"));
}

TEST_F(VBKCommandTest, dir_destroyed__extract_named) {
    const auto copy_fname = no_dir_fname(vbk_fname());
    
    run_cmd({"unused", copy_fname, "--deep", "-x", "0000_0005.bin"});

    const auto root = get_out_dir(vbk_fname_str());
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "0000_0005.bin"));
}

TEST_F(VBKCommandTest, dir_destroyed__extract_glob) {
    const auto copy_fname = no_dir_fname(vbk_fname());
    
    run_cmd({"unused", copy_fname, "--deep", "-x", "0000_0005*"});

    const auto root = get_out_dir(vbk_fname_str());
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "0000_0005.bin"));
}

///////////////////////////////////////////////////////////////////////////////

std::filesystem::path no_parent_dir_fname(const std::filesystem::path& vbk_fname) {
    const auto copy_fname = get_out_pathname(vbk_fname.string(), "no_parent_dir.vbk");
    std::filesystem::copy(vbk_fname, copy_fname);

    char buf[0x1000];
    memset(buf, 0, sizeof(buf));

    std::ofstream f(copy_fname, std::ios::binary | std::ios::in | std::ios::out);
    f.seekp(0x106000);
    f.write(buf, sizeof(buf));
    f.seekp(0x14a000);
    f.write(buf, sizeof(buf));

    return copy_fname;
}

TEST_F(VBKCommandTest, parent_dir_destroyed__list_files) {
    const auto copy_fname = no_parent_dir_fname(vbk_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", copy_fname, "-l"});
    });

    for_each_line( R"(
        0000:0005 IntFib        1 25Kb   summary.xml
        0000:000b IntFib        1 6Kb    digest_66d23fcb-9393-439d-972a-972a132f2a9e
        0000:0010 IntFib    e8e0d 931Gb  5b5c13e8-c84b-40f7-aca6-beb67a10ff29
        0002:0011 IntFib        1 249    GuestMembers.xml
        0002:0015 IntFib        1 5Kb    BackupComponents.xml
    )", [&](const std::string& line) {
        EXPECT_THAT(output, HasSubstr(line));
    });
}

TEST_F(VBKCommandTest, parent_dir_destroyed__test_all) {
    const auto copy_fname = no_parent_dir_fname(vbk_fname());
    std::string output = capture_stdout([&](){
        run_cmd({"unused", copy_fname, "-t"});
    });
    EXPECT_THAT(output, HasSubstr("OK_BLK"));
    EXPECT_THAT(output, HasSubstr("summary.xml"));
    EXPECT_THAT(output, HasSubstr("GuestMembers.xml"));
    EXPECT_THAT(output, HasSubstr("BackupComponents.xml"));
    EXPECT_THAT(output, HasSubstr("digest_66d23fcb-9393-439d-972a-972a132f2a9e"));
    EXPECT_THAT(output, HasSubstr("5b5c13e8-c84b-40f7-aca6-beb67a10ff29"));
    EXPECT_THAT(output, HasSubstr("100.00"));
}

TEST_F(VBKCommandTest, parent_dir_destroyed__extract_all) {
    const auto copy_fname = no_parent_dir_fname(vbk_fname());
    
    run_cmd({"unused", copy_fname, "-x"});

    const auto root = get_out_dir(vbk_fname_str());
    check_all_files(root);
}

TEST_F(VBKCommandTest, parent_dir_destroyed__extract_named) {
    const auto copy_fname = no_parent_dir_fname(vbk_fname());
    
    run_cmd({"unused", copy_fname, "-x", "summary.xml"});

    const auto root = get_out_dir(vbk_fname_str());
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
}

TEST_F(VBKCommandTest, parent_dir_destroyed__extract_glob) {
    const auto copy_fname = no_parent_dir_fname(vbk_fname());
    
    run_cmd({"unused", copy_fname, "-x", "summary.*"});

    const auto root = get_out_dir(vbk_fname_str());
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
}

///////////////////////////////////////////////////////////////////////////////

TEST_F(VBKCommandTest, extract_by_full_name) {
    run_cmd({"unused", vbk_fname_str(), "-x", "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)/summary.xml"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
}

TEST_F(VBKCommandTest, extract_by_short_name) {
    run_cmd({"unused", vbk_fname_str(), "-x", "summary.xml"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
}

TEST_F(VBKCommandTest, extract_by_glob) {
    run_cmd({"unused", vbk_fname_str(), "-x", "*/summary.xml"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
}

TEST_F(VBKCommandTest, extract_by_id) {
    run_cmd({"unused", vbk_fname_str(), "-x", "0000:0005"});
    const auto root = get_out_dir(vbk_fname_str()) / "6745a759-2205-4cd2-b172-8ec8f7e60ef8 (075920a5-8905-ff57-696f-b06ebfc92287)";
    ASSERT_EQ("cc7922e1d25516a083a4b2ea8c4cfdbfab83c3d8c33f9450037810dd440118e5", blake3z_calc_file_str(root / "summary.xml"));
}

TEST_F(VBKCommandTest, show_page) {
    std::string output = capture_stdout([&](){
        run_cmd({"unused", vbk_fname_str(), "--page", "1:0"});
    });
    EXPECT_THAT(output, HasSubstr("00000000: 43 00 00 00 ff ff ff ff ff ff ff ff 00 68 08 c1"));
}

TEST_F(VBKCommandTest, show_stack) {
    std::string output = capture_stdout([&](){
        run_cmd({"unused", vbk_fname_str(), "--stack", "0:1"});
    });
    EXPECT_THAT(output, HasSubstr("PageStack[2]{0000:0007, 0002:0010}"));
}
