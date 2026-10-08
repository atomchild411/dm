# Discord Messenger for IRIX

Discord Messenger is a messenger application designed to be compatible with Discord. This is its
port to SGI's IRIX 6.5, with a Motif user interface that follows your desktop's colour scheme.

It started as a fork of [Discord Messenger](https://github.com/DiscordMessenger/dm) by iProgramInCpp
and its contributors.

**NOTE**: This is beta software, so there may be issues which need to be fixed!

The project is licensed under the MIT license.

## Disclaimer

Using third party clients is against Discord's TOS! Although the risk to get banned is low, the
risk is there! The author of this software is not responsible for the status of your Discord
account.

See https://twitter.com/discord/status/1229357198918197248.

## Screenshots

![IRIX screenshot: the Motif client in demo mode on the 4Dwm desktop](doc/ss_irix.png)

![IRIX screenshot: code blocks, a reply with formatting and an embed](doc/ss_irix2.png)

## Minimum System Requirements

- IRIX 6.5.22 or later, with X11 and IRIX's Motif (`x_eoe`, `motif_eoe`)

- Any MIPS CPU from the R4000 on (the program is built for MIPS III): R4400, R4600, R5000, R8000,
  R10000 and later

- 128 MB of RAM: the client keeps about 50 MB resident on a busy server

- About 25 MB of disk for the program, its fonts and certificates, plus up to the size you allow for
  the image and message cache in `~/.discordmessenger/cache`

- Any graphics board: true-colour visuals are used as they are, 8-bit ones through a colour cube

- A network connection with working DNS; everything goes over HTTPS and secure websockets

Nothing else is needed: OpenSSL, FreeType, libpng, zlib, bzip2, libwebp and libc++ are linked into
the program, and the package brings its own fonts (DejaVu, Noto Color Emoji) and root certificates.

## Installing

Download the newest `.tardist` from the [releases](https://github.com/atomchild411/dm/releases) and
open it with Software Manager (swmgr), or:

```
mkdir /usr/tmp/dm && cd /usr/tmp/dm && tar xf dmessenger-<version>.tardist
inst -f /usr/tmp/dm -a
```

It upgrades any earlier release in place. Then run `/usr/local/bin/discord-messenger`.
Its README, installed as `/usr/local/lib/discord-messenger/README` (`irix/dist/README` here),
describes logging in, every feature and the environment variables it reads.

## Building

The client is cross-compiled on Linux and packaged on IRIX.

You need:

- a clang that targets IRIX 6.5 n32 (`mipseb-sgi-irix6.5`). The LLVM 21 port behind the
  `lang/clang-irix` package of [our pkgsrc fork](https://github.com/atomchild411/pkgsrc/tree/irix)
  does.
- IRIX's own X11 and Motif headers and libraries (`/usr/include`, `/usr/lib32`), copied from an
  IRIX system.
- OpenSSL 3, FreeType, libpng, zlib, bzip2 and libwebp built for IRIX, as static libraries, under
  one prefix.

After cloning, check out the submodules with `git submodule update --init`. Then:

```
make FRONTEND=motif STATIC_DEPS=1 \
    CXX=mipseb-sgi-irix6.5-clang++ CC=mipseb-sgi-irix6.5-clang \
    PREFIX_DEPS=<prefix> X_CFLAGS=-I<irix>/usr/include \
    X_LIBS='-L<irix>/usr/lib32 -lSgm -lXm -lXt -lX11 -lXext' \
    FT_LIBS='<prefix>/lib/libfreetype.a <prefix>/lib/libpng16.a <prefix>/lib/libz.a <prefix>/lib/libbz2.a' \
    EXTRA_CXXFLAGS='-march=mips3 -DDM_DATADIR=\"/usr/local/lib/discord-messenger\"' \
    EXTRA_LDFLAGS='-march=mips3 -static-libstdc++'
```

The program is `bin/dm-motif`. `FRONTEND=cli` builds `dm-cli`, a text client that drives the same
core without a GUI, for testing a port (`dm-cli --probe` checks HTTPS, TLS and the gateway without
logging in). The top of the `Makefile` lists every setting.

To make the package, copy the program and the `irix/dist` directory to an IRIX machine and run
`irix/dist/make-tardist.sh` there; it uses IRIX's `gendist`. The script's comments give its
arguments.

## Features

### Implemented

- Logging in with a QR code scanned by the Discord app, or with a token
- Servers (with their icons and folders), channels, the member list and direct messages
- Messages with Discord's formatting, replies, reactions, embeds and colour emoji
- Pictures in messages, and a viewer that scales them with its window
- Sending, replying, editing and deleting messages; emoji by shortcode or from a picker, the
  server's own emoji included
- Adding and taking back reactions
- Who is typing
- A sound for mentions and direct messages, and unread counts in the window's icon name
- The Messages menu, listing direct messages with the unread ones first; each conversation opens
  in a window of its own
- Read marks kept in step with Discord's other clients
- Showing or hiding the server, channel and member lists, larger or smaller text
- Coming back to the server and channel you were last in
- A cache of images and message history on disk, with size limits

### Unimplemented

- Uploading attachments
- Voice channels
- Friends list
- Typing non-Latin-1 text in the message box (emoji are written as shortcodes)
- Discord's captcha, which it sometimes asks for at the end of a QR login (log in with a token
  then)

## Attributions

Discord Messenger is powered by the following external libraries:

- [JSON for Modern C++](https://github.com/nlohmann/json)
- [Boost](https://www.boost.org)
- [Libwebp](https://github.com/webmproject/libwebp)
- [Httplib](https://github.com/yhirose/cpp-httplib)
- [Asio](https://think-async.com/Asio)
- [Websocketpp](https://github.com/zaphoyd/websocketpp)
- [OpenSSL](https://www.openssl.org)
- [FreeType](https://freetype.org), [libpng](http://www.libpng.org), [zlib](https://zlib.net) and
  [bzip2](https://sourceware.org/bzip2/)
- [stb_image](https://github.com/nothings/stb)
- [QR Code generator](https://github.com/nayuki/QR-Code-generator)
- [LLVM libc++](https://libcxx.llvm.org)
- The [DejaVu](https://dejavu-fonts.github.io) and [Noto Color Emoji](https://github.com/googlefonts/noto-emoji)
  fonts, and Mozilla's root certificates

Their licences come with the package, in `/usr/local/lib/discord-messenger/licenses`.
