#include "test_utils.hpp"
#include <gtest/gtest.h>

fs::path find_file(const char* name) {
    fs::path path = ".";
    for(int i=0; i<3; i++) {
        if(std::filesystem::exists(path / name)) {
            break;
        }
        path /= "..";
    }
    return path / name;
}

fs::path find_fixture(const char* name) {
    return find_file(("tests/fixtures/" + std::string(name)).c_str());
}

int count_files_with_extension(const fs::path& directory, const std::string& extension) {
    int count = 0;
    if (fs::exists(directory) && fs::is_directory(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (entry.is_regular_file() && entry.path().extension() == extension) {
                ++count;
            }
        }
    }
    return count;
}

std::string capture_stdout(const std::function<void()>& func) {
    testing::internal::CaptureStdout();
    func();
    return testing::internal::GetCapturedStdout();
}

std::vector<std::string> split(const std::string& str, const char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);

    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }

    return tokens;
}

std::string trim(const std::string& str) {
    std::string token = str;
    // Trim leading and trailing whitespace
    token.erase(0, token.find_first_not_of(" \t\n\r"));
    token.erase(token.find_last_not_of(" \t\n\r") + 1);
    return token;
}

void for_each_line(const std::string& str, const std::function<void(const std::string& line)>& func) {
    for (const auto& line : split(str, '\n')) {
        func(trim(line));
    }
}

std::vector<std::string> vpath2vstr(const std::initializer_list<VPathOrStr>& vpaths) {
    std::vector<std::string> result;
    result.reserve(vpaths.size());
    for (const auto& vpath : vpaths) {
        if (std::holds_alternative<std::string>(vpath)) {
            result.push_back(std::get<std::string>(vpath));
        } else if (std::holds_alternative<std::filesystem::path>(vpath)) {
            result.push_back(std::get<std::filesystem::path>(vpath).string());
        } else {
            result.push_back(std::get<const char*>(vpath));
        }
    }
    return result;
}
