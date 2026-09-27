import re
p='/opt/b8000h/linux-drm/drivers/pinctrl/mediatek/pinctrl-mt6589.c'
s=open(p).read()
m=re.search(r'(static const struct mtk_pin_field_calc mt6589_pin_r0_range\[\] = \{\n)(.*?)(\n\};)', s, re.S)
ents=re.findall(r'PIN_FIELD_R0\((\d+), (\d+)\)', m.group(2))
ents=sorted(ents, key=lambda e:int(e[1]))
body='\t/* sorted by pin: mtk_hw_pin_field_lookup() does a binary search */\n'+'\n'.join('\tPIN_FIELD_R0(%s, %s),'%e for e in ents)
s=s[:m.start(2)]+body+s[m.end(2):]
if 'mt6589_bias_set_combo' not in s:
    s=s.replace('static const struct mtk_pin_soc mt6589_pinctrl_data = {','''/*
 * MT6589 PUPD polarity is the opposite of the generic PUPD/R1/R0 code:
 * '1' = pull up (vendor mt_sd.h MSDC*_PUPD_BASE), verified on MSDC3.
 */
static int mt6589_bias_set_combo(struct mtk_pinctrl *hw,
				 const struct mtk_pin_desc *desc,
				 u32 pullup, u32 arg)
{
	if (arg >= MTK_PUPD_SET_R1R0_00 && arg <= MTK_PUPD_SET_R1R0_11)
		pullup = !pullup;
	return mtk_pinconf_bias_set_combo(hw, desc, pullup, arg);
}

static const struct mtk_pin_soc mt6589_pinctrl_data = {''',1)
    s=s.replace('\t.bias_set_combo = mtk_pinconf_bias_set_combo,','\t.bias_set_combo = mt6589_bias_set_combo,',1)
open(p,'w').write(s)
print(len(ents),'R0 entries sorted')

# plus: #include <dt-bindings/pinctrl/mt65xx.h> before pinctrl-paris.h
