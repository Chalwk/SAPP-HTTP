# Security Policy

SAPP-HTTP is a 32-bit Windows DLL that exposes HTTP/HTTPS functionality to Lua
scripts through LuaJIT FFI. This document explains which versions receive
security updates, how to report a vulnerability, and what to expect.

## Supported Versions

Fixes are applied to the latest version on `main`.

| Version          | Supported |
| ---------------- | --------- |
| Latest on `main` | Yes       |
| Older commits    | No        |
| Forks            | No        |

## Reporting a Vulnerability

**Please do not open a public issue for security problems.**

Two private channels are available:

1. **GitHub Private Vulnerability Reporting** (preferred). Use the
   [Report a vulnerability](https://github.com/Chalwk/SAPP-HTTP/security/advisories/new)
   button on the Security tab.
2. **Email**. Email [chalwk.dev@gmail.com](mailto:chalwk.dev@gmail.com) with
   "SECURITY" in the subject line.

### What to include

- The DLL version or commit
- The SAPP or Phasor version
- A clear description of the issue
- Steps to reproduce, or a minimal proof of concept
- The impact you believe it has

Redact any real API keys, tokens, or IP addresses.

## Scope

### In scope

- Buffer overflows, use-after-free, or memory corruption in the C API
- Unsafe handling of HTTP responses (headers, body, redirects)
- TLS verification being disabled or bypassable
- URL parsing issues that could allow SSRF or header injection
- Credentials leaked into logs or error messages
- Hardcoded secrets in the source or build files
- Vulnerabilities in the bundled libcurl or its dependencies

### Out of scope

- Issues in SAPP itself or in scripts that consume the DLL
- Rate limiting or uptime of third-party services
- Findings from automated scanners with no demonstrated impact
- Cosmetic bugs or feature requests

## What to expect

- **Acknowledgement:** within 7 days
- **Initial assessment:** within 14 days
- **Fix:** usually within 30 days for confirmed issues
- **Public disclosure:** coordinated with you

## Using SAPP-HTTP safely

- Never hardcode API keys or tokens in Lua scripts. Read them from a config
  file or environment variable.
- Verify TLS. Don't disable certificate checks.
- Treat responses from external services as untrusted input.
- Keep the bundled libcurl up to date.

## Automated security

- Dependabot alerts and security updates
- Secret scanning with push protection
