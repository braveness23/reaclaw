# Security Policy

ReaClaw embeds an HTTPS server directly inside the REAPER DAW process. This
gives AI agents — and potentially any local or remote network client — the
ability to query DAW state, trigger REAPER actions, and execute Lua scripts.
Understanding the security model before deploying ReaClaw is important.

---

## Supported Versions

| Version | Supported |
|---------|-----------|
| v0.x (current development) | Yes — best-effort, patches released as needed |
| v1.0+ (first stable release) | Yes — full support with SLA below |
| < v0.1 | No |

Pre-release (v0.x) builds receive security fixes, but patch cadence may be
faster or slower than the commitments described below. Once v1.0.0 is tagged,
the response SLA below applies in full.

---

## Reporting a Vulnerability

**Preferred method:** Use GitHub's built-in private vulnerability reporting.
Navigate to the Security tab of this repository and click
"Report a vulnerability." This keeps the disclosure confidential until a fix
is ready.

**Fallback:** If GitHub private reporting is unavailable for any reason, open a
regular GitHub Issue with the title "Security: [brief description]" and mark it
with the `security` label. Avoid including full exploit details in a public
issue; request a private channel first.

Please include as much of the following as you can:

- A description of the vulnerability and the attack scenario
- Steps to reproduce (config used, request payload, etc.)
- The version of ReaClaw and OS/platform affected
- Any proof-of-concept code or logs

### Response Commitment

| Milestone | Target |
|-----------|--------|
| Acknowledgement | Within **72 hours** of report |
| Triage / severity assessment | Within **5 business days** |
| Patch for **critical** issues | Within **14 days** |
| Patch for **high** issues | Within **30 days** |
| Patch for **medium/low** issues | Next scheduled release |

We will coordinate disclosure timing with you. We aim for responsible
disclosure — public details should not be released until a patch is available
or 90 days have elapsed, whichever comes first.

---

## Security Model

### Transport

ReaClaw listens exclusively on HTTPS. Plain HTTP is not supported and cannot
be enabled via configuration. The server defaults to `127.0.0.1:9091`
(loopback only). Binding to `0.0.0.0` or a specific interface is possible via
config but is an explicit user choice.

### TLS / Certificate

At first run, ReaClaw generates a self-signed X.509 certificate stored in
`{ResourcePath}/reaclaw/certs/`. The certificate is generated locally using
OpenSSL and never transmitted to any external service.

Because the certificate is self-signed, AI agent clients will typically need
to disable certificate verification (e.g., `curl -k` or the equivalent in
Python/Node). **This is expected and documented behavior, not a
vulnerability.** Users who require full certificate chain validation can
replace the auto-generated certificate with one signed by a local CA.

### Authentication

Two auth modes are supported, selected via `openclaw.json`:

| Mode | Description |
|------|-------------|
| `none` | No authentication. Suitable only for fully isolated localhost use. |
| `api-key` | Bearer token required on every request (`Authorization: Bearer <key>`). |

API key auth is the recommended default. The key is generated at install time
and stored in `{ResourcePath}/reaclaw/config.db` (SQLite). There is no
session, no cookie, no OAuth flow — just a shared secret per request.

### Lua Script Execution

ReaClaw can execute Lua scripts via the REAPER scripting API. Scripts are
subject to the following constraints:

- Scripts must reside within `{ResourcePath}/reaclaw/scripts/`. Paths outside
  this directory are rejected with HTTP 400.
- Lua code submitted inline via the API is parsed for syntax validity only
  before being written to the scripts directory. The agent submitting the
  script is considered a trusted principal. ReaClaw does not sandbox Lua
  execution — REAPER's own Lua environment applies.
- Script filenames are sanitized to prevent directory traversal
  (`../` sequences are rejected).

### Action Execution

ReaClaw can trigger any REAPER action by command ID. There is no allowlist of
permitted actions; the API gives full access to REAPER's action catalog. This
is by design — the intended deployment is a trusted local AI agent. Users who
need to restrict the action surface should firewall the port at the OS level.

---

## Scope

### In Scope

The following classes of issues are in scope for this security policy:

- **API authentication bypass** — any method to access authenticated endpoints
  without a valid API key
- **TLS bypass or downgrade** — forcing the server to accept plain HTTP or a
  weakened TLS handshake
- **Script path traversal** — escaping `{ResourcePath}/reaclaw/scripts/` via
  crafted filenames or paths
- **Certificate generation flaws** — weaknesses in the self-signed cert
  generation (e.g., predictable key material, dangerously short validity)
- **Privilege escalation within REAPER** — using the ReaClaw API to escape
  REAPER's expected permission boundary
- **Denial of service within ReaClaw itself** — resource exhaustion via the
  HTTP layer (e.g., request floods causing REAPER to hang or crash)

### Out of Scope

- Bugs in REAPER itself (report those to Cockos)
- OS-level privilege escalation (not caused by ReaClaw)
- Issues that require physical access to the machine
- Attacks that require the attacker to already have write access to
  `{ResourcePath}` (at that point they can already modify REAPER directly)
- Self-signed certificate warnings in browsers or curl without `-k`
  (expected behavior, not a vulnerability — see TLS section above)
- Issues only reproducible on a REAPER version no longer in mainstream use

---

## Security Best Practices for Deployers

- **Keep the server on loopback.** Do not bind to `0.0.0.0` unless you
  understand the implications and have a firewall in place.
- **Always enable API key auth.** The `none` mode is provided for local
  development convenience; do not use it in any environment where the port
  is reachable from untrusted hosts.
- **Rotate the API key** if you believe it has been compromised. A new key
  can be generated via the ReaClaw CLI or by deleting `config.db` (this also
  clears execution history).
- **Review scripts before use.** Lua scripts run with the same OS permissions
  as REAPER. Do not execute scripts from untrusted sources.
- **Keep ReaClaw updated.** Security fixes are released as patch versions.
