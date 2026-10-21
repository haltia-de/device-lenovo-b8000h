# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm: kernel 27. DMA-BUF heaps with the system
# heap (/dev/dma_heap/system): libcamera's software ISP allocates its output
# buffers there and does not start without a dma-buf provider (camera,
# haltia-de/libcamera#1). Both options are bool, so they are built in.
import os

K = os.environ.get('K', '/opt/b8000h/linux-drm') + '/'
WANT = """# DMA-BUF heaps for libcamera's software ISP
CONFIG_DMABUF_HEAPS=y
CONFIG_DMABUF_HEAPS_SYSTEM=y
"""

for cfg in ('arch/arm/configs/lenovo-blade_defconfig', '.config'):
    if not os.path.exists(K + cfg):
        continue
    s = open(K + cfg).read()
    if 'CONFIG_LOCALVERSION="-b8000h27"' in s:
        print('already', cfg)
        continue
    assert 'CONFIG_LOCALVERSION="-b8000h26"' in s, cfg
    s = s.replace('CONFIG_LOCALVERSION="-b8000h26"', 'CONFIG_LOCALVERSION="-b8000h27"')
    for line in WANT.splitlines():
        if line.startswith('CONFIG_'):
            s = s.replace('# %s is not set\n' % line.split('=')[0], '')
    s += WANT
    open(K + cfg, 'w').write(s)
    print('patched', cfg)
