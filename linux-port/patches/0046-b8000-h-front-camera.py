# SPDX-License-Identifier: GPL-2.0
# Apply to /opt/b8000h/linux-drm: kernel 26. The front camera, stage 1
# (device-lenovo-b8000h#17): the OmniVision OV9760 sensor driver and the
# MT6589 SENINF driver that gives it its 24 MHz MCLK and a media device,
# both as modules, with the device tree nodes on the B8000-H. The driver
# sources are copied by ci/apply-patches.sh from linux-port/drivers/ov9760
# and linux-port/drivers/mt6589-seninf; this script only adds Kconfig and
# Makefile entries, the DT and the config.
import os

K = os.environ.get('K', '/opt/b8000h/linux-drm') + '/'


def edit(path, anchor, text, marker, before=False):
    p = K + path
    s = open(p).read()
    if marker in s:
        print('already', path)
        return
    assert s.count(anchor) == 1, (path, anchor)
    s = s.replace(anchor, text + anchor if before else anchor + text)
    open(p, 'w').write(s)
    print('patched', path)


edit('drivers/media/i2c/Kconfig', 'config VIDEO_OV9734\n', '''config VIDEO_OV9760
	tristate "OmniVision OV9760 sensor support"
	depends on OF && GPIOLIB
	select V4L2_CCI_I2C
	help
	  This is a Video4Linux2 sensor driver for the OmniVision OV9760
	  1.6 Mpixel RAW10 camera with one MIPI CSI-2 lane.

	  To compile this driver as a module, choose M here: the
	  module's name is ov9760.

''', 'config VIDEO_OV9760', before=True)

edit('drivers/media/i2c/Makefile', 'obj-$(CONFIG_VIDEO_OV9734) += ov9734.o\n',
     'obj-$(CONFIG_VIDEO_OV9760) += ov9760.o\n', 'CONFIG_VIDEO_OV9760')

edit('drivers/media/platform/mediatek/Kconfig',
     'source "drivers/media/platform/mediatek/mdp3/Kconfig"\n', '''
config VIDEO_MT6589_SENINF
	tristate "MediaTek MT6589 SENINF sensor interface"
	depends on ARCH_MEDIATEK || COMPILE_TEST
	depends on VIDEO_DEV && COMMON_CLK && PM
	select MEDIA_CONTROLLER
	select V4L2_FWNODE
	select VIDEO_V4L2_SUBDEV_API
	select VIDEOBUF2_DMA_CONTIG
	help
	  The camera receive path of the MediaTek MT6589: the sensor master
	  clock (MCLK), the CSI-2 receiver (SENINF, MIPI-RX) and raw frames
	  through the ISP pass 1 to memory (IMGO), as a V4L2 video device.
''', 'VIDEO_MT6589_SENINF')

edit('drivers/media/platform/mediatek/Makefile', 'obj-y += mdp3/\n',
     'obj-$(CONFIG_VIDEO_MT6589_SENINF) += mt6589-seninf.o\n',
     'CONFIG_VIDEO_MT6589_SENINF')

