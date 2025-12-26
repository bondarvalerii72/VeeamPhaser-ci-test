/**
 * @file MD5.cpp
 * @brief Implementation of MD5 hash calculation using OpenSSL EVP interface.
 *
 * This file provides a wrapper around OpenSSL's EVP MD5 functions for computing
 * MD5 hashes. Used throughout the application for block identification and
 * deduplication. The EVP interface is preferred over the deprecated direct MD5
 * API for future compatibility.
 */

#include "MD5.hpp"
#include <stdexcept>

#include <openssl/err.h>

/**
 * @brief Handles OpenSSL errors by printing them and aborting.
 */
static void handle_errors() {
    ERR_print_errors_fp(stderr);
    abort();
}

/**
 * @brief Constructs an MD5 calculator and initializes OpenSSL context.
 * @throws std::runtime_error If MD5 context creation fails.
 */
MD5::MD5() {
    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (mdctx == nullptr) {
        throw std::runtime_error("Failed to create MD5 context");
    }
    this->m_ctx = mdctx;
}

/**
 * @brief Calculates MD5 hash of the provided data.
 *
 * @param data Pointer to data to hash.
 * @param size Size of data in bytes.
 * @return digest_t containing the 16-byte MD5 hash.
 */
digest_t MD5::Calculate(const void* data, size_t size) {
    unsigned char hash[DIGEST_LENGTH];
    unsigned int hashLen;

    if(1 != EVP_DigestInit_ex(m_ctx, EVP_md5(), nullptr)) handle_errors();
    if(1 != EVP_DigestUpdate(m_ctx, data, size)) handle_errors();
    if(1 != EVP_DigestFinal_ex(m_ctx, hash, &hashLen)) handle_errors();

    return *(digest_t*)hash;
}

/**
 * @brief Destructor frees the OpenSSL MD5 context.
 */
MD5::~MD5() {
    if (this->m_ctx) {
        EVP_MD_CTX_free(this->m_ctx);
        this->m_ctx = nullptr;
    }
}
