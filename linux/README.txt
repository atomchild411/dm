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

Logging in: scan the QR code with the Discord app on your phone, or use a
token.  (Discord sometimes asks for a captcha at the end of a QR login,
which the Linux version cannot show yet: log in with a token then.)  It
follows your desktop's light or dark setting where the desktop says it
(GNOME, KDE), or choose one under the gear's Theme menu.

Your settings and the cache of images and messages are kept in
~/.discordmessenger.

Using third party clients is against Discord's terms of service.  The
risk of a ban is low, but it is there; the authors are not responsible for
your Discord account.

The licences of everything inside are in the "licenses" folder.  Discord
Messenger itself is under the MIT licence.
