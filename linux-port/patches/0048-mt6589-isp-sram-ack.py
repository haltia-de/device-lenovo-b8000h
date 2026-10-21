# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm. The MT6589 ISP power domain acknowledges
# its SRAM power-down with two bits (SPM_ISP_PWR_CON 13:12), not four: with
# GENMASK(15, 12) scpsys_sram_disable() timed out, the power-off stopped
# halfway and the domain stayed on with its SRAM down. After the first
# system sleep IMGO then wrote nothing but zeros until a reboot
# (SPM_ISP_PWR_CON read 0x3f0d instead of 0x000d; device#30 DEV2-6).
import os

K = os.environ.get('K', '/opt/b8000h/linux-drm') + '/'
p = K + 'drivers/pmdomain/mediatek/mt6589-pm-domains.h'
s = open(p).read()
old = """		.name = "isp",
		.sta_mask = PWR_STATUS_ISP,
		.ctl_offs = SPM_ISP_PWR_CON,
		.sram_pdn_bits = GENMASK(11, 8),
		.sram_pdn_ack_bits = GENMASK(15, 12),"""
new = old.replace('.sram_pdn_ack_bits = GENMASK(15, 12),', '.sram_pdn_ack_bits = GENMASK(13, 12),')
if new in s:
    print('already')
else:
    assert s.count(old) == 1, p
    open(p, 'w').write(s.replace(old, new))
    print('patched')
