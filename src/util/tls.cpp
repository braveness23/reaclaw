#include "util/tls.h"

#include "util/logging.h"

#include <cstdio>
#include <filesystem>
#include <string>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace fs = std::filesystem;

namespace ReaClaw::TLS {

bool files_exist(const std::string& cert_path, const std::string& key_path) {
    return fs::exists(cert_path) && fs::exists(key_path);
}

bool generate_self_signed(const std::string& cert_path, const std::string& key_path) {
    Log::info("Generating self-signed TLS certificate (RSA-4096, 10yr)...");

    bool ok = false;
    EVP_PKEY_CTX* kctx = nullptr;
    EVP_PKEY* pkey = nullptr;
    X509* x509 = nullptr;
    FILE* f = nullptr;

    kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!kctx) {
        Log::error("TLS: EVP_PKEY_CTX_new_id failed");
        goto done;
    }
    if (EVP_PKEY_keygen_init(kctx) <= 0) {
        Log::error("TLS: keygen_init failed");
        goto done;
    }
    if (EVP_PKEY_CTX_set_rsa_keygen_bits(kctx, 4096) <= 0) {
        Log::error("TLS: set_rsa_keygen_bits failed");
        goto done;
    }
    if (EVP_PKEY_keygen(kctx, &pkey) <= 0) {
        Log::error("TLS: keygen failed");
        goto done;
    }

    x509 = X509_new();
    if (!x509) {
        Log::error("TLS: X509_new failed");
        goto done;
    }

    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 10L * 365 * 24 * 60 * 60);

    {
        X509_NAME* name = X509_get_subject_name(x509);
        X509_NAME_add_entry_by_txt(name,
                                   "CN",
                                   MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("ReaClaw"),
                                   -1,
                                   -1,
                                   0);
        X509_set_issuer_name(x509, name);
    }

    X509_set_pubkey(x509, pkey);

    if (!X509_sign(x509, pkey, EVP_sha256())) {
        Log::error("TLS: X509_sign failed");
        goto done;
    }

    // Write certificate
    f = fopen(cert_path.c_str(), "wb");
    if (!f) {
        Log::error("TLS: cannot open for writing: " + cert_path);
        goto done;
    }
    PEM_write_X509(f, x509);
    fclose(f);
    f = nullptr;

    // Write private key
    f = fopen(key_path.c_str(), "wb");
    if (!f) {
        Log::error("TLS: cannot open for writing: " + key_path);
        goto done;
    }
    PEM_write_PrivateKey(f, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(f);
    f = nullptr;

    Log::info("TLS cert: " + cert_path);
    Log::info("TLS key:  " + key_path);
    ok = true;

done:
    if (f)
        fclose(f);
    if (x509)
        X509_free(x509);
    if (pkey)
        EVP_PKEY_free(pkey);
    if (kctx)
        EVP_PKEY_CTX_free(kctx);
    return ok;
}

}  // namespace ReaClaw::TLS
