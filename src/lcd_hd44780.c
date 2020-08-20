#include "lcd_hd44780.h"
#include <stddef.h>


// Clear Display and Return Home commands
static const uint32_t DELAY_CLRRET_US = 1530;
// Read Data from RAM and Write Data to RAM commands
static const uint32_t DELAY_READWRITE_US = 43;
// Read Busy Flag and Address command
static const uint32_t DELAY_BUSYFLAG_US = 0;
// Entry Mode Set, Display ON/OFF Control, Cursor or Display Shift,
// Function Set, Set CGRAM Address, Set DDRAM Address commands
static const uint32_t DELAY_CONTROL_US = 39;
// TODO: ...
static const uint32_t DELAY_ENA_STROBE_US = 1;
// ...
static const uint32_t DELAY_INIT0_US = 4100;
static const uint32_t DELAY_INIT1_US = 100;

enum ua_sym {
	G_UPPER_CASE = 0, 	// 'Ґ'
	G_LOWER_CASE,		// 'ґ'
	YI_UPPER_CASE,		// 'Є'
	YI_LOWER_CASE,		// 'є'
	YE_UPPER_CASE,		// 'Ї'
	YE_LOWER_CASE,		// 'ї'
	SOFTSIGN,			// 'ь'
	TEMP_SYM,			// '°'
	__LAST
};

static const uint8_t ua_pattern[__LAST][__LAST] = {
	[G_UPPER_CASE] = {0x01, 0x1F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00},
	[G_LOWER_CASE] = {0x00, 0x01, 0x1F, 0x10, 0x10, 0x10, 0x10, 0x00},
	[YI_UPPER_CASE] = {0x0A, 0x00, 0x0E, 0x04, 0x04, 0x04, 0x0e, 0x00},
	[YI_LOWER_CASE] = {0x09, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0e, 0x00},
	[YE_UPPER_CASE] = {0x0E, 0x11, 0x10, 0x1C, 0x10, 0x11, 0x0E, 0x00},
	[YE_LOWER_CASE] = {0x00, 0x00, 0x0E, 0x11, 0x1C, 0x11, 0x0E, 0x00},
	[SOFTSIGN] = {0x00, 0x00, 0x10, 0x10, 0x1C, 0x12, 0x1C, 0x00},
	[TEMP_SYM] = {0x0C, 0x12, 0x12, 0x0C, 0x00, 0x00, 0x00, 0x00}
};

sk_err sk_lcd_set_backlight(struct sk_lcd *lcd, uint8_t level)
{
	if (NULL == lcd)
		return SK_EWRONGARG;

	if (NULL != lcd->set_backlight_func) {
		// try to set with user provided function
		lcd->set_backlight_func(level);
	} else if (NULL != lcd->pin_bkl) {
		// fallback to direct pin control if available
		sk_pin_set(*lcd->pin_bkl, (bool)level);
	} else {
		return SK_EWRONGARG;
	}

	return SK_EOK;
}


/**
  * Private: Provides abstaction over two delay functions passed when constructing sk_lcd object
  *
  * Detect if optimal delay function is applicable and use it when possible with fallback
  * to unoptimal variants
  */
static void lcd_delay_us(struct sk_lcd *lcd, uint32_t us)
{
	if (NULL == lcd)
		return;

	sk_delay_func_t msfunc = lcd->delay_func_ms,
					usfunc = lcd->delay_func_us;

	if ((NULL == msfunc) && (NULL == usfunc))
		return;

	if (NULL == msfunc) {
		// only us-resolution func is set -> use unoptimal us delay
		usfunc(us);
		return;
	}

	if (NULL == usfunc) {
		// only ms-resolution func is set -> use rounded us delay
		msfunc((us % 1000) ? (1 + us / 1000) : (us / 1000));
		return;
	}

	// both functions are set -> use ms delay for divisor and us for remainder
	if (us / 1000)
		msfunc(us / 1000);
	if (us % 1000)
		usfunc(us % 1000);
}


static void lcd_data_set_halfbyte(struct sk_lcd *lcd, uint8_t half)
{
	sk_pin_set(*lcd->pin_en, true);
	sk_pin_group_set(*lcd->pin_group_data, half & 0x0F);
	lcd_delay_us(lcd, DELAY_ENA_STROBE_US);
	sk_pin_set(*lcd->pin_en, false);
	lcd_delay_us(lcd, DELAY_ENA_STROBE_US);
}


