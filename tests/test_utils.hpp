#pragma once
#include <filesystem>
#include <functional>
#include <variant>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "utils/common.hpp"

using testing::HasSubstr;

std::filesystem::path find_file(const char* name);
std::filesystem::path find_fixture(const char* name);
int count_files_with_extension(const std::filesystem::path& directory, const std::string& extension);
std::string capture_stdout(const std::function<void()>& func);
std::vector<std::string> split(const std::string& str, const char delimiter);
std::string trim(const std::string& str);
void for_each_line(const std::string& str, const std::function<void(const std::string& line)>& func);

// std::filesystem::path is not convertible to std::string on windows, so we need these
typedef std::variant<std::string, std::filesystem::path, const char*> VPathOrStr;
std::vector<std::string> vpath2vstr(const std::initializer_list<VPathOrStr>& vpaths);

template <typename TCmd>
class CmdTestBase : public ::testing::Test {
    protected:

    std::filesystem::path vbk_fname() const {
        return find_fixture("AgentBack2024-09-16T163946.vbk");
    }
    std::filesystem::path vib_fname() const {
        return find_fixture("AgentBack2024-09-16T164908.vib");
    }

    // std::filesystem::path is not convertible to std::string on windows, so we need these
    std::string vbk_fname_str() const {
        return vbk_fname().string();
    }
    std::string vib_fname_str() const {
        return vib_fname().string();
    }

    void run_cmd(const std::initializer_list<VPathOrStr>& args, int expected_code = 0) {
        TCmd cmd;
        std::vector<std::string> vargs = vpath2vstr(args);
        logger->set_arguments(vargs);
#ifdef __WIN32__
        // don't write to default log file on windows because it then fails to delete a .out directory, containing the log
        init_log("", "/dev/null");
#endif
        cmd.parser().parse_args(vargs);
        EXPECT_EQ(expected_code, cmd.run());
    }
};
