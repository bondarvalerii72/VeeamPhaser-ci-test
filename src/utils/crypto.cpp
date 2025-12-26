#include "crypto.hpp"
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <stdexcept>
#include <memory>
#include <cstring>


#if !defined(__x86_64__) && !defined(_M_X64)
#error "Only x86-64 is supported"
#endif

#include <immintrin.h>
#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <cpuid.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define AES_TARGET __attribute__((target("aes")))
#else
#define AES_TARGET
#endif

namespace crypto {

std::vector<uint8_t> pbkdf2(const std::string& password, const std::vector<uint8_t>& salt,int iterations, int key_length, bool use_sha1) {
    // We do not support pw with non-ascii char
    std::vector<uint8_t> utf16_password;
    for (char c : password) {
        utf16_password.push_back(static_cast<uint8_t>(c));
        utf16_password.push_back(0);
    }
    
    std::vector<uint8_t> result(key_length);
    
    const EVP_MD* md = use_sha1 ? EVP_sha1() : EVP_sha256();
    
    if (PKCS5_PBKDF2_HMAC(
            reinterpret_cast<const char*>(utf16_password.data()), utf16_password.size(),
            salt.data(), salt.size(),
            iterations,
            md,
            key_length,
            result.data()) != 1) {
        throw std::runtime_error("PBKDF2 failed");
    }
    
    return result;
}

// This func will bruteforce all known pbkdf2 configs used by veeam to decrypt the key.
std::vector<uint8_t> decrypt_pbkdf2_data(const std::string& password, const std::vector<uint8_t>& salt,  const std::vector<uint8_t>& encrypted_data) {
    const int configs[][2] = {{600000, 0}, {310000, 0}, {10000, 1}}; // {iterations, is_sha1}
    
    for (const auto& config : configs) {
        try {
            auto result = pbkdf2(password, salt, config[0], 48, config[1]);
            std::vector<uint8_t> key_data(result.data(), result.data() + 32);
            std::vector<uint8_t> iv_data(result.data() + 32, result.data() + 48);

            std::vector<uint8_t> decrypted = encrypted_data;

            AES256 aes(key_data.data(), iv_data.data());
            aes.decrypt(decrypted);
            return decrypted;
        } catch (const std::exception&) {
            // try next config
        }
    }
    
    throw std::runtime_error("All PBKDF2 decryption attempts failed");
}

std::vector<uint8_t> rsa_decrypt(const std::string& private_key_pem, const std::vector<uint8_t>& encrypted_data) {
                                    
    BIO* bio = BIO_new_mem_buf(private_key_pem.data(), private_key_pem.size());
    if (!bio) throw std::runtime_error("Failed to create BIO");
    
    EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey) throw std::runtime_error("Failed to read RSA key");
    
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new(pkey, nullptr);
    if (!ctx || EVP_PKEY_decrypt_init(ctx) <= 0 || 
        EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0) {
        if (ctx) EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("Failed to init RSA decrypt");
    }
    
    size_t out_len = 0;
    if (EVP_PKEY_decrypt(ctx, nullptr, &out_len, encrypted_data.data(), encrypted_data.size()) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("Failed to get decrypt size");
    }
    
    std::vector<uint8_t> result(out_len);
    if (EVP_PKEY_decrypt(ctx, result.data(), &out_len, encrypted_data.data(), encrypted_data.size()) <= 0) {
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(pkey);
        throw std::runtime_error("rsa decrypt failed");
    }
    
    result.resize(out_len);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return result;
}

namespace {

bool aesni_supported() {
#if defined(__x86_64__) || defined(_M_X64)
    #if defined(_MSC_VER)
        int regs[4] = {0, 0, 0, 0};
        __cpuid(regs, 1);
        return (regs[2] & (1 << 25)) != 0;
    #else
        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
        if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
                return false;
        return (ecx & bit_AES) != 0;
    #endif
#else
        return false;
#endif
}

size_t pkcs7_unpad_len(const uint8_t* buf, size_t len) {
    if (len == 0)
        return 0;

    const uint8_t pad = buf[len - 1];
    if (pad == 0 || pad > 16 || pad > len)
        throw std::runtime_error("invalid PKCS#7 padding");

    for (size_t i = len - pad; i < len; ++i) {
        if (buf[i] != pad)
            throw std::runtime_error("invalid PKCS#7 padding");
    }

    return static_cast<size_t>(pad);
}


AES_TARGET static inline __m128i xor3_slli(__m128i x) {
    x = _mm_xor_si128(x, _mm_slli_si128(x, 4));
    x = _mm_xor_si128(x, _mm_slli_si128(x, 4));
    x = _mm_xor_si128(x, _mm_slli_si128(x, 4));
    return x;
}

AES_TARGET static inline void expand_round(__m128i& k0, __m128i& k1, int rcon, __m128i enc[15], int& idx) {
    __m128i t = _mm_aeskeygenassist_si128(k1, rcon);
    t = _mm_shuffle_epi32(t, 0xff);
    k0 = _mm_xor_si128(xor3_slli(k0), t);
    enc[idx++] = k0;

    t = _mm_aeskeygenassist_si128(k0, 0x00);
    t = _mm_shuffle_epi32(t, 0xaa);
    k1 = _mm_xor_si128(xor3_slli(k1), t);
    enc[idx++] = k1;
}

AES_TARGET static inline void expand_enc_keys_256(const uint8_t key[32], __m128i enc[15]) {
    __m128i k0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
    __m128i k1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key + 16));

    enc[0] = k0;
    enc[1] = k1;

    int idx = 2;

    expand_round(k0, k1, 0x01, enc, idx);
    expand_round(k0, k1, 0x02, enc, idx);
    expand_round(k0, k1, 0x04, enc, idx);
    expand_round(k0, k1, 0x08, enc, idx);
    expand_round(k0, k1, 0x10, enc, idx);
    expand_round(k0, k1, 0x20, enc, idx);

    __m128i t = _mm_aeskeygenassist_si128(k1, 0x40);
    t = _mm_shuffle_epi32(t, 0xff);
    k0 = _mm_xor_si128(xor3_slli(k0), t);
    enc[idx++] = k0;
}

