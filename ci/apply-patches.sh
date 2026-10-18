#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Apply linux-port/patches onto the pinned blade/v7.1 tree (ci/sources.sh
# fetched it), then make the config: lenovo-blade_defconfig with the MT6628
# modules enabled. By hand and in CI alike (ADR 0003).
# Stops at the first patch that does not apply. Does not edit the kernel to force a fit.
set -eu
K=/opt/b8000h/linux-drm
P=/src/linux-port/patches
REPO=/src
git config --global --add safe.directory "$K"
cd "$K"
echo "HEAD $(git rev-parse HEAD)"
git checkout --detach 7dd245445adf6eaa27018cf92fbd5a92ec0b118e
git reset --hard HEAD
git clean -fd
echo "PIN $(git rev-parse HEAD)"
# 0022 and 0030 also mirror options into .config. A fresh tree has none;
# the real config is regenerated from the defconfig after the series.
: > .config

add_dtb_after() {
	after=$1
	line=$2
	python3 - "$after" "$line" <<'PY'
import sys
from pathlib import Path
after, line = sys.argv[1], sys.argv[2]
p = Path("arch/arm/boot/dts/mediatek/Makefile")
s = p.read_text()
if line in s:
    sys.exit(0)
needle = "\t" + after + " \\\n"
insert = needle + "\t" + line + " \\\n"
if needle not in s:
    raise SystemExit("makefile anchor missing: " + after)
p.write_text(s.replace(needle, insert, 1))
PY
}

install_sd() {
	cp "$P/0007-b8000-h-sd-dts.txt" \
		arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h-sd.dts
	# 0009's context already contains this line; no patch in the series adds it.
	add_dtb_after mt6589-lenovo-b8000-h-safe.dtb mt6589-lenovo-b8000-h-sd.dtb
	echo "INSTALLED sd dts"
}

install_install2() {
	cp "$P/0011-b8000-h-install2-dts.txt" \
		arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h-install2.dts
	add_dtb_after mt6589-lenovo-b8000-h-install.dtb mt6589-lenovo-b8000-h-install2.dtb
	echo "INSTALLED install2 dts"
}

run_py() {
	f=$1
	src="$P/$f"
	if [ "$f" = 0018-apply-mt6589-mipi-tx.py ]; then
		sed "s|/mnt/c/Users/<user>/src/lenovo-b8000h-linux|$REPO|" "$src" > /tmp/0018.py
		src=/tmp/0018.py
	fi
	# 0044 and later expect the build number in a .config they find, and
	# skip a missing one: the stub has served 0022-0039 and goes now.
	case $f in 004[4-9]*|00[5-9]*) rm -f .config ;; esac
	python3 "$src"
	# 0019 writes the C string with a real newline because its Python
	# literal is not raw, so "\n" becomes a line break inside the quotes.
	if [ "$f" = 0019-mt6589-dsi-lk-handover.py ]; then
		python3 - << 'PY'
from pathlib import Path
p = Path("drivers/gpu/drm/mediatek/mtk_dsi.c")
s = p.read_text()
bad = 'dev_info(dev, "keeping bootloader DSI video mode setup\n");'
good = 'dev_info(dev, "keeping bootloader DSI video mode setup\\n");'
if bad not in s:
    if good not in s:
        raise SystemExit("0019: DSI handover dev_info string not found")
else:
    p.write_text(s.replace(bad, good, 1))
    print("FIXED DSI dev_info string newline")
PY
	fi
	# 0026's header adds this include; the script itself does not.
	if [ "$f" = 0026-mt6589-pinctrl-r0-sort-pupd-polarity.py ]; then
		python3 - << 'PY'
from pathlib import Path
p = Path("drivers/pinctrl/mediatek/pinctrl-mt6589.c")
s = p.read_text()
inc = "#include <dt-bindings/pinctrl/mt65xx.h>\n"
if inc not in s:
    needle = '#include "pinctrl-paris.h"\n'
    if needle not in s:
        raise SystemExit("0026: pinctrl-paris.h include missing")
    p.write_text(s.replace(needle, inc + needle, 1))
    print("ADDED mt65xx.h include for MTK_PUPD_SET_R1R0_*")
