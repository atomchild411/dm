# Changes to the vendored libraries

`deps/` holds upstream releases with only these changes:

- **asio 1.28.0**
  - `detail/posix_event.hpp`, `detail/impl/posix_event.ipp`: IRIX has no
    `pthread_condattr_setclock`, so condition waits use the realtime
    clock, and their deadlines come from that clock too.
  - `ssl/impl/rfc2818_verification.ipp`: reads certificate names through
    the `ASN1_STRING` accessors, for OpenSSL 4.
- **cpp-httplib 0.11.4**
  - Readable `to_string(Error)` messages, which the client shows in its
    error dialogs.
  - 16 KB receive and 32 KB compression buffers.
  - The multipart parser rejects a part whose headers it cannot read.
  - Common-name checks through the `ASN1_STRING` accessors, for OpenSSL 4.
- **websocketpp 0.8.2**: as released.

Everything else under `deps/` is as its upstream ships it.
