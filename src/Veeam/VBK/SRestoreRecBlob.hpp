#pragma once
#include <cstdint>

namespace Veeam::VBK {

struct __attribute__((packed)) SRestoreRecBlob {
    static const uint64_t MAGIC = 0xFFFFFFFFFFFFFFFF;

    uint64_t minus_one;
    uint32_t keyrec_size;
    uint32_t unk1;
    uint32_t one;
    uint32_t keyset_id_size;
    uint8_t keyset_id[16];
    uint32_t enc_key_size;
    uint16_t unk2;
    uint16_t unk3;
    uint32_t unk4;
    uint16_t unk5;
    uint32_t encrypted_key_size;
    uint32_t key_checksum_size;
    uint32_t salt_size;
    // encrypted_key[encrypted_key_size]
    // key_checksum[key_checksum_size]
    // salt[salt_size]

    const uint8_t* encrypted_key() const {
        return reinterpret_cast<const uint8_t*>(this + 1);
    }

    const uint8_t* key_checksum() const {
        return encrypted_key() + encrypted_key_size;
    }

    const uint8_t* salt() const {
        return key_checksum() + key_checksum_size;
    }

    bool is_pbkdf2_derived() const {
        return salt_size != 0 && encrypted_key_size != 0 && key_checksum_size != 0;
    }

    bool valid() const {
        return minus_one == MAGIC && one == 1 && keyset_id_size == 16;
    }

    std::string keyset_id_str() const {
        return fmt::format("{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", keyset_id[0], keyset_id[1], keyset_id[2], keyset_id[3], keyset_id[4], keyset_id[5], keyset_id[6], keyset_id[7], keyset_id[8], keyset_id[9], keyset_id[10], keyset_id[11], keyset_id[12], keyset_id[13], keyset_id[14], keyset_id[15]);
    }

    std::string to_string() const {
        return fmt::format("<SRestoreRecBlob keyset_id={} pbkdf2_derived={} key_sz={:x} chk_sz={:x} salt_sz={:x}>",
            keyset_id_str(), is_pbkdf2_derived(), encrypted_key_size, key_checksum_size, salt_size);
    }
};

}
