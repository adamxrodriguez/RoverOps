# Vendored C++ dependencies

Unmodified upstream files, pinned for reproducible offline C++ builds:

| Library | Version | Upstream | License |
| --- | --- | --- | --- |
| cpp-httplib | 0.60.1 | https://github.com/yhirose/cpp-httplib/tree/v0.60.1 | MIT |
| nlohmann/json | 3.12.0 | https://github.com/nlohmann/json/tree/v3.12.0 | MIT |
| doctest | 2.4.12 | https://github.com/doctest/doctest/tree/v2.4.12 | MIT |

Each subdirectory includes the upstream license. `SHA256SUMS` records the exact
headers and licenses. Verify from this directory with `shasum -a 256 -c SHA256SUMS`.
To upgrade, replace from a tagged upstream release, retain its license, regenerate
the manifest, and run C++ plus API/browser tests. Do not apply first-party formatting
to these files. Networking is plain local HTTP/WebSocket; optional TLS dependencies
are not enabled in this build.
