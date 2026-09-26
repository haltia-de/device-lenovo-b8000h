d = '/opt/b8000h/linux-drm/arch/arm/boot/dts/mediatek/mt6589-lenovo-b8000-h.dts'
s = open(d).read()
old = '''			panel@0 {
				compatible = "lenovo,b8000-panel";
				reg = <0>;
			};
'''
new = '''			panel@0 {
				compatible = "lenovo,b8000-panel";
				reg = <0>;

				port {
					panel_in: endpoint {
						remote-endpoint = <&dsi0_out>;
					};
				};
			};

			/* mtk_dsi looks the panel up through the OF graph (port 1). */
			ports {
				#address-cells = <1>;
				#size-cells = <0>;

				port@1 {
					reg = <1>;

					dsi0_out: endpoint {
						remote-endpoint = <&panel_in>;
					};
				};
			};
'''
if 'dsi0_out' not in s:
    assert s.count(old) == 1
    s = s.replace(old, new)
    open(d, 'w').write(s)
print('ok')
