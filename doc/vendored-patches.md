# Changes to the vendored libraries

`deps/` holds upstream releases with only these changes:

- **cpp-httplib 0.11.4**
  - Readable `to_string(Error)` messages, which the client shows in its
    error dialogs.
  - 16 KB receive and 32 KB compression buffers.
  - The multipart parser rejects a part whose headers it cannot read.
  - Common-name checks through the `ASN1_STRING` accessors, for OpenSSL 4.

Everything else under `deps/` is as its upstream ships it.
