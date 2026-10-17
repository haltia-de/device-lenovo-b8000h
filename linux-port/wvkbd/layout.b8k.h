/* Landscape keyboard for the B8000-H.
 * Every row of one layout has the same width sum, so a 1.0 key stays square.
 * Enter on the letter layouts is an inverted Г: its stem is the right-hand
 * column of the two lower letter rows, and its foot is the short bottom bar.
 * Pads under that foot are not keys. The hyphen is the half key directly
 * left of the stem. Comma and period are the wide marks on the bottom
 * letter row. 123 is the left bottom key. The alphabet switch is next.
 * The digits layout leads on to the symbols one (<>| and the rest).
 */

#define KBD_PIXEL_HEIGHT 360
#define KBD_PIXEL_LANDSCAPE_HEIGHT 360
#define KBD_KEY_BORDER 5

enum layout_id {
	Latin = 0,
	Cyrillic,
	Numbers,
	Symbols,
	Index, /* referenced by the compose path; not shown in normal use */
	NumLayouts,
};

static struct key keys_latin[], keys_cyrillic[], keys_numbers[], keys_index[],
	keys_symbols[];

static struct layout layouts[NumLayouts] = {
	[Latin] = {keys_latin, "latin", "latin", true},
	[Cyrillic] = {keys_cyrillic, "cyrillic", "cyrillic", true},
	[Numbers] = {keys_numbers, "latin", "numbers", false},
	[Symbols] = {keys_symbols, "latin", "symbols", false},
	[Index] = {keys_index, "latin", "index", false},
};

/* Row sum 11. */
static struct key keys_latin[] = {
	{"q", "Q", 1.0, Code, KEY_Q},
	{"w", "W", 1.0, Code, KEY_W},
	{"e", "E", 1.0, Code, KEY_E},
	{"r", "R", 1.0, Code, KEY_R},
	{"t", "T", 1.0, Code, KEY_T},
	{"y", "Y", 1.0, Code, KEY_Y},
	{"u", "U", 1.0, Code, KEY_U},
	{"i", "I", 1.0, Code, KEY_I},
	{"o", "O", 1.0, Code, KEY_O},
	{"p", "P", 1.0, Code, KEY_P},
	{"⌫", "⌫", 1.0, Code, KEY_BACKSPACE, .scheme = 1},
	{"", "", 0.0, EndRow},

	{"", "", 0.5, Pad},
	{"a", "A", 1.0, Code, KEY_A},
	{"s", "S", 1.0, Code, KEY_S},
	{"d", "D", 1.0, Code, KEY_D},
	{"f", "F", 1.0, Code, KEY_F},
	{"g", "G", 1.0, Code, KEY_G},
	{"h", "H", 1.0, Code, KEY_H},
	{"j", "J", 1.0, Code, KEY_J},
	{"k", "K", 1.0, Code, KEY_K},
	{"l", "L", 1.0, Code, KEY_L},
	{"-", "-", 0.5, Copy, 0x002D, 0, 0x002D},
	{"↵", "↵", 1.0, Code, KEY_ENTER, .scheme = 1, .lrows = 2, .lfoot = 0.5},
	{"", "", 0.0, EndRow},

	{"⇧", "⇧", 1.0, Mod, Shift, .scheme = 1},
	{"z", "Z", 1.0, Code, KEY_Z},
	{"x", "X", 1.0, Code, KEY_X},
	{"c", "C", 1.0, Code, KEY_C},
	{"v", "V", 1.0, Code, KEY_V},
	{"b", "B", 1.0, Code, KEY_B},
	{"n", "N", 1.0, Code, KEY_N},
	{"m", "M", 1.0, Code, KEY_M},
	/* Copy keeps the printed mark. A code key would change it while shift is on. */
	{",", ",", 0.75, Copy, 0x002C, 0, 0x002C},
	{".", ".", 0.75, Copy, 0x002E, 0, 0x002E},
	{"", "", 0.5, Pad},
	{"", "", 1.0, Pad},
	{"", "", 0.0, EndRow},

