#!/bin/sh
# Look up every package in .github/dependencies.json in OSV.dev and fail if
# any has a known vulnerability. Dependabot cannot do this for us: its
# advisories cover package ecosystems (npm, PyPI, ...), not C libraries
# fetched from git. OSV maps CVEs onto the libraries' git commits, so each
# release tag is resolved to its commit and the commit is looked up.
#
# A pkg:github purl names its repository and tag; any other purl gives its
# repository in a vcs_url qualifier (git+URL@tag). IDs listed in
# .github/osv-ignore.txt (one per line, # comments) are reported but do not
# fail the check; say there why each one does not affect us.
set -eu
here=$(dirname "$0")
ignore="$here/osv-ignore.txt"
found=0
for purl in $(jq -r '[.manifests[].packages[]] | unique | .[]' "$here/dependencies.json"); do
    case $purl in
    pkg:github/*)
        p=${purl#pkg:github/}; p=${p%%\?*}
        repo=https://github.com/${p%@*}; tag=${p#*@} ;;
    *vcs_url=git+*)
        v=${purl#*vcs_url=git+}; v=${v%%&*}
        repo=${v%@*}; tag=${v##*@} ;;
    *) echo "skip  $purl (no repository to look up)"; continue ;;
    esac
    sha=$(git ls-remote "$repo" "refs/tags/$tag" "refs/tags/$tag^{}" 2>/dev/null | sort -k2 | tail -1 | cut -f1)
    if [ -z "$sha" ]; then
        echo "FAIL  $purl: tag $tag not found in $repo"; found=1; continue
    fi
    ids=$(curl -sf -X POST https://api.osv.dev/v1/query -d "{\"commit\":\"$sha\"}" | jq -r '.vulns[]?.id')
    bad=""
    for id in $ids; do
        if [ -f "$ignore" ] && grep -q "^$id\b" "$ignore"; then
            echo "      $purl: $id (ignored, see osv-ignore.txt)"
        else
            bad="$bad $id"
        fi
    done
    if [ -n "$bad" ]; then
        echo "VULN  $purl ($sha):$bad"; found=1
    else
        echo "ok    $purl"
    fi
done
[ $found -eq 0 ] || { echo; echo "Look the IDs up at https://osv.dev/vulnerability/<ID>."; exit 1; }
