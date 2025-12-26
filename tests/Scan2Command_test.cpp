#include <gtest/gtest.h>
#include <blake3z_file.hpp>
#include "commands/Scan2Command.hpp"
#include "test_utils.hpp"

extern argparse::ArgumentParser program;

class Scan2CommandTest : public CmdTestBase<Scan2Command> {
    protected:
    void SetUp() override {
        register_program_args(program);
        cmd = new Scan2Command();
    }

    void TearDown() override {
        delete cmd;
    }

    Scan2Command* cmd;
};

TEST_F(Scan2CommandTest, registers_itself) {
    ASSERT_NE(Command::registry()["scan"], nullptr);
}

TEST_F(Scan2CommandTest, scan_vbk) {
    const std::string fname = vbk_fname_str();
    std::filesystem::remove_all(get_out_dir(fname));

    cmd->parser().parse_args({"unused", fname});
    ASSERT_EQ(0, cmd->run());

    // finds 2 slots (only 1 after dedup)
    ASSERT_EQ(1, count_files_with_extension(get_out_dir(fname), ".slot"));

    // creates 000000001000.slot
    ASSERT_EQ("5e0665d95763929f627ec021648f93671658240d61b5ebce4d822001b56f4ae7", blake3z_calc_file_str(get_out_dir(fname) / "000000001000.slot"));

    // creates 000000081000.slot
    //ASSERT_EQ("75806d13bc091c77d6fc6907d781fba8374a1cb93d9bb1324a6876b0a7635555", blake3z_calc_file_str(get_out_dir(fname) / "000000081000.slot"));
}

std::string read_file(const fs::path& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open file: " + path.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST_F(Scan2CommandTest, scan_vbk_blocks) {
    const std::string fname = vbk_fname_str();
    std::filesystem::remove_all(get_out_dir(fname));

    cmd->parser().parse_args({"unused", fname, "--blocks"});
    ASSERT_EQ(0, cmd->run());

    ASSERT_EQ(read_file(find_fixture("AgentBack2024-09-16T163946.vbk.csv")), read_file(get_out_dir(fname) / "carved_blocks.csv"));
}

TEST_F(Scan2CommandTest, scan_vbk_blocks_zlib) {
    const std::string fname = find_fixture("hi_comp.vbk").string();
    std::filesystem::remove_all(get_out_dir(fname));

    cmd->parser().parse_args({"unused", fname, "--blocks"});
    ASSERT_EQ(0, cmd->run());

    ASSERT_EQ(read_file(find_fixture("hi_comp.vbk.csv")), read_file(get_out_dir(fname) / "carved_blocks.csv"));
}

TEST_F(Scan2CommandTest, scan_vib) {
    const std::string fname = vib_fname_str();
    std::filesystem::remove_all(get_out_dir(fname));

    cmd->parser().parse_args({"unused", fname});
    ASSERT_EQ(0, cmd->run());

    // finds 2 slots (only 1 after dedup code)
    ASSERT_EQ(1, count_files_with_extension(get_out_dir(fname), ".slot"));

    // creates 000000001000.slot
    ASSERT_EQ("76cae49dcc2c0ec312291708adcc37c06959e93bb93d0cadb25493ce29813105", blake3z_calc_file_str(get_out_dir(fname) / "000000001000.slot"));

    // creates 000000081000.slot
    //ASSERT_EQ("2525aacc5ad357f44848e957137ac3852dd7f8a022ea7a2542dca31891404a57", blake3z_calc_file_str(get_out_dir(fname) / "000000081000.slot"));
}
