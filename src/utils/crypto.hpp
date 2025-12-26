#pragma once

#include <openssl/err.h> 
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <cstdint>
#include <cstddef>
#include <map>
#include <memory>
#include <vector>
#include <string>

#include "Veeam/VBK.hpp"

namespace crypto {

struct aes_key {
    uint8_t key[32];
    uint8_t iv[16];
};

std::vector<uint8_t> pbkdf2(const std::string& password, 
                             const std::vector<uint8_t>& salt,
                             int iterations, 
                             int key_length,
                             bool use_sha1 = false);

std::vector<uint8_t> decrypt_pbkdf2_data(const std::string& password,
                                          const std::vector<uint8_t>& salt,
                                          const std::vector<uint8_t>& enc_data);

std::vector<uint8_t> rsa_decrypt(const std::string& private_key_pem,
                                  const std::vector<uint8_t>& encrypted_data);

class AES256 {
public:
    AES256(const uint8_t key[32], const uint8_t iv[16]);
    void decrypt(std::vector<uint8_t>& data, bool remove_padding = true, size_t size = 0) const;
    void decrypt(uint8_t* data, size_t len, bool remove_padding = false) const;

private:
    alignas(16) uint8_t iv0_[16];
    alignas(16) unsigned char dec_keys_[15][16];
};

void register_keyset(std::map<Veeam::VBK::digest_t, aes_key>& keys,
                     std::map<Veeam::VBK::digest_t, std::unique_ptr<AES256>>& ciphers,
                     const Veeam::VBK::digest_t& id,
                     const aes_key& key);

}

