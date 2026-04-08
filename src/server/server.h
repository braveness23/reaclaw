#pragma once

namespace ReaClaw {
struct Config;

namespace Server {

// Start the HTTPS server on a background thread.
// Generates TLS cert/key if configured and missing.
// Returns false on fatal error (e.g., cert generation failure).
bool start(const Config& cfg);

// Signal the server to stop and join its thread. Blocks until done.
void stop();

// Returns true if the server thread is currently listening.
bool is_running();

}  // namespace Server
}  // namespace ReaClaw