	{"123", "123", 1.5, Layout, 0, &layouts[Numbers], .scheme = 1},
	{"РУС", "РУС", 1.5, Layout, 0, &layouts[Cyrillic], .scheme = 1},
	{"space", "space", 7.0, Code, KEY_SPACE},
	{"▼", "▼", 1.0, Hide, 0, .scheme = 2},
	{"", "", 0.0, Last},
};

/* Row sum 13. */
static struct key keys_cyrillic[] = {
	{"й", "Й", 1.0, Code, KEY_Q},
	{"ц", "Ц", 1.0, Code, KEY_W},
	{"у", "У", 1.0, Code, KEY_E},
	{"к", "К", 1.0, Code, KEY_R},
	{"е", "Е", 1.0, Code, KEY_T},
	{"н", "Н", 1.0, Code, KEY_Y},
	{"г", "Г", 1.0, Code, KEY_U},
	{"ш", "Ш", 1.0, Code, KEY_I},
	{"щ", "Щ", 1.0, Code, KEY_O},
	{"з", "З", 1.0, Code, KEY_P},
	{"х", "Х", 1.0, Code, KEY_LEFTBRACE},
	{"ъ", "Ъ", 1.0, Code, KEY_RIGHTBRACE},
	{"⌫", "⌫", 1.0, Code, KEY_BACKSPACE, .scheme = 1},
	{"", "", 0.0, EndRow},

	{"ф", "Ф", 1.0, Code, KEY_A},
	{"ы", "Ы", 1.0, Code, KEY_S},
	{"в", "В", 1.0, Code, KEY_D},
	{"а", "А", 1.0, Code, KEY_F},
	{"п", "П", 1.0, Code, KEY_G},
	{"р", "Р", 1.0, Code, KEY_H},
	{"о", "О", 1.0, Code, KEY_J},
	{"л", "Л", 1.0, Code, KEY_K},
	{"д", "Д", 1.0, Code, KEY_L},
	{"ж", "Ж", 1.0, Code, KEY_SEMICOLON},
	{"э", "Э", 1.0, Code, KEY_APOSTROPHE},
	/* Half of the column under ъ, so the stem still lines up with backspace. */
	{"ё", "Ё", 0.5, Code, KEY_GRAVE},
	{"-", "-", 0.5, Copy, 0x002D, 0, 0x002D},
	{"↵", "↵", 1.0, Code, KEY_ENTER, .scheme = 1, .lrows = 2, .lfoot = 0.5},
	{"", "", 0.0, EndRow},

	{"⇧", "⇧", 1.0, Mod, Shift, .scheme = 1},
	{"я", "Я", 1.0, Code, KEY_Z},
	{"ч", "Ч", 1.0, Code, KEY_X},
	{"с", "С", 1.0, Code, KEY_C},
	{"м", "М", 1.0, Code, KEY_V},
	{"и", "И", 1.0, Code, KEY_B},
	{"т", "Т", 1.0, Code, KEY_N},
	{"ь", "Ь", 1.0, Code, KEY_M},
	{"б", "Б", 1.0, Code, KEY_COMMA},
	{"ю", "Ю", 1.0, Code, KEY_DOT},
	/* KEY_COMMA and KEY_DOT are б and ю on this map. */
	{",", ",", 0.75, Copy, 0x002C, 0, 0x002C},
	{".", ".", 0.75, Copy, 0x002E, 0, 0x002E},
	{"", "", 0.5, Pad},
	{"", "", 1.0, Pad},
	{"", "", 0.0, EndRow},

	/* The bottom row keeps the Latin geometry (1.5 + 1.5 + 7 + 1 of 11),
	 * scaled to 13, so no key moves when the alphabet changes. */
	{"123", "123", 1.5 * 13 / 11, Layout, 0, &layouts[Numbers], .scheme = 1},
	{"ENG", "ENG", 1.5 * 13 / 11, Layout, 0, &layouts[Latin], .scheme = 1},
	{"пробел", "пробел", 7.0 * 13 / 11, Code, KEY_SPACE},
	{"▼", "▼", 1.0 * 13 / 11, Hide, 0, .scheme = 2},
	{"", "", 0.0, Last},
};

