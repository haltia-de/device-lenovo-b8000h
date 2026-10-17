# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm: kernel 25. Netfilter with nftables,
# connection tracking and NFQUEUE, all but the core as modules, for the
# optional DPI bypass (b8k-zapret: nfqws from zapret takes the first
# packets of each TLS connection from a queue). Without b8k-zapret on
# nothing of it loads.
import os

K = os.environ.get('K', '/opt/b8000h/linux-drm') + '/'
WANT = """# Netfilter for b8k-zapret (nfqws through an nftables queue)
CONFIG_NETFILTER=y
CONFIG_NETFILTER_ADVANCED=y
CONFIG_NETFILTER_NETLINK_QUEUE=m
CONFIG_NF_CONNTRACK=m
CONFIG_NF_TABLES=m
CONFIG_NF_TABLES_INET=y
CONFIG_NF_TABLES_IPV4=y
CONFIG_NF_TABLES_IPV6=y
CONFIG_NFT_CT=m
CONFIG_NFT_QUEUE=m
CONFIG_NFT_REJECT=m
"""

for cfg in ('arch/arm/configs/lenovo-blade_defconfig', '.config'):
    if not os.path.exists(K + cfg):
        continue
    s = open(K + cfg).read()
    if 'CONFIG_LOCALVERSION="-b8000h25"' in s:
        print('already', cfg)
        continue
    assert 'CONFIG_LOCALVERSION="-b8000h24"' in s, cfg
    s = s.replace('CONFIG_LOCALVERSION="-b8000h24"', 'CONFIG_LOCALVERSION="-b8000h25"')
    for line in WANT.splitlines():
        if line.startswith('CONFIG_'):
            s = s.replace('# %s is not set\n' % line.split('=')[0], '')
    s += WANT
    open(K + cfg, 'w').write(s)
    print('patched', cfg)
