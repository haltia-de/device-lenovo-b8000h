/* Landscape keyboard for the B8000-H.
 * Every row of one layout has the same width sum, so a 1.0 key stays square.
 * Enter on the letter layouts is an inverted Г: its stem is the right-hand
 * column of the two lower letter rows, and its foot is the short bottom bar.
 * Pads under that foot are not keys. The hyphen is the half key directly
 * left of the stem. Comma and period are the wide marks on the bottom
 * letter row. 123 is the left bottom key. The alphabet switch is next.
 */

#define KBD_PIXEL_HEIGHT 360
#define KBD_PIXEL_LANDSCAPE_HEIGHT 360
#define KBD_KEY_BORDER 5

enum layout_id {
	Latin = 0,
	Cyrillic,
	Numbers,
	Index, /* referenced by the compose path; not shown in normal use */
	NumLayouts,
};

static struct key keys_latin[], keys_cyrillic[], keys_numbers[], keys_index[];

static struct layout layouts[NumLayouts] = {
	[Latin] = {keys_latin, "latin", "latin", true},
	[Cyrillic] = {keys_cyrillic, "cyrillic", "cyrillic", true},
	[Numbers] = {keys_numbers, "latin", "numbers", false},
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
	{"пробел", "пробел", 7.0, Code, KEY_SPACE},
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

	{"123", "123", 1.5, Layout, 0, &layouts[Numbers], .scheme = 1},
	{"ENG", "ENG", 1.5, Layout, 0, &layouts[Latin], .scheme = 1},
	{"пробел", "пробел", 9.0, Code, KEY_SPACE},
	{"▼", "▼", 1.0, Hide, 0, .scheme = 2},
	{"", "", 0.0, Last},
};

/* Row sum 11. Enter stays square: a foot here would cover symbols.
 * Tab, Esc and Ctrl are for the terminal. */
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
	{"_", "_", 1.0, Code, KEY_MINUS, 0, Shift},
	{"=", "=", 1.0, Code, KEY_EQUAL},
	{"+", "+", 1.0, Code, KEY_EQUAL, 0, Shift},
	{"/", "/", 1.0, Code, KEY_SLASH},
	{"?", "?", 1.0, Code, KEY_SLASH, 0, Shift},
	{"!", "!", 1.0, Code, KEY_1, 0, Shift},
	{"@", "@", 1.0, Code, KEY_2, 0, Shift},
	{"(", "(", 1.0, Code, KEY_9, 0, Shift},
	{")", ")", 1.0, Code, KEY_0, 0, Shift},
	{"Tab", "Tab", 1.0, Code, KEY_TAB, .scheme = 1},
	{"", "", 0.0, EndRow},

	{",", ",", 1.0, Code, KEY_COMMA},
	{".", ".", 1.0, Code, KEY_DOT},
	{":", ":", 1.0, Code, KEY_SEMICOLON, 0, Shift},
	{";", ";", 1.0, Code, KEY_SEMICOLON},
	{"\"", "\"", 1.0, Code, KEY_APOSTROPHE, 0, Shift},
	{"'", "'", 1.0, Code, KEY_APOSTROPHE},
	{"#", "#", 1.0, Code, KEY_3, 0, Shift},
	{"$", "$", 1.0, Code, KEY_4, 0, Shift},
	{"&", "&", 1.0, Code, KEY_7, 0, Shift},
	{"*", "*", 1.0, Code, KEY_8, 0, Shift},
	{"Esc", "Esc", 1.0, Code, KEY_ESC, .scheme = 1},
	{"", "", 0.0, EndRow},

	/* Ctrl holds for the next key, also after АБВ: Ctrl, АБВ, c is Ctrl+C. */
	{"Ctrl", "Ctrl", 1.0, Mod, Ctrl, .scheme = 1},
	{"АБВ", "АБВ", 1.0, BackLayer, .scheme = 1},
	{"пробел", "пробел", 7.0, Code, KEY_SPACE},
	{"↵", "↵", 1.0, Code, KEY_ENTER, .scheme = 1},
	{"▼", "▼", 1.0, Hide, 0, .scheme = 2},
	{"", "", 0.0, Last},
};

static struct key keys_index[] = {
	{"АБВ", "АБВ", 1.0, BackLayer, .scheme = 1},
	{"", "", 0.0, Last},
};
