# Fixes on top of the libraries' releases

Security fixes upstream has committed but not yet released, applied to the
release tarballs by every build that compiles the library
(`macos/build-deps.sh`, `linux/inside-portable.sh`, `windows/inside.sh`;
the IRIX builds get the same fixes from our pkgsrc packages). Each file is
the upstream commit as `git format-patch` wrote it, named after the CVE it
fixes and the commit. Drop a directory once a release includes its fixes,
and remove the CVEs from `.github/osv-ignore.txt` with it.

- `freetype-2.14.3/`: CVE-2026-50811 (TrueType GX variations, out-of-bounds
  read), CVE-2026-95512 (CID fonts, unbounded subroutine overlaps)
- `zlib-1.3.2/`: CVE-2026-76844 (non-blocking gzwrite buffer overflow);
  Windows only, macOS and Linux use the system's zlib
