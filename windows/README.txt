Discord Messenger for Windows
=============================

Discord Messenger is a messenger application designed to be compatible
with Discord.  https://github.com/atomchild411/dm

Run DiscordMessenger.exe.  Keep the "fonts" folder beside it.

Logging in: log in on discord.com's own page (email and password, and any
captcha Discord asks for) in a window of the program, or use a token.  The
window uses Microsoft's Edge WebView2, which Windows 11 and current Windows
10 have; it keeps no cookies.

Your settings, and the cache of images and messages, are kept in
%APPDATA%\DiscordMessenger; the login itself in Windows' Credential
Manager (Windows Credentials, "DiscordMessenger:...").  Messages for troubleshooting go to the
console when the program is started from one (cmd or PowerShell).

Using third party clients is against Discord's terms of service.  The
risk of a ban is low, but it is there; the authors are not responsible for
your Discord account.

The licences of everything inside are in the "licenses" folder.  Discord
Messenger itself is under the MIT licence.
