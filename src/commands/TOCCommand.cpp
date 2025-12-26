/**
 * @file TOCCommand.cpp
 * @brief Implementation of the TOCCommand for extracting and decrypting Table of Contents from VIB/VBK files.
 *
 * This file provides comprehensive functionality for extracting the Table of Contents (TOC)
 * from Veeam backup files, including support for encrypted metadata. It implements Veeam's
 * encryption scheme using AES-256-CBC with PBKDF2 key derivation, handling encrypted
 * metadata blocks and encryption keys. The extracted metadata can be saved to a file
 * for further processing with other commands.
 */

#include "TOCCommand.hpp"
#include "utils/common.hpp"
#include "core/structs.hpp"

#include <openssl/err.h> // For error printing
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

REGISTER_COMMAND(TOCCommand);

/**
 * @brief Constructs a TOCCommand with the specified registration status.
 * @param reg Boolean indicating whether to register this command with the command registry.
 */
TOCCommand::TOCCommand(bool reg) : Command(reg, "toc", "extract TOC from VIB/VBK") {
    m_parser.add_argument("filename").help("VIB/VBK file");
    m_parser.add_argument("-p", "--password").help("VBK/VIB encryption password");
    m_parser.add_argument("--try-all-keys")
        .implicit_value(true)
        .default_value(false)
        .help("try to decrypt metadata with all decryption keys");
}

/**
 * @brief Structure representing a Table of Contents entry.
 *
 * Each TOC entry points to a metadata block in the VIB/VBK file,
 * containing its CRC32 checksum, file offset, and size.
 */
struct __attribute__((packed)) TOCEntry {
    uint32_t crc;      ///< CRC32 checksum of the metadata block
    uint64_t offset;   ///< File offset where the metadata block is located
    uint32_t size;     ///< Size of the metadata block in bytes
};

/**
 * @brief Generates encryption key and IV using PBKDF2-HMAC-SHA1.
 *
 * Implements Veeam's key derivation function which uses PBKDF2 with HMAC-SHA1,
 * 10000 iterations, to derive a 256-bit encryption key and 128-bit IV from
 * a password and salt.
 *
 * @param password User password as a byte array (typically UTF-16LE encoded).
 * @param salt Salt value for key derivation.
 * @param[out] key Output buffer that will contain the derived 256-bit key.
 * @param[out] iv Output buffer that will contain the derived 128-bit IV.
 * @return True if key derivation succeeded, false on error.
 */
bool veGenerateKDFdata(const std::string& password, 
                       const std::string& salt, 
                       std::string& key, 
                       std::string& iv) {
    // Define sizes
    const int keyLength = 32; // 256-bit key
    const int ivLength = 16;  // 128-bit IV
    const int outputLength = keyLength + ivLength; // Total length of derived data
    const int iterations = 10000; // PBKDF2 iteration count

    // Resize key and iv strings
    key.resize(keyLength);
    iv.resize(ivLength);

    // Temporary buffer for the derived key and IV combined
    std::string derived(outputLength, '\0');

    // Perform PBKDF2 key derivation using HMAC-SHA1
    if (PKCS5_PBKDF2_HMAC(password.c_str(), password.size(),
                          (uint8_t*)salt.c_str(), salt.size(),
                          iterations, EVP_sha1(),
                          outputLength, (uint8_t*)(&derived[0])) != 1) {
        logger->critical("PBKDF2 key derivation failed");
        return false;
    }

    // Copy the derived data into key and iv
    key.assign(derived.begin(), derived.begin() + keyLength);
    iv.assign(derived.begin() + keyLength, derived.end());

    return true;
}

/**
 * @brief Decrypts data using AES-256-CBC.
 *
 * Performs AES-256-CBC decryption on the provided data using the given key and IV.
 * This function handles PKCS#7 padding removal and validates the padding correctness.
 * It's used to decrypt both encryption keys and encrypted metadata blocks.
 *
 * @param data Encrypted data to decrypt.
 * @param key 256-bit (32 bytes) encryption key.
 * @param iv 128-bit (16 bytes) initialization vector.
 * @param[out] outBuf Output buffer that will contain the decrypted data.
 * @return True if decryption succeeded and padding is valid, false otherwise.
 */
