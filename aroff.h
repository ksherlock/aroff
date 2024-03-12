#ifndef AROFF_H
#define AROFF_H


enum {
	JUST_LEFT,
	JUST_RIGHT,
	JUST_FULL,
	JUST_CENTER
};

enum {
	TAB_LEFT,
	TAB_RIGHT,
	TAB_CENTER,
	TAB_DECIMAL
};

enum {
	ATTR_PLAIN,
	ATTR_BOLD = 1,
	ATTR_UNDERLINE = 2,
	ATTR_SUPERSCRIPT = 4,
	ATTR_SUBSCRIPT = 8,
	ATTR_ITALIC = 16,
	ATTR_REVERSE = 32,
};

extern unsigned aroff_indent[2];
extern unsigned aroff_width[2];

extern unsigned aroff_line;

extern unsigned char para[512];
extern unsigned char style[512];
extern unsigned pos;
extern unsigned just;

extern char *months[12];


#define AROFF_BUFFER_SIZE 511
#define AROFF_MAX_TABS 16
extern unsigned tab_count;
extern unsigned tab_stops[AROFF_MAX_TABS];
extern unsigned tab_types[AROFF_MAX_TABS];


extern int flag_c;
extern int flag_f;

void aroff_init(void);
void aroff_flush_paragraph(int cr);

void aroff_append_string(const char *str, unsigned attr);

void aroff_render(unsigned char *text, unsigned char *style, unsigned length);

#endif
