Discord Messenger for Windows
=============================

Discord Messenger is a messenger application designed to be compatible
with Discord.  https://github.com/atomchild411/dm

Run "Discord Messenger.exe".  Keep the "fonts" folder beside it.

Logging in: scan the QR code with the Discord app on your phone, log in on
discord.com's own page (email and password) in a window of the program, or
use a token.  When Discord asks for a captcha at the end of a QR login, it
shows in a window of its own.  Both windows use Microsoft's Edge WebView2,
which Windows 11 and current Windows 10 have; they keep no cookies.

Your settings, and the cache of images and messages, are kept in
%APPDATA%\DiscordMessenger.  Messages for troubleshooting go to the
console when the program is started from one (cmd or PowerShell).

Using third party clients is against Discord's terms of service.  The
risk of a ban is low, but it is there; the authors are not responsible for
your Discord account.

The licences of everything inside are in the "licenses" folder.  Discord
Messenger itself is under the MIT licence.
