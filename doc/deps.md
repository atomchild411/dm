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