AES_TARGET static inline void make_dec_keys_256(const __m128i enc[15], __m128i dec[15]) {
    dec[0] = enc[14];
    for (int i = 1; i < 14; ++i)
        dec[i] = _mm_aesimc_si128(enc[14 - i]);
    dec[14] = enc[0];
}

AES_TARGET static inline __m128i aes256_dec_block(__m128i s, const __m128i* dk) {
    s = _mm_xor_si128(s, dk[0]);
    for (int i = 1; i < 14; ++i)
        s = _mm_aesdec_si128(s, dk[i]);
    return _mm_aesdeclast_si128(s, dk[14]);
}


}

AES_TARGET AES256::AES256(const uint8_t key[32], const uint8_t iv[16]) {
    if (!aesni_supported())
        throw std::runtime_error("AES-NI not supported on this CPU");

    std::memcpy(iv0_, iv, 16);
    __m128i enc[15];
    __m128i dec[15];
    expand_enc_keys_256(key, enc);
    make_dec_keys_256(enc, dec);

    for (int i = 0; i < 15; ++i)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dec_keys_[i]), dec[i]);
}

AES_TARGET void AES256::decrypt(std::vector<uint8_t>& data, bool remove_padding, size_t size) const {
    const size_t n = (size == 0) ? data.size() : size;
    if (n == 0)
        return;
    if (n > data.size())
        throw std::runtime_error("decrypt size out of range");
    if ((n & 15) != 0)
        throw std::runtime_error("aes input size must be a multiple of 16");

    const __m128i* dk = reinterpret_cast<const __m128i*>(dec_keys_);
    __m128i prev = _mm_loadu_si128(reinterpret_cast<const __m128i*>(iv0_));
    uint8_t* p = data.data();

    for (size_t off = 0; off < n; off += 16) {
        if (off + 64 < n) // pre-fetch next blocks for a performance boost.
            _mm_prefetch(reinterpret_cast<const char*>(p + off + 64), _MM_HINT_T0);

        const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(p + off));
        __m128i x = aes256_dec_block(c, dk);
        x = _mm_xor_si128(x, prev);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(p + off), x);
        prev = c;
    }

    if (remove_padding) {
        const size_t pad = pkcs7_unpad_len(data.data(), n);
        data.resize(n - pad);
    }
}
// overload for raw buffer
AES_TARGET void AES256::decrypt(uint8_t* data, size_t len, bool remove_padding) const {
    if (len == 0)
        return;
    if ((len & 15) != 0)
        throw std::runtime_error("aes input size must be a multiple of 16");

    if (remove_padding)
        throw std::runtime_error("remove_padding not supported for raw buffer decrypt");

    const __m128i* dk = reinterpret_cast<const __m128i*>(dec_keys_);
    __m128i prev = _mm_loadu_si128(reinterpret_cast<const __m128i*>(iv0_));

    for (size_t off = 0; off < len; off += 16) {
        if (off + 64 < len)
            _mm_prefetch(reinterpret_cast<const char*>(data + off + 64), _MM_HINT_T0);

        const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + off));
        __m128i x = aes256_dec_block(c, dk);
        x = _mm_xor_si128(x, prev);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(data + off), x);
        prev = c;
    }
}


// just for convenience
void register_keyset(std::map<Veeam::VBK::digest_t, aes_key>& keys,
                     std::map<Veeam::VBK::digest_t, std::unique_ptr<AES256>>& ciphers,
                     const Veeam::VBK::digest_t& id,
                     const aes_key& key) {
    auto cipher = std::make_unique<AES256>(key.key, key.iv);
    keys[id] = key;
    ciphers[id] = std::move(cipher);
}

}
