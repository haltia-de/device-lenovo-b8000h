#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Write linux-port/apps/packages.lock from the GitHub releases (ADR 0002):
#   update-lock.sh                 every repository of sources, its latest release
#   update-lock.sh NAME [vX.Y.Z]   one repository, its latest or a given release
# A line per package file: name repository version file sha256, the sha256
# taken from the release's SHA256SUMS. A repository whose release carries
# no apk keeps its old lines (or none: the image builds it from sources).
# Needs gh (signed in) or curl for the GitHub API, and sha256 sums in the
# release. The -pyc subpackages are left out: they only save the first start.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
lock=$here/packages.lock
tmp=$(mktemp)
trap 'rm -f "$tmp" "$tmp.new"' EXIT
[ -f "$lock" ] && grep -v '^#' "$lock" >"$tmp" || : >"$tmp"

api() { # path
	if command -v gh >/dev/null; then gh api "$1"; else curl -fsSL "https://api.github.com/$1"; fi
}

one() { # name repo-url [tag]
	name=$1
	repo=$(echo "$2" | sed -E 's|https://github.com/||; s|\.git$||')
	path=repos/$repo/releases/latest
	[ -z "${3:-}" ] || path=repos/$repo/releases/tags/$3
	rel=$(api "$path" 2>/dev/null) || { echo "$name: no release in $repo, lines kept" >&2; return 0; }
	# printf, not echo: dash's echo turns \n in the release notes into a
	# raw newline inside a JSON string.
	tag=$(printf '%s' "$rel" | python3 -c 'import json,sys; print(json.load(sys.stdin).get("tag_name",""))')
	files=$(printf '%s' "$rel" | python3 -c '
import json, sys
for a in json.load(sys.stdin).get("assets", []):
    n = a["name"]
    if n.endswith(".apk") and "-pyc-" not in n:
        print(n, a["browser_download_url"])')
	[ -n "$files" ] || { echo "$name: no apk in $repo $tag, lines kept" >&2; return 0; }
	sums=$(curl -fsSL "https://github.com/$repo/releases/download/$tag/SHA256SUMS")
	grep -v "^$name " "$tmp" >"$tmp.new" || true
	echo "$files" | while read -r file _url; do
		sum=$(echo "$sums" | awk -v f="$file" '$2 == f { print $1 }')
		[ -n "$sum" ] || { echo "$name: $file not in SHA256SUMS" >&2; exit 1; }
		echo "$name $repo ${tag#v} $file $sum"
	done >>"$tmp.new"
	mv "$tmp.new" "$tmp"
	echo "$name: $tag"
}

if [ $# -gt 0 ]; then
	url=$(awk -v n="$1" '$1 == n { print $2 }' "$here/sources")
	[ -n "$url" ] || { echo "no $1 in sources" >&2; exit 2; }
	one "$1" "$url" "${2:-}"
else
	grep -v '^#' "$here/sources" | while read -r name url _tag _commit _private; do
		[ -n "$name" ] && one "$name" "$url"
	done
fi
{
	echo "# Packages the image installs, from GitHub releases (ADR 0002)."
	echo "# Written by update-lock.sh; name repository version file sha256."
	sort "$tmp"
} >"$lock"
