#pragma once
#include <vector>
#include <cstdint>
#include <functional>

class buf_t : public std::vector<uint8_t> {
    public:
    buf_t() = default;
    buf_t(size_t size) : std::vector<uint8_t>(size) {}
    buf_t(const buf_t& src, size_t size) : std::vector<uint8_t>(src.data(), src.data() + size) {}

    template<typename T>
        void for_each(std::function<bool(T*)> func) {
            size_t step = sizeof(T);
            size_t count = size() / step;

            T* ptr = reinterpret_cast<T*>(data());
            for (size_t i = 0; i < count; ++i) {
                if(!func(ptr++))
                    break;
            }
        }

    template<typename T>
        void for_each_with_index(std::function<bool(T*, size_t)> func) {
            size_t step = sizeof(T);
            size_t count = size() / step;

            T* ptr = reinterpret_cast<T*>(data());
            for (size_t i = 0; i < count; ++i) {
                if(!func(ptr++, i))
                    break;
            }
        }

    bool is_all_zero() const;
};
