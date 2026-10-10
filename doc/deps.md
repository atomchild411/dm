# What deps/ holds

Each as its upstream released it, unchanged:

- **nlohmann/json 3.12.0**: `json.h` is the release's single-header `json.hpp`.
- **Nayuki's QR Code generator 1.8.0**, its C version (`qrcodegen.c`, `qrcodegen.h`).
- **Dear ImGui 1.92.9**, the `deps/imgui` submodule.

Everything else the program uses is linked from the system or built from its
release tarball by the platform scripts: OpenSSL, FreeType, libpng, zlib,
bzip2, libwebp and GLFW.  Security fixes upstream has committed but not yet
released are applied on top from `deps/patches` (its README lists them); the
IRIX builds get the same fixes from our pkgsrc packages.  The network code (HTTPS, WebSocket) is our own,
over OpenSSL, in `src/core/network`; so is the reader for the protobuf
settings Discord sends (`src/core/utils/ProtoReader.hpp`).

GitHub's dependency graph cannot read C or C++ builds, so the versions above,
and the ones the platform scripts build, are listed by hand in
`.github/dependencies.json`; `.github/workflows/dependency-graph.yml` submits
that list on every push that changes it.  Each package is listed once (the
graph shows a package once per manifest): libraries more than one platform
uses go under `deps`.  Keep it in step when a version moves: the same list
is what `.github/workflows/vulnerabilities.yml` checks against OSV.dev every
week (`sh .github/osv-check.sh` runs it locally).  Dependabot alerts are on,
but they only cover package ecosystems, so they cannot see these libraries.
A CVE we have patched goes in `.github/osv-ignore.txt`, with the reason.

## The version

`VERSION` holds the release's version, the same for every client (since
1.12); each build's number is the commit count at the release commit
(`DM_BUILD` where there is no .git).  The Windows resources, the MSI, the
macOS bundle, the .deb and the tarball names all read it; the IRIX image
version is VERSION's digits followed by the build as six digits (1.12,
build 662: 112000662).
