Discord Messenger for Linux
===========================

Discord Messenger is a messenger application designed to be compatible
with Discord.  https://github.com/atomchild411/dm

Run ./discord-messenger from this folder.  Keep the "fonts" folder beside
it.  It needs X11 or Wayland with OpenGL 3.0; OpenSSL, FreeType, libpng,
libwebp and GLFW are inside it.

To have it in your desktop's menu, copy discord-messenger.desktop to
~/.local/share/applications/ (with Exec= set to this folder's
discord-messenger) and discord-messenger.png to
~/.local/share/icons/hicolor/64x64/apps/.

Logging in: log in on discord.com's own page (email and password, and any
captcha Discord asks for) in a window of the program, where WebKitGTK is
installed (libwebkit2gtk-4.1 or 4.0; most desktops have it); or use a
token.  It
follows your desktop's light or dark setting where the desktop says it
(GNOME, KDE), or choose one under the gear's Theme menu.

Your settings and the cache of images and messages are kept in
~/.discordmessenger.  The login is kept in your desktop's keyring (GNOME
Keyring, KWallet: the Secret Service) where there is one, else in
~/.discordmessenger/settings.json, readable only by you.

Using third party clients is against Discord's terms of service.  The
risk of a ban is low, but it is there; the authors are not responsible for
your Discord account.

The licences of everything inside are in the "licenses" folder.  Discord
Messenger itself is under the MIT licence.
