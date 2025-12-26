#pragma once
#include <cstdint>
#include <string>

std::string bytes2human(uint64_t size, const char* default_unit = "", uint64_t min_unit = 1);
uint64_t human2bytes(const std::string& size);

std::string seconds2human(uint64_t seconds, size_t maxUnits = 2);
