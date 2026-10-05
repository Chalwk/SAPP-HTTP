# Contributing

Bugs, ideas, and pull requests are welcome.

## Reporting a bug

Open an issue and include:

- Which SAPP build you're running (SAPP or Phasor, plus version)
- The Lua script or snippet that triggers the problem
- The exact error message or crash output
- Which libcurl version is bundled
- Whether you're on 32-bit Windows

## Reporting a security issue

**Do not open a public issue for security problems.**

Use the private [Report a vulnerability](https://github.com/Chalwk/SAPP-HTTP/security/advisories/new)
flow on the Security tab, or see [SECURITY.md](https://github.com/Chalwk/SAPP-HTTP/blob/main/SECURITY.md)
for the full policy.

## Suggesting a feature

Open an issue describing the problem you're trying to solve. Keep in mind the
goal is a thin C API that's easy to drive from LuaJIT FFI.

## Pull requests

Before opening a PR, make sure:

- The DLL builds cleanly with the provided CMake setup
- The C API stays backwards compatible, or breaking changes are documented
- New functions are exposed through the LuaJIT FFI header
- Example Lua scripts still run
- No hardcoded URLs, secrets, or machine-specific paths
- Tested against the SAPP and Phasor runtimes where applicable

## Code style

- C99, 4-space indentation
- Prefix public API symbols with `sapp_http_`
- Keep the C API thin; logic belongs in the implementation
- Document each exported function in the header

## Questions

Open a [GitHub Discussion](https://github.com/Chalwk/SAPP-HTTP/discussions)
or find me on [Discord](https://discord.gg/VAEb4FXU5).
