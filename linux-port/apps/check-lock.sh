#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# check-lock.sh: packages.lock and sources name the same version of every
# locked program (update-lock.sh after a tag bump). build-rootfs.sh runs it
# before it installs from the lock; it also runs by hand. Exit 1 on the
# first disagreement, with what to run.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
bad=0
grep -v '^#' "$here/packages.lock" | awk 'NF { print $1, $3 }' | sort -u |
while read -r name version; do
	tag=$(awk -v n="$name" '$1 == n { print $3 }' "$here/sources")
	if [ -z "$tag" ]; then
		echo "$name: in packages.lock, not in sources" >&2
		exit 1
	fi
	if [ "$tag" != "v$version" ]; then
		echo "$name: packages.lock has $version, sources $tag: run update-lock.sh $name $tag" >&2
		exit 1
	fi
done || bad=1
exit $bad
