#include <string>
#include <vector>
#include <cstdint>

void hexdump(const void *ptr, size_t buflen, const char* prefix="");
void hexdump(const std::string& str, const char* prefix = "");
void hexdump(const std::vector<uint8_t>& str, const char* prefix = "");
