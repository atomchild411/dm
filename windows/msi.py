#!/usr/bin/env python3
# msi.py PKGDIR VERSION ARCH ICON OUT.wxs: the WiX source (for wixl, msitools)
# of the Windows installer: PKGDIR's files (the program, fonts, licences,
# README) into %LOCALAPPDATA%\Programs\Discord Messenger, for the user alone
# (no administrator), with a Start menu shortcut and the uninstall entry.
# ARCH is x64 or arm64.  VERSION is a.b.c; a higher one upgrades in place.

import os
import sys
from xml.sax.saxutils import quoteattr

pkg, version, arch, icon, out = sys.argv[1:6]

# These identify the product and its parts across versions: never change them.
UPGRADE_CODE = "F71B8A6F-3C12-405C-A7DA-E1A501E8ADD2"
GUIDS = {
    "Main": "6F2AF173-8A88-4746-815F-6CDC52BDF572",
    "Fonts": "7AD373B0-9DC6-4ADA-931E-DF64DD5F436C",
    "Licenses": "09CB962E-D398-4858-A34A-82079C28229A",
    "Shortcut": "5CA116A5-F6D2-4405-9BF6-3BA2D16528A9",
}

def files(sub):
    d = os.path.join(pkg, sub) if sub else pkg
    return sorted(f for f in os.listdir(d) if os.path.isfile(os.path.join(d, f)))

n = 0
def file_elements(sub):
    global n
    out = []
    for f in files(sub):
        n += 1
        src = os.path.join(pkg, sub, f) if sub else os.path.join(pkg, f)
        out.append('<File Id="f%d" Name=%s Source=%s />' % (n, quoteattr(f), quoteattr(os.path.abspath(src))))
    return "\n                ".join(out)

def keypath(name):
    # a per-user component's key: a value of ours in HKCU
    return ('<RegistryValue Root="HKCU" Key="Software\\DiscordMessenger" Name="%s" '
            'Type="integer" Value="1" KeyPath="yes" />' % name)

wxs = '''<?xml version="1.0" encoding="utf-8"?>
<Wix xmlns="http://schemas.microsoft.com/wix/2006/wi">
  <Product Id="*" Name="Discord Messenger" Language="1033" Version="{version}"
           Manufacturer="atomchild411" UpgradeCode="{upgrade}">
    <Package InstallerVersion="500" Compressed="yes" InstallScope="perUser"
             Description="Discord Messenger {version}" Comments="A Discord-compatible messenger" />
    <MajorUpgrade AllowSameVersionUpgrades="yes" DowngradeErrorMessage="A newer Discord Messenger is installed already." />
    <Media Id="1" Cabinet="dm.cab" EmbedCab="yes" />
    <Icon Id="dm.ico" SourceFile={icon} />
    <Property Id="ARPPRODUCTICON" Value="dm.ico" />
    <Property Id="ARPURLINFOABOUT" Value="https://github.com/atomchild411/dm" />
    <Property Id="ARPNOMODIFY" Value="1" />
    <Directory Id="TARGETDIR" Name="SourceDir">
      <Directory Id="LocalAppDataFolder">
        <Directory Id="UserProgramsDir" Name="Programs">
          <Directory Id="INSTALLDIR" Name="Discord Messenger">
            <Component Id="Main" Guid="{g_main}">
                {kp_main}
                {main_files}
                <RemoveFolder Id="RemoveInstallDir" On="uninstall" />
            </Component>
            <Directory Id="FontsDir" Name="fonts">
              <Component Id="Fonts" Guid="{g_fonts}">
                {kp_fonts}
                {font_files}
                <RemoveFolder Id="RemoveFontsDir" On="uninstall" />
              </Component>
            </Directory>
            <Directory Id="LicensesDir" Name="licenses">
              <Component Id="Licenses" Guid="{g_lic}">
                {kp_lic}
                {lic_files}
                <RemoveFolder Id="RemoveLicensesDir" On="uninstall" />
              </Component>
            </Directory>
          </Directory>
        </Directory>
      </Directory>
      <Directory Id="ProgramMenuFolder">
        <Component Id="Shortcut" Guid="{g_sc}">
          <Shortcut Id="StartMenuShortcut" Name="Discord Messenger" Description="A Discord-compatible messenger"
                    Target="[INSTALLDIR]DiscordMessenger.exe" WorkingDirectory="INSTALLDIR" />
          {kp_sc}
        </Component>
      </Directory>
    </Directory>
    <Feature Id="Complete" Level="1">
      <ComponentRef Id="Main" />
      <ComponentRef Id="Fonts" />
      <ComponentRef Id="Licenses" />
      <ComponentRef Id="Shortcut" />
    </Feature>
  </Product>
</Wix>
'''.format(version=version, upgrade=UPGRADE_CODE, icon=quoteattr(os.path.abspath(icon)),
           g_main=GUIDS["Main"], g_fonts=GUIDS["Fonts"], g_lic=GUIDS["Licenses"], g_sc=GUIDS["Shortcut"],
           kp_main=keypath("installed"), kp_fonts=keypath("fonts"), kp_lic=keypath("licenses"),
           kp_sc=keypath("shortcut"),
           main_files=file_elements(""), font_files=file_elements("fonts"), lic_files=file_elements("licenses"))

open(out, "w").write(wxs)