/* Row sum 11. Enter stays square: a foot here would cover symbols.
 * Tab, Esc and Ctrl are for the terminal. Shifted marks are Copy keys: the
 * mobile keymap has its own shift level (shift+slash is >, not ?). */
static struct key keys_numbers[] = {
	{"1", "1", 1.0, Code, KEY_1},
	{"2", "2", 1.0, Code, KEY_2},
	{"3", "3", 1.0, Code, KEY_3},
	{"4", "4", 1.0, Code, KEY_4},
	{"5", "5", 1.0, Code, KEY_5},
	{"6", "6", 1.0, Code, KEY_6},
	{"7", "7", 1.0, Code, KEY_7},
	{"8", "8", 1.0, Code, KEY_8},
	{"9", "9", 1.0, Code, KEY_9},
	{"0", "0", 1.0, Code, KEY_0},
	{"⌫", "⌫", 1.0, Code, KEY_BACKSPACE, .scheme = 1},
	{"", "", 0.0, EndRow},

	{"-", "-", 1.0, Code, KEY_MINUS},
	{"_", "_", 1.0, Copy, 0x005F, 0, 0x005F},
	{"=", "=", 1.0, Code, KEY_EQUAL},
	{"+", "+", 1.0, Copy, 0x002B, 0, 0x002B},
	{"/", "/", 1.0, Code, KEY_SLASH},
	{"?", "?", 1.0, Copy, 0x003F, 0, 0x003F},
	{"!", "!", 1.0, Copy, 0x0021, 0, 0x0021},
	{"@", "@", 1.0, Copy, 0x0040, 0, 0x0040},
	{"(", "(", 1.0, Copy, 0x0028, 0, 0x0028},
	{")", ")", 1.0, Copy, 0x0029, 0, 0x0029},
	{"Tab", "Tab", 1.0, Code, KEY_TAB, .scheme = 1},
	{"", "", 0.0, EndRow},

	{",", ",", 1.0, Code, KEY_COMMA},
	{".", ".", 1.0, Code, KEY_DOT},
	{":", ":", 1.0, Copy, 0x003A, 0, 0x003A},
	{";", ";", 1.0, Code, KEY_SEMICOLON},
	{"\"", "\"", 1.0, Copy, 0x0022, 0, 0x0022},
	{"'", "'", 1.0, Code, KEY_APOSTROPHE},
	{"#", "#", 1.0, Copy, 0x0023, 0, 0x0023},
	{"$", "$", 1.0, Copy, 0x0024, 0, 0x0024},
	{"&", "&", 1.0, Copy, 0x0026, 0, 0x0026},
	{"*", "*", 1.0, Copy, 0x002A, 0, 0x002A},
	{"Esc", "Esc", 1.0, Code, KEY_ESC, .scheme = 1},
	{"", "", 0.0, EndRow},

	/* Ctrl holds for the next key, also after АБВ: Ctrl, АБВ, c is Ctrl+C. */
	{"Ctrl", "Ctrl", 1.0, Mod, Ctrl, .scheme = 1},
	{"АБВ", "АБВ", 1.0, BackLayer, .scheme = 1},
	{"<>|", "<>|", 1.0, Layout, 0, &layouts[Symbols], .scheme = 1},
	{"пробел", "пробел", 6.0, Code, KEY_SPACE},
	{"↵", "↵", 1.0, Code, KEY_ENTER, .scheme = 1},
	{"▼", "▼", 1.0, Hide, 0, .scheme = 2},
	{"", "", 0.0, Last},
};

/* Row sum 11, laid out like the digits. The marks a shell needs come first.
 * All marks are Copy keys: the mobile keymap puts other characters on
 * shifted comma and period (apostrophe and question mark, not < and >). */