DTS = '''
/*
 * Front camera (device-lenovo-b8000h#17, docs/camera-front.md): OmniVision
 * OV9760 on I2C 1 at 0x10, one CSI-2 lane. Supplies, pins and the order
 * follow the stock kdCISModulePowerOn: VCAMA 2.8 V, VGP2 1.8 V, RST GPIO32
 * and PDN GPIO31, both active low. The stock code also names VGP5, which is
 * the touch panel's 2.8 V rail and stays with it. MCLK (24 MHz) comes from
 * SENINF TG1, which the receiver driver of stage 2 will also drive.
 */
&mt6320_ldo_vcama {
	regulator-min-microvolt = <2800000>;
	regulator-max-microvolt = <2800000>;
};

&mt6320_ldo_vgp2 {
	regulator-min-microvolt = <1800000>;
	regulator-max-microvolt = <1800000>;
};

&pio {
	camera_mclk_pins: camera-mclk-default {
		pin-mclk {
			pinmux = <MT6589_GPIO210__FUNC_CMMCLK>;
		};
	};

	/*
	 * The CSI1 lanes share pads with the parallel camera interface
	 * (CMDAT0..9, CMHSYNC, CMVSYNC: pins 197-208): GPIO mode with the
	 * input buffers off, as the stock initTg2CSI2 leaves them (IES 0x1C0
	 * bits 5..15, 0x1D0 bit 0).
	 */
	camera_csi1_pins: camera-csi1-default {
		pins-csi1 {
			pinmux = <MT6589_GPIO197__FUNC_GPIO197>,
				 <MT6589_GPIO198__FUNC_GPIO198>,
				 <MT6589_GPIO199__FUNC_GPIO199>,
				 <MT6589_GPIO200__FUNC_GPIO200>,
				 <MT6589_GPIO201__FUNC_GPIO201>,
				 <MT6589_GPIO202__FUNC_GPIO202>,
				 <MT6589_GPIO203__FUNC_GPIO203>,
				 <MT6589_GPIO204__FUNC_GPIO204>,
				 <MT6589_GPIO205__FUNC_GPIO205>,
				 <MT6589_GPIO206__FUNC_GPIO206>,
				 <MT6589_GPIO207__FUNC_GPIO207>,
				 <MT6589_GPIO208__FUNC_GPIO208>;
			input-disable;
		};
	};
};

&{/soc} {
	/*
	 * SENINF with the parts of the camera block the receive path uses:
	 * ISP pass 1 (TG2, IMGO), MIPI-RX analog (CSI0/CSI1, after the DSI PHY
	 * at 0x10012000) and MIPI-RX calibration. The ISP interrupt is GIC SPI
	 * 151, level low, as in the stock camera_isp.c.
	 */
	seninf: camera-interface@15008000 {
		compatible = "mediatek,mt6589-seninf";
		reg = <0x15008000 0x1000>,
		      <0x15004000 0x1000>,
		      <0x10012800 0x30>,
		      <0x1500c000 0x100>;
		reg-names = "seninf", "isp", "mipi-rx", "mipi-rx-cal";
		interrupts = <GIC_SPI 151 IRQ_TYPE_LEVEL_LOW>;
		clocks = <&imgsys CLK_IMAGE_CAM_CAM>,
			 <&imgsys CLK_IMAGE_SEN_TG>,
			 <&imgsys CLK_IMAGE_SEN_CAM>,
			 <&imgsys CLK_IMAGE_CAM_SMI>,
			 <&imgsys CLK_IMAGE_LARB4_SMI>,
			 <&imgsys CLK_IMAGE_COMMON_SMI>;
		clock-names = "cam", "tg", "sen", "cam-smi", "larb", "smi";
		power-domains = <&spm MT6589_POWER_DOMAIN_ISP>;
		pinctrl-names = "default";
		pinctrl-0 = <&camera_mclk_pins>, <&camera_csi1_pins>;
		#clock-cells = <0>;

		port {
			seninf_in: endpoint {
				remote-endpoint = <&ov9760_out>;
				data-lanes = <1>;
			};
		};
	};
};

&i2c1 {
	camera@10 {
		compatible = "ovti,ov9760";
		reg = <0x10>;
		clocks = <&seninf>;
		avdd-supply = <&mt6320_ldo_vcama>;
		dovdd-supply = <&mt6320_ldo_vgp2>;
		reset-gpios = <&pio 32 GPIO_ACTIVE_LOW>;
		powerdown-gpios = <&pio 31 GPIO_ACTIVE_LOW>;
		orientation = <0>;	/* front */

		port {
			ov9760_out: endpoint {
				remote-endpoint = <&seninf_in>;
				data-lanes = <1>;
				link-frequencies = /bits/ 64 <300000000>;
			};
		};
	};
};
'''
p = K + 'arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s = open(p).read()
if 'ovti,ov9760' in s:
    print('already dts')
else:
    open(p, 'w').write(s + DTS)
    print('patched dts')

WANT = """# Front camera, stage 1: OV9760 and SENINF (MCLK, media device)
CONFIG_MEDIA_SUPPORT=m
CONFIG_MEDIA_SUPPORT_FILTER=y
CONFIG_MEDIA_CAMERA_SUPPORT=y
CONFIG_MEDIA_PLATFORM_SUPPORT=y
CONFIG_VIDEO_DEV=m
CONFIG_MEDIA_CONTROLLER=y
CONFIG_VIDEO_V4L2_SUBDEV_API=y
CONFIG_VIDEO_CAMERA_SENSOR=y
CONFIG_VIDEO_OV9760=m
CONFIG_MEDIA_PLATFORM_DRIVERS=y
CONFIG_VIDEO_MT6589_SENINF=m
"""

for cfg in ('arch/arm/configs/lenovo-blade_defconfig', '.config'):
    if not os.path.exists(K + cfg):
        continue
    s = open(K + cfg).read()
    if 'CONFIG_LOCALVERSION="-b8000h26"' in s:
        print('already', cfg)
        continue
    assert 'CONFIG_LOCALVERSION="-b8000h25"' in s, cfg
    s = s.replace('CONFIG_LOCALVERSION="-b8000h25"', 'CONFIG_LOCALVERSION="-b8000h26"')
    for line in WANT.splitlines():
        if line.startswith('CONFIG_'):
            s = s.replace('# %s is not set\n' % line.split('=')[0], '')
    s += WANT
    open(K + cfg, 'w').write(s)
    print('patched', cfg)