static void lcd_data_set_byte(struct sk_lcd *lcd, uint8_t byte)
{
	if (lcd->is4bitinterface) {
		lcd_data_set_halfbyte(lcd, byte >> 4);
		lcd_data_set_halfbyte(lcd, byte & 0x0F);
	} else {
		// 8 bit data interface

		// (!) not implemented yet
	}
}


static void lcd_rsrw_set(struct sk_lcd *lcd, bool rs, bool rw)
{
	sk_pin_set(*lcd->pin_rs, rs);
	sk_pin_set(*lcd->pin_rw, rw);
}


static void lcd_send_byte(struct sk_lcd *lcd, bool rs, uint8_t byte)
{
	lcd_rsrw_set(lcd, rs, false);
	lcd_data_set_byte(lcd, byte);
}


void lcd_print_char(struct sk_lcd *lcd, char ch)
{
	lcd_send_byte(lcd, true, (uint8_t)ch);
	lcd_delay_us(lcd, DELAY_CONTROL_US);
}


void lcd_set_commad(struct sk_lcd *lcd, uint8_t cmd)
{
	lcd_send_byte(lcd, false, cmd);
}


void lcd_set_cgram_addr(struct sk_lcd *lcd, uint8_t addr)
{
	// 3 - 0 bit SGRAM address
	lcd_set_commad(lcd, addr | 0x40);
}


void lcd_set_ddram_addr(struct sk_lcd *lcd, uint8_t addr)
{
	// 4 - 0 bit DGRAM address
	lcd_set_commad(lcd, addr | 0x80);
}


static void sk_lcd_init_ua_char(struct sk_lcd *lcd)
{
	void set_char_pattern(struct sk_lcd *lcd, uint8_t *pattern)
	{
		for (int i = 0; i < __LAST; i++)
			lcd_send_byte(lcd, true, pattern[i]);
	}

	lcd_set_cgram_addr(lcd, 0x00);
	set_char_pattern(lcd, ua_pattern[G_UPPER_CASE]);

	lcd_set_cgram_addr(lcd, 0x08);
	set_char_pattern(lcd, ua_pattern[G_LOWER_CASE]);

	lcd_set_cgram_addr(lcd, 0x10);
	set_char_pattern(lcd, ua_pattern[YI_UPPER_CASE]);

	lcd_set_cgram_addr(lcd, 0x18);
	set_char_pattern(lcd, ua_pattern[YI_LOWER_CASE]);

	lcd_set_cgram_addr(lcd, 0x020);
	set_char_pattern(lcd, ua_pattern[YE_UPPER_CASE]);

	lcd_set_cgram_addr(lcd, 0x028);
	set_char_pattern(lcd, ua_pattern[YE_LOWER_CASE]);

	lcd_set_cgram_addr(lcd, 0x030);
	set_char_pattern(lcd, ua_pattern[SOFTSIGN]);

	lcd_set_cgram_addr(lcd, 0x038);
	set_char_pattern(lcd, ua_pattern[TEMP_SYM]);
}

void sk_lcd_putchar(struct sk_lcd *lcd, const char ch)
{
	uint8_t conv = lcd->charmap_func(ch);
	lcd_send_byte(lcd, true, conv);
}


void lcd_print_str(struct sk_lcd *lcd, char *str)
{
	uint8_t cnt = 0;
	while(str[cnt]){
		// lcd_print_char(lcd, str[cnt++]);
		sk_lcd_putchar(lcd, str[cnt++]);
	}
}


void lcd_return_home(struct sk_lcd *lcd)
{
	lcd_set_commad(lcd, 0x02);
}


void lcd_clear(struct sk_lcd *lcd)
{
	lcd_rsrw_set(lcd, 0, 0);
	lcd_data_set_byte(lcd, 0x01);
	lcd_delay_us(lcd, DELAY_CLRRET_US);
}


void lcd_set_cursor(struct sk_lcd *lcd, uint8_t x, uint8_t y)
{
	uint8_t cursor_addr = 0;
	switch (y) {
		case 0: cursor_addr = x;
			break;
		case 1: cursor_addr = (0x40 + x);
			break;
	}
	lcd_set_ddram_addr(lcd, cursor_addr);
}


