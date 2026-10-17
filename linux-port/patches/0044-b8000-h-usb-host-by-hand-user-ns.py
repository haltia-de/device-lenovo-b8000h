# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm: kernel 24.
#
# USB host for a keyboard, switched by hand. The ID pin of the shared blade
# description (GPIO57) reads 1 with the PC cable, with an OTG adapter and
# with nothing in the socket, and no other pin moves either, so the port
# cannot tell an adapter from a cable. The connector node goes; dr_mode
# becomes "otg", so the glue registers its role switch with userspace
# control (/sys/class/usb_role/*/role). With no connector nothing picks a
# role, so the glue starts as a device, as before: the USB console and
# charging work from boot. The bq24190 OTG boost (usb_otg_vbus) gets a
# regulator-output consumer, whose sysfs state powers the keyboard.
#
# User namespaces, for the bubblewrap sandbox of WebKitGTK browsers and
# Flatpak.
import os

K = os.environ.get('K', '/opt/b8000h/linux-drm') + '/'


def ed(p, old, new, marker):
    s = open(K + p).read()
    if marker in s:
        print('already', p)
        return
    assert s.count(old) == 1, (p, old[:60])
    open(K + p, 'w').write(s.replace(old, new))
    print('patched', p)


ed('arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts', '''/*
 * The ID pin polarity in the shared blade description has not been verified
 * on B8000-H; with correct GPIO input readback it selects host mode and the
 * USB console disappears. Force device mode for bring-up.
 */
&usb0 {
	dr_mode = "peripheral";
};
''', '''/*
 * The ID pin of the shared blade description (GPIO57) reads 1 whatever is
 * in the socket, and the connector driver took that for an adapter: host
 * mode, no USB console. No connector here; the glue starts as a device and
 * userspace switches the role (b8k-usb).
 */
&usb0 {
	dr_mode = "otg";
	/delete-node/ connector;
};
''', '/delete-node/ connector;')

ed('arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts', '''/ {
	/* Vibration motor on the MT6320 VIBR LDO. */
''', '''/ {
	/* 5 V for a USB keyboard in host mode: the bq24190 OTG boost. */
	usb-otg-vbus-output {
		compatible = "regulator-output";
		vout-supply = <&usb_otg_vbus>;
	};

	/* Vibration motor on the MT6320 VIBR LDO. */
''', 'usb-otg-vbus-output')

ed('drivers/usb/musb/mediatek.c', '''	case USB_DR_MODE_OTG:
		glue->phy_mode = PHY_MODE_USB_OTG;
		glue->role = USB_ROLE_NONE;
		break;
	default:
		return dev_err_probe(&pdev->dev, -EINVAL,''', '''	case USB_DR_MODE_OTG: {
		struct device_node *conn __free(device_node) =
			of_get_child_by_name(np, "connector");

		/*
		 * Without a connector nothing sets a role: stay a device
		 * until userspace asks for host.
		 */
		if (!conn) {
			glue->phy_mode = PHY_MODE_USB_DEVICE;
			glue->role = USB_ROLE_DEVICE;
			break;
		}
		glue->phy_mode = PHY_MODE_USB_OTG;
		glue->role = USB_ROLE_NONE;
		break;
	}
	default:
		return dev_err_probe(&pdev->dev, -EINVAL,''', 'until userspace asks for host')

for cfg in ('arch/arm/configs/lenovo-blade_defconfig', '.config'):
    if not os.path.exists(K + cfg):
        continue
    s = open(K + cfg).read()
    if 'CONFIG_LOCALVERSION="-b8000h24"' in s:
        print('already', cfg)
        continue
    assert 'CONFIG_LOCALVERSION="-b8000h23"' in s, cfg
    s = s.replace('CONFIG_LOCALVERSION="-b8000h23"', 'CONFIG_LOCALVERSION="-b8000h24"')
    s = s.replace('# CONFIG_USER_NS is not set\n', '')
    s = s.replace('# CONFIG_REGULATOR_USERSPACE_CONSUMER is not set\n', '')
    s += ('# bubblewrap sandboxes (WebKitGTK, Flatpak)\nCONFIG_USER_NS=y\n'
          '# OTG boost switch for USB host (b8k-usb)\nCONFIG_REGULATOR_USERSPACE_CONSUMER=y\n')
    open(K + cfg, 'w').write(s)
    print('patched', cfg)
