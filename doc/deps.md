# What deps/ holds

Each as its upstream released it, unchanged:

- **nlohmann/json 3.12.0**: `json.h` is the release's single-header `json.hpp`.
- **Nayuki's QR Code generator 1.8.0**, its C version (`qrcodegen.c`, `qrcodegen.h`).
- **Dear ImGui 1.92.9**, the `deps/imgui` submodule.

Everything else the program uses is linked from the system or built from its
release tarball by the platform scripts: OpenSSL, FreeType, libpng, zlib,
bzip2, libwebp and GLFW.  The network code (HTTPS, WebSocket) is our own,
over OpenSSL, in `src/core/network`; so is the reader for the protobuf
settings Discord sends (`src/core/utils/ProtoReader.hpp`).

GitHub's dependency graph cannot read C or C++ builds, so the versions above,
and the ones the platform scripts build, are listed by hand in
`.github/dependencies.json`; `.github/workflows/dependency-graph.yml` submits
that list on every push that changes it.  Each package is listed once (the
graph shows a package once per manifest): libraries more than one platform
uses go under `deps`.  Keep it in step when a version moves.