static struct key keys_symbols[] = {
	{"<", "<", 1.0, Copy, 0x003C, 0, 0x003C},
	{">", ">", 1.0, Copy, 0x003E, 0, 0x003E},
	{"|", "|", 1.0, Copy, 0x007C, 0, 0x007C},
	{"\\", "\\", 1.0, Copy, 0x005C, 0, 0x005C},
	{"~", "~", 1.0, Copy, 0x007E, 0, 0x007E},
	{"`", "`", 1.0, Copy, 0x0060, 0, 0x0060},
	{"^", "^", 1.0, Copy, 0x005E, 0, 0x005E},
	{"%", "%", 1.0, Copy, 0x0025, 0, 0x0025},
	{"[", "[", 1.0, Copy, 0x005B, 0, 0x005B},
	{"]", "]", 1.0, Copy, 0x005D, 0, 0x005D},
	{"⌫", "⌫", 1.0, Code, KEY_BACKSPACE, .scheme = 1},
	{"", "", 0.0, EndRow},

	{"{", "{", 1.0, Copy, 0x007B, 0, 0x007B},
	{"}", "}", 1.0, Copy, 0x007D, 0, 0x007D},
	{"«", "«", 1.0, Copy, 0x00AB, 0, 0x00AB},
	{"»", "»", 1.0, Copy, 0x00BB, 0, 0x00BB},
	{"—", "—", 1.0, Copy, 0x2014, 0, 0x2014},
	{"…", "…", 1.0, Copy, 0x2026, 0, 0x2026},
	{"№", "№", 1.0, Copy, 0x2116, 0, 0x2116},
	{"°", "°", 1.0, Copy, 0x00B0, 0, 0x00B0},
	{"€", "€", 1.0, Copy, 0x20AC, 0, 0x20AC},
	{"₽", "₽", 1.0, Copy, 0x20BD, 0, 0x20BD},
	{"Tab", "Tab", 1.0, Code, KEY_TAB, .scheme = 1},
	{"", "", 0.0, EndRow},

	{"$", "$", 1.0, Copy, 0x0024, 0, 0x0024},
	{"&", "&", 1.0, Copy, 0x0026, 0, 0x0026},
	{"*", "*", 1.0, Copy, 0x002A, 0, 0x002A},
	{"#", "#", 1.0, Copy, 0x0023, 0, 0x0023},
	{"=", "=", 1.0, Copy, 0x003D, 0, 0x003D},
	{"/", "/", 1.0, Copy, 0x002F, 0, 0x002F},
	{"-", "-", 1.0, Copy, 0x002D, 0, 0x002D},
	{"\"", "\"", 1.0, Copy, 0x0022, 0, 0x0022},
	{"'", "'", 1.0, Copy, 0x0027, 0, 0x0027},
	{":", ":", 1.0, Copy, 0x003A, 0, 0x003A},
	{"Esc", "Esc", 1.0, Code, KEY_ESC, .scheme = 1},
	{"", "", 0.0, EndRow},

	{"Ctrl", "Ctrl", 1.0, Mod, Ctrl, .scheme = 1},
	{"АБВ", "АБВ", 1.0, BackLayer, .scheme = 1},
	{"123", "123", 1.0, Layout, 0, &layouts[Numbers], .scheme = 1},
	{"←", "←", 1.0, Code, KEY_LEFT, .scheme = 1},
	{"↑", "↑", 1.0, Code, KEY_UP, .scheme = 1},
	{"↓", "↓", 1.0, Code, KEY_DOWN, .scheme = 1},
	{"→", "→", 1.0, Code, KEY_RIGHT, .scheme = 1},
	{"пробел", "пробел", 2.0, Code, KEY_SPACE},
	{"↵", "↵", 1.0, Code, KEY_ENTER, .scheme = 1},
	{"▼", "▼", 1.0, Hide, 0, .scheme = 2},
	{"", "", 0.0, Last},
};

static struct key keys_index[] = {
	{"АБВ", "АБВ", 1.0, BackLayer, .scheme = 1},
	{"", "", 0.0, Last},
};
