#ifndef config_h_INCLUDED
#define config_h_INCLUDED

#define DEFAULT_FONT "Sans 28"
#define DEFAULT_ROUNDING 10
static const int transparency = 255;

struct clr_scheme schemes[] = {
{
  /* letter keys: b8k palette, light grey key on the dark frame */
  .bg = {.bgra = {36, 36, 36, transparency}},
  .fg = {.bgra = {74, 74, 74, transparency}},
  .high = {.bgra = {228, 132, 53, transparency}},
  .swipe = {.bgra = {228, 132, 53, transparency}},
  .text = {.color = UINT32_MAX},
  .text_press = {.color = UINT32_MAX},
  .text_swipe = {.color = UINT32_MAX},
  .font = DEFAULT_FONT,
  .rounding = DEFAULT_ROUNDING,
},
{
  /* actions: shift, backspace, enter, layout; a darker key */
  .bg = {.bgra = {36, 36, 36, transparency}},
  .fg = {.bgra = {48, 48, 48, transparency}},
  .high = {.bgra = {228, 132, 53, transparency}},
  .swipe = {.bgra = {228, 132, 53, transparency}},
  .text = {.color = UINT32_MAX},
  .text_press = {.color = UINT32_MAX},
  .text_swipe = {.color = UINT32_MAX},
  .font = DEFAULT_FONT,
  .rounding = DEFAULT_ROUNDING,
},
{
  /* hide key only: the accent */
  .bg = {.bgra = {36, 36, 36, transparency}},
  .fg = {.bgra = {228, 132, 53, transparency}},
  .high = {.bgra = {216, 113, 28, transparency}},
  .swipe = {.bgra = {216, 113, 28, transparency}},
  .text = {.color = UINT32_MAX},
  .text_press = {.color = UINT32_MAX},
  .text_swipe = {.color = UINT32_MAX},
  .font = DEFAULT_FONT,
  .rounding = DEFAULT_ROUNDING,
}
};

/* Portrait and landscape use the same three layers. The tablet stays landscape. */
static enum layout_id layers[] = {
  Latin,
  Cyrillic,
  Numbers,
  NumLayouts
};

static enum layout_id landscape_layers[] = {
  Latin,
  Cyrillic,
  Numbers,
  NumLayouts
};

#endif // config_h_INCLUDED
