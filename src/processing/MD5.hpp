#include <openssl/evp.h>
#include "utils/common.hpp"

class MD5 {
    public:
    static constexpr size_t DIGEST_LENGTH = 16;

    MD5();
    ~MD5();

    digest_t Calculate(const void* data, size_t size);
    inline digest_t Calculate(const buf_t& buf) { return Calculate(buf.data(), buf.size()); }

    // one-shot calculation
    inline static digest_t Calc(const buf_t& data) {
        MD5 md5;
        return md5.Calculate(data);
    }

    private:
    EVP_MD_CTX *m_ctx = nullptr;
};

static_assert(sizeof(digest_t) == MD5::DIGEST_LENGTH, "digest_t must be 16 bytes for MD5");
