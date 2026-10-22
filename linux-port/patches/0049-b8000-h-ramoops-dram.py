# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm. A pstore large enough for a whole oops
# (device#38). The SoC dtsi puts ramoops in 7 KiB of on-chip SRAM with
# 1 KiB records: the SDIO panic of 22 Oct 2026 (#39) wrote only its first
# lines ("pstore: backend (ramoops) writing error (-28)"). This moves it to
# 1 MiB of DRAM right below LK's framebuffer (0xbf500000, which LK and the
# kernel otherwise leave alone across a warm reset): 32 KiB records, 256 KiB
# of console, ECC to tell a damaged record. Verified on the tablet with a
# forced panic: the record survives the watchdog reboot.
import os

K = os.environ.get('K', '/opt/b8000h/linux-drm') + '/'
p = K + 'arch/arm/boot/dts/mediatek/mt6589-lenovo-blade.dtsi'
s = open(p).read()
NODE = """
/* pstore in DRAM (device-lenovo-b8000h#38): the SoC's SRAM one is 7 KiB. */
/ {
	reserved-memory {
		/delete-node/ ramoops@10dc00;

		ramoops@bf500000 {
			compatible = "ramoops";
			reg = <0xbf500000 0x100000>;
			record-size = <0x8000>;
			console-size = <0x40000>;
			ecc-size = <16>;
			no-map;
		};
	};
};
"""
if 'ramoops@bf500000' in s:
    print('already')
else:
    open(p, 'w').write(s + NODE)
    print('patched')