PY
	fi
	echo "PY $f"
}

# Within one number, a .patch is the base and a .py is the follow-up
# (0023's header says the script runs after the patch).
files=$(python3 - "$P" <<'PY'
import sys
from pathlib import Path
root = Path(sys.argv[1])
rank = {".patch": 0, ".txt": 1, ".py": 2}
def key(p):
    num = p.name.split("-", 1)[0]
    return (num, rank.get(p.suffix, 9), p.name)
for p in sorted(root.iterdir(), key=key):
    if p.name.endswith(".current.txt"):
        continue
    print(p.name)
PY
)
for f in $files; do
	case "$f" in
		*.current.txt) echo "SKIP $f"; continue ;;
		0007-b8000-h-sd-dts.txt) install_sd; continue ;;
		0011-b8000-h-install2-dts.txt) install_install2; continue ;;
		*.patch)
			# 0032 inserts CONFIG_SND_DUMMY=m inside the sound block that
			# 0033 replaces with the built-in driver. The rest of 0033 matches.
			if [ "$f" = "0033-b8000-h-builtin-drivers.patch" ]; then
				python3 - << 'PY'
from pathlib import Path
p = Path("arch/arm/configs/lenovo-blade_defconfig")
t = p.read_text()
old = "CONFIG_SND_HRTIMER=m\nCONFIG_SND_DUMMY=m\n# CONFIG_SND_SOC is not set\n"
new = "CONFIG_SND_HRTIMER=m\n# CONFIG_SND_SOC is not set\n"
if old not in t:
    raise SystemExit("0033 precondition failed: CONFIG_SND_DUMMY=m is not in the sound block")
p.write_text(t.replace(old, new, 1))
print("DROPPED CONFIG_SND_DUMMY=m so 0033 context matches")
PY
			fi
			echo "APPLY $f"
			if git apply --check "$P/$f" 2>/tmp/apply.err; then
				git apply "$P/$f"
			elif git apply --reverse --check "$P/$f" 2>/tmp/apply.err; then
				# A .py with the same number already wrote this change.
				echo "ALREADY $f"
			else
				# Some files were saved as a whole new file after an earlier
				# patch created them. Replace those paths, then apply.
				diffs=$(grep -c '^diff --git ' "$P/$f" || true)
				news=$(grep -c '^new file mode ' "$P/$f" || true)
				if [ "$diffs" != "$news" ] || [ "$news" = 0 ]; then
					cat /tmp/apply.err >&2
					echo "FAILED $f" >&2
					exit 1
				fi
				grep '^diff --git ' "$P/$f" | while read -r _ _ _ b; do
					rm -f "${b#b/}"
				done
				if ! git apply --check "$P/$f" 2>/tmp/apply.err; then
					cat /tmp/apply.err >&2
					echo "FAILED $f" >&2
					exit 1
				fi
				git apply "$P/$f"
				echo "REPLACED $f"
			fi
			;;
		*.py) run_py "$f" ;;
		*) echo "UNHANDLED $f" >&2; exit 1 ;;
	esac
done

# 0022's header copies this file; the script itself only edits Kconfig.
cp "$REPO/linux-port/drivers/mt6589-bls-backlight/mt6589-bls-bl.c" \
	drivers/video/backlight/mt6589-bls-bl.c
rm -f .config
make ARCH=arm LLVM=1 lenovo-blade_defconfig
# 0033 selects the MT6628 modules, but Kconfig hides them unless serdev
# and cfg80211 are on. A plain defconfig drops both.
scripts/config --enable SERIAL_DEV_BUS --module CFG80211 \
	--module MT6628_WMT --module MT6628_WLAN
make ARCH=arm LLVM=1 olddefconfig
echo "PATCHES_OK"
grep -n 'b8000-h' arch/arm/boot/dts/mediatek/Makefile
grep -E 'LOCALVERSION|ARM_APPENDED_DTB|BACKLIGHT_MT6589_BLS|DRM_MEDIATEK|ARM_ERRATA_814220' .config