bool veAesCbcDecrypt(const std::string& data, const std::string& key, const std::string& iv, std::string& outBuf) {
    const int blockSize = 16; // AES block size in bytes
    const int keySize = 32;   // 256-bit key

    // Ensure key and IV sizes are correct
    if (key.size() != keySize || iv.size() != blockSize) {
        logger->critical("Key or IV size is incorrect");
        return false;
    }

    // Create a context for the AES operation
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        logger->critical("Failed to create EVP_CIPHER_CTX");
        return false;
    }

    // Initialize the decryption operation
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, (uint8_t*)key.data(), (uint8_t*)iv.data())) {
        logger->critical("EVP_DecryptInit_ex failed");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    EVP_CIPHER_CTX_set_padding(ctx, 0); // toggle padding

    // Provide the message to be decrypted, and obtain the plaintext output
    outBuf.resize(data.size() + blockSize); // Ensure enough space for padding
    int lenOut = 0;
    int lenFinal = 0;

    if (1 != EVP_DecryptUpdate(ctx, (uint8_t*)(&outBuf[0]), &lenOut, (uint8_t*)data.c_str(), data.size())) {
        logger->critical("EVP_DecryptUpdate failed");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    // Finalize the decryption. Further bytes may be written if the plaintext was padded.
    if (1 != EVP_DecryptFinal_ex(ctx, (uint8_t*)&outBuf[lenOut], &lenFinal)) {
        logger->critical("EVP_DecryptFinal_ex failed");
        ERR_print_errors_fp(stderr);
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    // Adjust the size of the output buffer
    outBuf.resize(lenOut + lenFinal);
    logger->trace("veAesCbcDecrypt(): decrypted data ({} bytes): {}", outBuf.size(), to_hexdump(outBuf));

    // Check padding
    unsigned char padLen = outBuf.back();
    if (padLen == 0) {
        // No padding issue
    } else if (padLen <= blockSize) {
        for (size_t i = outBuf.size() - 1; i >= outBuf.size() - padLen; --i) {
            if (outBuf[i] != padLen) {
                logger->error("veAesCbcDecrypt: Padding problem");
                EVP_CIPHER_CTX_free(ctx);
                return false;
            }
        }
        outBuf.resize(outBuf.size() - padLen);
    } else {
        logger->info("veAesCbcDecrypt: Padding Last byte over PKCS Range");
        EVP_CIPHER_CTX_free(ctx);
        return false;
    }

    EVP_CIPHER_CTX_free(ctx);
    return true;
}

/**
 * @brief Verifies encrypted metadata by counting zero blocks.
 *
 * Scans the encrypted metadata area for zero digest blocks, which indicate
 * portions that don't need decryption. This helps identify the actual encrypted
 * data size within a metadata block.
 *
 * @param data Metadata block containing encrypted data.
 * @return Number of bytes identified as zero blocks (not needing decryption).
 */
uint32_t veVerifyEncrData(const std::string& data){
    uint32_t dataNOK = 0;
    uint32_t mdEncrSize = *(uint32_t*)(data.data() + 0xc14);
    if( mdEncrSize ){
        digest_t *p = (digest_t*)(data.data() + 0x1000);
        for(uint32_t i = 0; i < (mdEncrSize/0x10); i++){
            if( p[i] == ZERO_BLOCK_DIGEST ){
                dataNOK += 0x10;
            }
        }
    }
    return dataNOK;
}

/**
 * @brief Acquires the encryption password from user or command-line arguments.
 *
 * Retrieves the password either from the --password command-line argument or
 * by prompting the user interactively. The password is then converted to
 * UTF-16LE encoding (2-byte characters) as required by Veeam's encryption.
 *
 * @return Password encoded as UTF-16LE byte string.
 * @throws std::runtime_error If password input fails.
 */
std::string TOCCommand::veAcquirePwd(){
    std::string pwd;

    if( m_parser.present("--password") ) {
        pwd = m_parser.get<std::string>("--password");
    } else {
        printf("[?] Please specify VBK/VIB encryption password: ");
        char buf[0x100];
        if(!fgets(buf, sizeof(buf), stdin)){
            throw std::runtime_error("Failed to read password");
        }
        buf[strcspn(buf, "\r\n")] = 0;
        pwd = buf;
    }

    // convert pwd to 2-byte char
    std::string pwd2;
    pwd2.reserve(pwd.size() * 2);
    for( size_t i = 0; i < pwd.size(); i++ ) {
        pwd2.push_back(pwd[i]);
        pwd2.push_back(0);
    }
    return pwd2;
}

/**
 * @brief Extracts encryption key data from metadata pages.
 *
 * Scans through metadata pages of type 2 (encryption keys) to extract
 * encrypted key blocks, test patterns, and salt values. These components
 * are needed to decrypt the actual encryption keys used for metadata decryption.
 *
 * @param meta Metadata buffer containing type 2 (encryption key) pages.
 * @param[out] keys Vector that will be populated with extracted encryption key structures.
 */
void veGetKeyData(const std::string& meta, std::vector<veEncrKey>& keys){
    uint32_t nx;


    for( size_t i = 0; i < meta.size(); i+=0x1000 ) {
        uint32_t keySlot1 = *(uint32_t*)(meta.data() + i + 0x36); // Encrypted Key
        uint32_t keySlot2 = *(uint32_t*)(meta.data() + i + 0x3a); // Pwd validity pattern
        uint32_t keySlot3 = *(uint32_t*)(meta.data() + i + 0x3e); // Salt

        if( keySlot1 < 0x1000 && keySlot2 < 0x1000 && keySlot3 < 0x1000 && keySlot1 + keySlot2 + keySlot3 != 0 ) {
            logger->debug("{:06x}: keySlot1={:08x} keySlot2={:08x} keySlot3={:08x}", i, keySlot1, keySlot2, keySlot3);
            logger->trace("veGetKeyData(): keys page {}", to_hexdump(meta.data() + i, 0x1000));
//        }

//        if( keySlot1 == 0x60 || keySlot2 == 0x10 || keySlot3 == 0x40 ) {
            veEncrKey key;
            nx = 0;
            // XXX original code calculates key offsets incorrectly if any keyslot value is not in (0x60, 0x10, 0x40)
//            if( keySlot1 == 0x60 ) {
                key.EncryptedKey = std::string(meta.data() + i + 0x42 + nx, keySlot1);
                if( !key.EncryptedKey.empty() )
                    logger->trace("EncryptedKey: {}", to_hexdump(key.EncryptedKey));
                nx += keySlot1;
//            }

//            if( keySlot2 == 0x10 ) {
                key.TestPattern = std::string(meta.data() + i + 0x42 + nx, keySlot2);
                if( !key.TestPattern.empty() )
                    logger->trace("TestPattern: {}", to_hexdump(key.TestPattern));
                nx += keySlot2;
//            }

//            if( keySlot3 == 0x40 ) {
                key.Salt = std::string(meta.data() + i + 0x42 + nx, keySlot3);
                if( !key.Salt.empty() )
                    logger->trace("Salt: {}", to_hexdump(key.Salt));
                nx += keySlot3;
//            }

            keys.push_back(std::move(key));
        }
    }
}

/**
 * @brief Processes and extracts the Table of Contents from a VIB/VBK file.
 *
 * This is the main TOC processing function that:
 * 1. Reads the TOC from a fixed offset (0x81078) in the VIB/VBK file
 * 2. Extracts all metadata blocks referenced by the TOC
 * 3. Identifies encryption key blocks (type 2 metadata)
 * 4. If encryption is detected, prompts for password and decrypts metadata
 * 5. Writes all extracted and decrypted metadata to a METADATA.bin file
 *
 * The function handles the complete encryption workflow including key derivation,
 * encryption key decryption, and metadata block decryption.
 *
 * @param fname Path to the VIB/VBK file to process.
 * @throws std::runtime_error If file operations fail.
 */
void TOCCommand::process_toc(std::string fname){
    std::ifstream f(fname, std::ios::binary);
    if( !f ){
        throw std::runtime_error("Failed to open " + fname);
    }

    f.seekg(0x81078);
    uint32_t nrecords = 0;
    f.read((char*)&nrecords, 4);

    if( (nrecords & 0xffff0000) != 0 ){
        logger->critical("TOC is probably corrupted");
        return;
    }
    logger->info("TOC Seems OK, {} entries", nrecords);

    std::vector<int> encrMd;
    std::vector<std::string> fms;
    std::vector<veEncrKey> veKeys;

    std::vector<TOCEntry> entries(nrecords);
    f.read((char*)entries.data(), nrecords * sizeof(TOCEntry));

    auto f2name = get_out_pathname(fname, "METADATA.bin");
    std::ofstream f2(f2name, std::ios::binary);
    if( !f2 ){
        throw std::runtime_error("Failed to open " + f2name.string());
    }

    const char TOCMark = 1;
    f2.write(&TOCMark, 1);

    for( size_t i = 0; i < nrecords; i++ ) {
        const auto& e = entries[i];
        logger->info("offset {:8x} crc {:08x} size {:8x}", e.offset, e.crc, e.size);
        f.seekg(e.offset);
        std::string fm(e.size, 0);
        f.read((char*)fm.data(), e.size);

        uint16_t msize = *(uint16_t*)fm.data();
        uint8_t mdType = fm[2]; // 0 - normal MD, 1 - unneeded MDs, 2 - encryption keys
        uint32_t srcMetaSize = ((msize >> 4) << 0x10) | 0x2000;

        const char* szMdtype = "unknown";
        switch( mdType ) {
            case 0: szMdtype = "normal block"; break;
            case 1: szMdtype = "unneeded block"; break;
            case 2:
                    szMdtype = "encryption keys";
                    encrMd.push_back(i);
                    break;
        }

        logger->info("    msize={:x}, srcMetaSize={:6x}, mdType={} ({})", msize, srcMetaSize, mdType, szMdtype);

        if( srcMetaSize != e.size ) {
            // XXX assuming that actual size is always less than or equal to the expected size
            logger->error("Metadata size mismatch: TOC {:x}, actual {:x}", e.size, srcMetaSize);
            fm.resize(srcMetaSize);
        }

        //hexdump(fm.data(), 0x20, "    ");
        fms.push_back(std::move(fm));
    }

    if( encrMd.size() > 0 ) {
        int mdTestId = -1;
        std::string veSalt, veKey, veIv, vePwd = veAcquirePwd(), veOut;

        logger->info("Encryption Block detected in extracted MD blocks, we will now decrypt keys and MDs");
        for( int idx: encrMd ){
            veGetKeyData(fms[idx], veKeys);
        }
        logger->info("Total Keys Acquired: {}", veKeys.size());

        for( size_t m = 0; m < veKeys.size(); m++ ) {
            const auto& key = veKeys[m];
            logger->info("Key {}: EncryptedKey{}, DecryptedKey{}, Salt{}, TestPwd{}", m,
                key.EncryptedKey.empty() ? '-' : '+',
                key.DecryptedKey.empty() ? '-' : '+',
                key.Salt.empty() ? '-' : '+',
                key.TestPattern.empty() ? '-' : '+');

            if( !key.Salt.empty() ) {
                veSalt = key.Salt;
            }
            if( !key.TestPattern.empty() ) {
                mdTestId = m;
            }
        }

        if( veSalt.empty() ){
            logger->critical("Not found needed salt in any KeyData acquired! Can't decrypt anything... will save to file encrypted MDs");

        } else {
            if( veGenerateKDFdata(vePwd, veSalt, veKey, veIv) ){
                logger->info("Key: {}", to_hexdump(veKey));
                logger->info("IV:  {}", to_hexdump(veIv));
            }

            if( mdTestId >= 0 ) {
                // Decrypt data of PWD test and verify pwd
                veAesCbcDecrypt(veKeys[mdTestId].TestPattern, veKey, veIv, veOut);
                logger->info("vTestPattern: {}", to_hexdump(veOut));
                veAesCbcDecrypt(veKeys[mdTestId].EncryptedKey, veKey, veIv, veOut);
                if( veOut.size() >=  0x52 ) {
                    veKey = veOut.substr(0x22, 0x20);
                    veIv  = veOut.substr(0x42, 0x10);

                    logger->info("Main Key: {}", to_hexdump(veKey));
                    logger->info("Main IV:  {}", to_hexdump(veIv));
                    veKeys[mdTestId].DecryptedKey = veOut;
                } else {
                    logger->error("not enough decrypted data ({} bytes)", veOut.size());
                }
            }

            for( size_t m = 0; m < veKeys.size(); m++ ) {
                // Decrypt all other keys
                if( !veKeys[m].DecryptedKey.empty() ) {
                    logger->info("DecrKey{}: {}", m, to_hexdump(veKeys[m].DecryptedKey));
                } else {
                    veAesCbcDecrypt(veKeys[m].EncryptedKey, veKey, veIv, veOut);
                    veKeys[m].DecryptedKey = veOut;
                    logger->info("DecrKey{}: {}", m, to_hexdump(veOut));
                }
                if( veKeys[m].DecryptedKey.size() >= 0x52 ) {
                    veKeys[m].Key = veKeys[m].DecryptedKey.substr(0x22, 0x20);
                    veKeys[m].IV  = veKeys[m].DecryptedKey.substr(0x42, 0x10);
                }
            }
        }
    } // if( encrMd.size() > 0 )

    if( !veKeys.empty() ){
        uint32_t dataNOK = 0;
        std::string veOut;

        for( size_t i = 0; i < fms.size(); i++ ) {
            uint8_t mdType = fms[i][2];
            uint32_t mdEncrLen = *(uint32_t*)(fms[i].data() + 0xc14);
            logger->debug("mdType={} mdEncrLen={:x}", mdType, mdEncrLen);
            logger->debug("encrypted: {}", to_hexdump(fms[i].data(), 0x1000+0x100));
            // C04 is mdKeyId, C14 is len of encr data at +$1000
            if( mdType < 2 ) {
                if( (mdEncrLen + 0x1000 + (0x1000-((mdEncrLen) % 0x1000))) == fms[i].size() ) {
                    dataNOK = veVerifyEncrData(fms[i]);
                    if( dataNOK ) {
                        logger->warn("MD {} dataNOK {} ToDecrypt {}", i, dataNOK, mdEncrLen-dataNOK);
                    }
                    if( m_parser.is_used("--try-all-keys") ){
                        for( size_t m = 0; m < veKeys.size(); m++ ) {
                            veAesCbcDecrypt(fms[i].substr(0x1000, mdEncrLen-dataNOK), veKeys[m].Key, veKeys[m].IV, veOut);
                            logger->debug("decrypted[{}]: {}", m, to_hexdump(veOut, 0x200));
                        }
                    }
                    veAesCbcDecrypt(fms[i].substr(0x1000, mdEncrLen-dataNOK), veKeys[0].Key, veKeys[0].IV, veOut); // XXX why only first key?
                    fms[i].replace(0x1000, veOut.size(), veOut);
                    if( !m_parser.is_used("--try-all-keys") ){
                        logger->debug("decrypted: {}", to_hexdump(veOut, 0x200));
                    }
                } else {
                    logger->error("Meta {} Encryption Size mismatch!", i);
                }
            }
        }
    }

    if( !fms.empty() ){
        for( const auto& fm : fms ){
            uint8_t mdType = fm[2];
            if( mdType <= 2 ){
                f2.write(fm.data(), fm.size());
            }
        }
    }

    logger->info("Metadata successfully saved to {}, use Load VBK next", f2name);
}

/**
 * @brief Executes the TOC command to extract and decrypt the Table of Contents.
 *
 * Initializes logging and delegates to process_toc() to perform the actual
 * TOC extraction and metadata processing.
 *
 * @return EXIT_SUCCESS (0) on successful completion.
 */
int TOCCommand::run() {
    const std::string fname = m_parser.get("filename");
    init_log(fname);
    process_toc(fname);
    return 0;
}
