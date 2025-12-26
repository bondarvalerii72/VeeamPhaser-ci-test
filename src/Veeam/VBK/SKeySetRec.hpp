#include <cstdint>
#include <ctime>
#include <string>
#include <spdlog/fmt/bundled/format.h>

namespace Veeam::VBK {

// stglib::tstg::SKeySetRec
struct  __attribute__((packed)) SKeySetRec {
    enum EAlgo : int {
        ALGO_AES256CBC,
        ALGO_RSA
    };

    enum EKeyRole : int {
        KR_SESSION = 1, // data blocks are encrypted with this keyset
        KR_STORAGE, //metadata is encrypted with this keyset
        KR_META, 
        KR_ENTERPRISE,
        KR_USER,
        KR_ARCHIVE,
        // no 7
        KR_POLICY = 8, //rsa private key 
        KR_AGENT, //intermediate aes key, decrypted by the rsa private key.
        KR_NAS_SESSION,
        KR_NAS_BACKUP,
        KR_KMS_MASTER
    };

    static const uint32_t MAGIC = 0xa110ca2e; // "allocate"

    __int128_t uuid;
    EAlgo algo;
    char hint[0x200];
    EKeyRole role;
    uint32_t magic;
    uint32_t unknown[7];
    PhysPageId key_blobs_loc;
    PhysPageId restore_rec_blobs_loc;
    uint64_t timestamp; // FILETIME

    bool valid() const {
        if( magic != MAGIC || role < KR_SESSION || role > KR_KMS_MASTER || algo < ALGO_AES256CBC || algo > ALGO_RSA || uuid == 0)
            return false;

        const auto unix_timestamp = timestamp / 10000000ULL - 11644473600ULL;
        const auto year = 1970 + unix_timestamp / 31536000;
        if( year < 2000 || year > 2100 )
            return false;

        for(size_t i=0; i < sizeof(hint) && hint[i]; i++) {
            if(hint[i] < 0x20 || hint[i] > 0x7E)
                return false;
        }

        return key_blobs_loc.valid_or_empty() && restore_rec_blobs_loc.valid_or_empty();
    }

    std::string get_hint() const {
        // Find the null terminator in the hint array
        size_t len = 0;
        while (len < sizeof(hint) && hint[len] != '\0') {
            len++;
        }
        return std::string(hint, len);
    }

    std::string to_string() const {
        const char* role_str = "unknown";
        switch(role) {
            case KR_SESSION: role_str = "session"; break;
            case KR_STORAGE: role_str = "storage"; break;
            case KR_META: role_str = "meta"; break;
            case KR_ENTERPRISE: role_str = "enterprise"; break;
            case KR_USER: role_str = "user"; break;
            case KR_ARCHIVE: role_str = "archive"; break;
            case KR_POLICY: role_str = "policy"; break;
            case KR_AGENT: role_str = "agent"; break;
            case KR_NAS_SESSION: role_str = "nas_session"; break;
            case KR_NAS_BACKUP: role_str = "nas_backup"; break;
            case KR_KMS_MASTER: role_str = "kms_master"; break;
        }

        const char* algo_str = algo == ALGO_AES256CBC ? "aes256cbc" : "rsa";

        uint64_t uuid_low = (uint64_t)(uuid & 0xFFFFFFFFFFFFFFFF);
        uint64_t uuid_high = (uint64_t)((uuid >> 64) & 0xFFFFFFFFFFFFFFFF);

        const auto unix_timestamp = timestamp / 10000000ULL - 11644473600ULL;
        std::time_t time = static_cast<std::time_t>(unix_timestamp);
        char time_buf[32];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::gmtime(&time));

        return fmt::format("<SKeySetRec uuid={:016x}:{:016x} role={} algo={} hint='{}' key_blobs_loc={} restore_rec_blobs_loc={} timestamp='{}'>",
            uuid_high, uuid_low, role_str, algo_str, get_hint(), key_blobs_loc, restore_rec_blobs_loc, time_buf);
    }
};

static_assert(sizeof(SKeySetRec) == 0x250, "SKeySetRec size mismatch");

}
