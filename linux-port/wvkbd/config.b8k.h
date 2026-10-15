#ifndef config_h_INCLUDED
#define config_h_INCLUDED

#define DEFAULT_FONT "Sans 28"
#define DEFAULT_ROUNDING 10
static const int transparency = 255;

struct clr_scheme schemes[] = {
{
  /* letter keys: light face, dark text */
  .bg = {.bgra = {24, 26, 30, transparency}},
  .fg = {.bgra = {236, 236, 236, transparency}},
  .high = {.bgra = {186, 206, 232, transparency}},
  .swipe = {.bgra = {186, 206, 232, transparency}},
  .text = {.bgra = {20, 20, 20, transparency}},
  .text_press = {.bgra = {10, 16, 28, transparency}},
  .text_swipe = {.bgra = {10, 16, 28, transparency}},
  .font = DEFAULT_FONT,
  .rounding = DEFAULT_ROUNDING,
},
{
  /* actions: shift, backspace, enter, layout */
  .bg = {.bgra = {24, 26, 30, transparency}},
  .fg = {.bgra = {168, 98, 36, transparency}},
  .high = {.bgra = {210, 140, 56, transparency}},
  .swipe = {.bgra = {210, 140, 56, transparency}},
  .text = {.color = UINT32_MAX},
  .text_press = {.color = UINT32_MAX},
  .text_swipe = {.color = UINT32_MAX},
  .font = DEFAULT_FONT,
  .rounding = DEFAULT_ROUNDING,
},
{
  /* hide key only */
  .bg = {.bgra = {24, 26, 30, transparency}},
  .fg = {.bgra = {48, 132, 214, transparency}},
  .high = {.bgra = {32, 96, 170, transparency}},
  .swipe = {.bgra = {32, 96, 170, transparency}},
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
