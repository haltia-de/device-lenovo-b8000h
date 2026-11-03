# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm. Kernel 29: the build number in the
# release selects the GPU module directory (/usr/lib/b8k/kN) and the
# kernel tarball's module path, so the k27 and k28 modules stay where they
# are and the previous boot.img keeps booting. Bump it whenever a build
# changes the kernel image or its configuration.
K = '/opt/b8000h/linux-drm/'
p = K + 'arch/arm/configs/lenovo-blade_defconfig'
s = open(p).read()
if 'CONFIG_LOCALVERSION="-b8000h29"' in s:
    print('already')
elif 'CONFIG_LOCALVERSION="-b8000h27"' in s:
    open(p, 'w').write(s.replace('CONFIG_LOCALVERSION="-b8000h27"',
                                 'CONFIG_LOCALVERSION="-b8000h29"'))
    print('patched')
else:
    raise SystemExit('unexpected CONFIG_LOCALVERSION in the defconfig')