void lcd_init_4bit(struct sk_lcd *lcd)
{
	lcd_rsrw_set(lcd, 0, 0);
	lcd_data_set_halfbyte(lcd, 0x03);
	lcd_delay_us(lcd, DELAY_INIT0_US);

	lcd_rsrw_set(lcd, 0, 0);
	lcd_data_set_halfbyte(lcd, 0x02);
	lcd_delay_us(lcd, DELAY_INIT1_US);

	lcd_rsrw_set(lcd, 0, 0);
	lcd_data_set_halfbyte(lcd, 0x02);
	lcd_delay_us(lcd, DELAY_CONTROL_US);

	// set display on/off: bit2 -- display on (D), bit1 -- cursor on (C), bit 0 -- blink on (B)
	lcd_rsrw_set(lcd, 0, 0);
	lcd_data_set_byte(lcd, 0x08 | 0x06);
	lcd_delay_us(lcd, DELAY_CONTROL_US);

	// clear display
	lcd_clear(lcd);

	// entry mode set: bit1 -- decrement/increment cnt (I/D), bit0 -- display noshift / shift (SH)
	lcd_rsrw_set(lcd, 0, 0);
	lcd_data_set_byte(lcd, 0x04 | 0x02);
	lcd_delay_us(lcd, DELAY_CLRRET_US);



	// set default charmap function if not provided
	if (NULL == lcd->charmap_func)
		lcd->charmap_func = &sk_lcd_charmap_cp1251;

	sk_lcd_init_ua_char(lcd);

	lcd_return_home(lcd);

}


// don't map, return as-is for direct table use
uint8_t sk_lcd_charmap_none(const char c)
{
	return *((const uint8_t *)&c);
}


// CP1251 (aka Windows-1251) char map
uint8_t sk_lcd_charmap_cp1251(const char c)
{
	uint8_t ch = *((uint8_t *)&c);		// treat char as byte
	if (ch < 128)
		return ch;

	switch (c) {
		case 'А': return 'A';
		case 'Б': return 0xA0;
		case 'В': return 'B';
		case 'Г': return 0xA1;
		case 'Ґ': return G_UPPER_CASE;		// bad sym
		case 'Д': return 0xE0;
		case 'Е': return 'E';
		case 'Ё': return 0xA2;
		case 'Ж': return 0xA3;
		case 'З': return 0xA4;
		case 'И': return 0xA5;
		case 'І':  return 'I';
		case 'Ї':  return YI_UPPER_CASE;		// bad sym
		case 'Й': return 0xA6;
		case 'К': return 'K';
		case 'Л': return 0xA7;
		case 'М': return 'M';
		case 'Н': return 'H';
		case 'О': return 'O';
		case 'П': return 0xA8;
		case 'Р': return 'P';
		case 'С': return 'C';
		case 'Т': return 'T';
		case 'У': return 0xA9;
		case 'Ф': return 0xAA;
		case 'Х': return 'X';
		case 'Ц': return 0xE1;
		case 'Ч': return 0xAB;
		case 'Ш': return 0xAC;
		case 'Щ': return 0xE2;
		case 'Ъ': return 0xAD;
		case 'Ы': return 0xAE;
		case 'Ь': return SOFTSIGN;
		case 'Э': return 0xAF;
		case 'Є': return YE_UPPER_CASE;
		case 'Ю': return 0xB0;
		case 'Я': return 0xB1;
		case 'а': return 'a';
		case 'б': return 0xB2;
		case 'в': return 0xB3;
		case 'г': return 0xB4;
		case 'ґ': return G_LOWER_CASE;
		case 'д': return 0xE3;
		case 'е': return 'e';
		case 'є': return YE_LOWER_CASE;
		case 'ё': return 0xB5;
		case 'ж': return 0xB6;
		case 'з': return 0xB7;
		case 'и': return 0xB8;
		case 'і': return 'i';
		case 'ї': return YI_LOWER_CASE;		
		case 'й': return 0xB9;
		case 'к': return 0xBA;
		case 'л': return 0xBB;
		case 'м': return 0xBC;
		case 'н': return 0xBD;
		case 'о': return 'o';
		case 'п': return 0xBE;
		case 'р': return 'p';
		case 'с': return 'c';
		case 'т': return 0xBF;
		case 'у': return 'y';
		case 'ф': return 0xE4;
		case 'х': return 'x';
		case 'ц': return 0xE5;
		case 'ч': return 0xC0;
		case 'ш': return 0xC1;
		case 'щ': return 0xE6;
		case 'ъ': return 0xC2;
		case 'ы': return 0xC3;
		case 'ь': return 0xC4;
		case 'э': return 0xC5;
		case 'ю': return 0xC6;
		case 'я': return 0xC7;

		case '“': return 0xCA;
		case '”': return 0xCB;
		case '«': return 0xC8;
		case '»': return 0xC9;
		case '№': return 0xCC;
		case '°': return /*0xEF;*/ TEMP_SYM;
		case '·': return 0xDF;


		default: return 0xFF;	// black square for unknown symbols
	}
}
