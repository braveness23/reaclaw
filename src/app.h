#pragma once
// Shared application-level singletons, declared here and defined in main.cpp.

#include "config/config.h"
#include "db/db.h"

#include <chrono>

namespace ReaClaw {

extern Config                                    g_config;
extern DB                                        g_db;
extern std::chrono::steady_clock::time_point     g_start_time;

}  // namespace ReaClaw
