#pragma once
#include <list>
#include <unordered_map>
#include <cstddef>
#include <functional>

template <typename T>
class lru_set {
public:
    using key_type = T;
    using size_type = std::size_t;

    explicit lru_set(size_type max_capacity)
        : _capacity(max_capacity) {}

    bool contains(const key_type& key) {
        auto it = _map.find(key);
        if (it == _map.end()) return false;

        // Move to front (MRU)
        _list.splice(_list.begin(), _list, it->second);
        return true;
    }

    void insert(const key_type& key) {
        auto it = _map.find(key);
        if (it != _map.end()) {
            _list.splice(_list.begin(), _list, it->second); // promote
            return;
        }

        if (_map.size() >= _capacity) {
            const key_type& lru_key = _list.back();
            _map.erase(lru_key);
            _list.pop_back();
        }

        _list.push_front(key);
        _map[key] = _list.begin();
    }

    bool empty() const noexcept { return _map.empty(); }
    size_type size() const noexcept { return _map.size(); }
    size_type capacity() const noexcept { return _capacity; }

    void clear() {
        _map.clear();
        _list.clear();
    }

private:
    size_type _capacity;
    std::list<key_type> _list; // front = MRU
    std::unordered_map<key_type, typename std::list<key_type>::iterator> _map;
};

