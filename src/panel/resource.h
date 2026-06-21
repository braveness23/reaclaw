#pragma once

// Control/dialog IDs for the ReaClaw menu dialogs (Status, API key, Log).
// Shared by dialogs.cpp (SWELL macro resources on Linux/macOS) and dialogs.rc
// (Win32 resource compiler).

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

// ---- Status dialog ---------------------------------------------------------
#define IDD_REACLAW_STATUS 300
#define IDC_ST_LED 301       // colored square: green=running, gray=stopped
#define IDC_ST_STATE 302     // "Running" / "Stopped"
#define IDC_ST_ADDR 303      // scheme://host:port
#define IDC_ST_AUTH 304      // auth mode
#define IDC_ST_UPTIME 305    // h m s
#define IDC_ST_VERSION 306   // build version
#define IDC_ST_COPYADDR 307  // copy address button

// ---- API key dialog --------------------------------------------------------
#define IDD_REACLAW_APIKEY 320
#define IDC_AK_KEY 321      // read-only edit field showing the key
#define IDC_AK_COPY 322     // copy-to-clipboard button
#define IDC_AK_CONFIRM 323  // "Copied!" confirmation label

// ---- Log dialog ------------------------------------------------------------
#define IDD_REACLAW_LOG 340
#define IDC_LG_TEXT 341     // read-only multiline log view
#define IDC_LG_REFRESH 342  // reload from disk
