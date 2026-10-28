# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm: kernel 28, a TEST kernel for device#39 and
# #50 (stack-protector panics in __mmc_claim_host, user processes killed by
# SIGSEGV/SIGILL on the same boots). Not for a release: drop this file and
# 0051 once the writer is found, and give the clean kernel its own number.
#
# - KFENCE samples heap allocations (one in 100 ms) and catches out-of-bounds
#   writes and use-after-free with the writer's stack; about 1% cost.
# - DMA_API_DEBUG checks DMA mappings: overlapping ones, freeing memory still
#   mapped, mapping stack or vmalloc memory (MSDC3 serves the MT6628).
# - DEBUG_LIST and BUG_ON_DATA_CORRUPTION stop on a corrupted list, the first
#   thing a stray write into a wait queue entry breaks.
# - SCHED_STACK_END_CHECK catches a task that ran over its stack.
# - MAGIC_SYSRQ with every key function off and none over the serial line:
#   only root's /proc/sysrq-trigger works ("echo c" to test pstore, #38).
import os

K = os.environ.get('K', '/opt/b8000h/linux-drm') + '/'
WANT = """# Memory checks of the test kernel 28 (device#39)
CONFIG_DEBUG_KERNEL=y
CONFIG_KFENCE=y
CONFIG_DMA_API_DEBUG=y
CONFIG_DEBUG_LIST=y
CONFIG_BUG_ON_DATA_CORRUPTION=y
CONFIG_SCHED_STACK_END_CHECK=y
CONFIG_MAGIC_SYSRQ=y
CONFIG_MAGIC_SYSRQ_DEFAULT_ENABLE=0x0
# CONFIG_MAGIC_SYSRQ_SERIAL is not set
"""

for cfg in ('arch/arm/configs/lenovo-blade_defconfig', '.config'):
    if not os.path.exists(K + cfg):
        continue
    s = open(K + cfg).read()
    if 'CONFIG_LOCALVERSION="-b8000h28"' in s:
        print('already', cfg)
        continue
    assert 'CONFIG_LOCALVERSION="-b8000h27"' in s, cfg
    s = s.replace('CONFIG_LOCALVERSION="-b8000h27"', 'CONFIG_LOCALVERSION="-b8000h28"')
    for line in WANT.splitlines():
        if line.startswith('CONFIG_'):
            s = s.replace('# %s is not set\n' % line.split('=')[0], '')
    s += WANT
    open(K + cfg, 'w').write(s)
    print('patched', cfg)
