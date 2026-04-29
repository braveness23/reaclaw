#pragma once

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#define IDD_REACLAW_PANEL 200

// Status row (top)
#define IDC_STATUS_LED 201   // Colored square indicator (green=running, gray=stopped)
#define IDC_STATUS_TEXT 202  // "Running  https://..." or "Stopped"
#define IDC_START_STOP 203   // "Start" / "Stop" toggle button

// Settings
#define IDC_HOST 204
#define IDC_PORT 205
#define IDC_TLS_BYPASS 206

// Separators (unique IDs so relayout can stretch them)
#define IDC_SEP1 207  // Between status row and settings
#define IDC_SEP2 208  // Between settings and log

// Log section
#define IDC_LOG 209
#define IDC_REFRESH 210
#define IDC_CLEAR_LOG 211

// Bottom action
#define IDC_APPLY 212
