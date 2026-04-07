#pragma once
#include <string>

namespace ReaClaw::TLS {

// Returns true if both PEM files already exist on disk.
bool files_exist(const std::string& cert_path, const std::string& key_path);

// Generate a self-signed RSA-4096 certificate (10 year validity).
// Writes cert_path (PEM) and key_path (PEM).
// Returns true on success.
bool generate_self_signed(const std::string& cert_path,
                          const std::string& key_path);

}  // namespace ReaClaw::TLS
