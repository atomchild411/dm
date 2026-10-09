# What deps/ holds

Each as its upstream released it, unchanged:

- **nlohmann/json 3.12.0**: `json.h` is the release's single-header `json.hpp`.
- **Nayuki's QR Code generator 1.8.0**, its C version (`qrcodegen.c`, `qrcodegen.h`).
- **Dear ImGui 1.92.9**, the `deps/imgui` submodule.

And one that is not a library: `protobuf/Protobuf.hpp` is Discord Messenger's
own small reader and writer for the protobuf data Discord sends (user
settings), from the original project, with our fixes for big-endian and
strict-alignment CPUs.

Everything else the program uses is linked from the system or built from its
release tarball by the platform scripts: OpenSSL, FreeType, libpng, zlib,
bzip2, libwebp and GLFW.  The network code (HTTPS, WebSocket) is our own,
over OpenSSL, in `src/core/network`.
