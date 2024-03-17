
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <time.h>

#include "aroff.h"

/* widths are in .1 inch units */
/* default 10 cpi, ie 1" == 10 characters. */
static unsigned left_margin = 10;
static unsigned right_margin = 10;
static unsigned platen_width = 80;
static unsigned indent = 0;
static unsigned cpi = 10;
static unsigned attr = 0;


static char date_str[32];
static char time_str[32];


void update_width(void) {
	
	aroff_width[0] = platen_width - left_margin - right_margin;
	aroff_width[1] = platen_width - left_margin - right_margin - indent;
	aroff_indent[0] = left_margin;
	aroff_indent[1] = left_margin + indent;
}

void aw_init(void) {


	time_t now;
	struct tm *tm;
	unsigned hh;

	aroff_init();

	left_margin = 10;
	right_margin = 10;
	platen_width = 80;
	indent = 0;

	cpi = 10;
	attr = ATTR_PLAIN;
	// just = flag_f ? JUST_FULL : JUST_LEFT;
	just = JUST_LEFT;

	update_width();

	now = time(NULL);
	tm = localtime(&now);

	/* strftime %e and %l will use a leading ' ' for 1-digit days/hours */
	#if 0
	strftime(&date_str, sizeof(date_str), "%B %e, %Y", tm);
	strftime(&time_str, sizeof(time_str), "%l:%M %p", tm);
	#endif

	hh = tm->tm_hour;
	if (hh > 11) hh -= 12;
	if (hh == 0) hh = 12;

	snprintf(date_str, sizeof(date_str), "%s %u, %u",
		months[tm->tm_mon], tm->tm_mday, 1900 + tm->tm_year
	);

	snprintf(time_str, sizeof(time_str), "%u:%02u %cm",
		hh, tm->tm_min, tm->tm_hour < 12 ? 'a' : 'p'
	);

}

static void process_tabs(unsigned char *buffer, unsigned length) {
	unsigned i, j;
	for (i = 0, j = 0; i < length; ++i) {
		unsigned c = buffer[i];
		switch(c) {
		case '<':
		case '|': /* aw 2.0 */
			tab_stops[j] = i;
			tab_types[j] = TAB_LEFT;
			++j;
			break;
		case '>':
			tab_stops[j] = i;
			tab_types[j] = TAB_RIGHT;
			++j;
			break;
		case '^':
			tab_stops[j] = i;
			tab_types[j] = TAB_CENTER;
			++j;
			break;
		case '.':
			tab_stops[j] = i;
			tab_types[j] = TAB_DECIMAL;
			++j;
			break;
		}
		if (j == AROFF_MAX_TABS) break;
	}
	tab_count = j;
}

static void aw_text(FILE *f, int arg) {

	static unsigned char buffer[256];

	/* arg is "number of bytes following this word".  Seems to always be 0 */

	int x;
	int special;
	unsigned i,l,cr;
	special = fgetc(f);
	/* 0xff = this is a ruler [???]
	otherwise, bit 7 indicates tabs and special filler codes used.
	bits 0-6 = screen column for first char.

	(ignored since we're using margin width?)
	*/
	x = fgetc(f);
	if (x < 0) errx(EX_DATAERR, "Unexpected EOF");

	/*
	bit 7 = CR at end of line
	bit 0-6 = bytes following;
	*/
	cr = x & 0x80;
	l = x & 0x7f;

	if (fread(buffer, 1, l, f) != l) errx(EX_DATAERR, "Unexpected EOF");

	if (special == 0xff) {
		/* ruler */
		if (flag_c) {
			fwrite(buffer, 1, l, stdout);
			fputc('\n', stdout);
		}

		process_tabs(buffer, l);

		return;
	}

	if (pos + l >= AROFF_BUFFER_SIZE) aroff_flush_paragraph(0);

	for (i = 0; i < l; ++i) {
		unsigned c = buffer[i];
		if (c > 0x7f) {
			if (c >= 0xe0) {
				/* inverse lower case */
				c = 0x60 | (c & 0x1f);
			} else if (c >= 0xc0) {
				/* mouse text */
				c = '?';
			} else if (c >= 0xa0) {
				/* inverse number/symbols */
				c = 0x20 | (c & 0x1f);
			} else if (c >= 0x80) {
				/* inverse number/symbols */
				c = 0x40 | (c & 0x1f);
			}
			para[pos] = c;
			style[pos] = ATTR_REVERSE;	
			++pos;
			continue;
		}
		if (c >= 0x20) {
			para[pos] = c;
			style[pos] = attr;
			++pos;
		} else {
			switch(c) {
			case 0x01:
				attr |= ATTR_BOLD;
				break;
			case 0x02:
				attr &= ~ATTR_BOLD;
				break;
			case 0x03:
				attr |= ATTR_SUPERSCRIPT;
				break;
			case 0x04:
				attr &= ~ATTR_SUPERSCRIPT;
				break;
			case 0x05:
				attr |= ATTR_SUBSCRIPT;
				break;
			case 0x06:
				attr &= ~ATTR_SUBSCRIPT;
				break;
			case 0x07:
				attr |= ATTR_UNDERLINE;
				break;
			case 0x08:
				attr &= ~ATTR_UNDERLINE;
				break;

			case 0x09:
				/* print page number */
				if (flag_c) aroff_append_string("<Page Number>", attr);
				break;
			case 0x0a:
				/* enter keyboard */
				if (flag_c) aroff_append_string("<Enter Keyboard>", attr);
				break;

			case 0x0b:
				/* sticky space */
				if (flag_c)
					para[pos] = '_';
				else
					para[pos] = ' ' | 0x80;
				style[pos] = attr;
				++pos;
				break;
			case 0x0c:
				if (flag_c) aroff_append_string("<Mail Merge>", attr);
				/* mail merge */
				break;
			case 0x0d:
				/* reserved */
				break;
			case 0x0e:
				/* print date */
				aroff_append_string(flag_c ? "<Date>" : date_str, attr);
				break;
			case 0x0f:
				/* print time */
				aroff_append_string(flag_c ? "<Time>" : time_str, attr);
				break;
			case 0x10:
			case 0x11:
			case 0x12:
			case 0x13:
			case 0x14:
			case 0x15:
				/* special codes */
				break;
			case 0x16:
				/* TODO - flag for aroff tabbing vs aw tabbing? */
				/* tab */
				if (flag_c) {
					para[pos] = '^';
					style[pos] = attr;
					++pos;
					break;
				}
				#if 0
				if (flag_t) {
					para[pos] = '\t';
					style[pos] = attr;
					++pos;
					break;
				}
				#endif
				/* drop through */
			case 0x17:
				#if 0
				if (flag_t) break;
				#endif
				/* tab fill char */
				para[pos] = ' ';
				style[pos] = attr;
				++pos;
				break;
			}
		}
	}

	if (flag_c) {
		// print_line(1);
		fwrite(para, pos, 1, stdout);
		pos = 0;
		if (cr) fputc('$', stdout);
		fputc('\n', stdout);
		return;
	}

	/* auto-disabled at the end of line */
	attr &= ~(ATTR_SUPERSCRIPT|ATTR_SUPERSCRIPT);
	if (cr) {
		/* auto-disabled at the end of paragraph */
		attr &= ~(ATTR_BOLD|ATTR_UNDERLINE);
		aroff_flush_paragraph(1);
	}
}

#define SHOW_CODE(...) if (flag_c) printf("--------" __VA_ARGS__)
void aw_process(FILE *f) {
	
	int sfminver;

	sfminver = header[183];

	// documentation states 
	fseek(f, sfminver ? 302 : 300, SEEK_SET);
	#if 0
	if (sfminver) {
		/* skip first line record (2 bytes) */
		fgetc(f);
		fgetc(f);
	}
	#endif
	aw_init();


	if (flag_c) {
		/* print the tab stops */
		fwrite(header + 5, 79, 1, stdout);
		fputc('\n', stdout);
	}

	process_tabs(header + 5, 79);

	for(;;) {
		int arg = fgetc(f);
		int command = fgetc(f);

		if (command < 0) errx(EX_DATAERR, "Unexpected EOF");
		if (command == 0xff) return;

		switch(command) {
		case 0:
			aw_text(f, arg);
			break;

		case 0xd0:
			/* CR.  arg is horizontal screen position */
			if (flag_c) {
				fputs("$\n", stdout);
			} else {
				aroff_flush_paragraph(1);
			}
			break;
		case 0xd4: /* reserved */
		case 0xd5: /* page header end */
			SHOW_CODE("Page Header");
			break;
		case 0xd6: /* page footer end */
			SHOW_CODE("Page Footer");
			break;
		case 0xd7:
			/* right justification */
			SHOW_CODE("Right Justified\n");
			just = JUST_RIGHT;
			break;
		case 0xd8:
			/* platen width */
			SHOW_CODE("Platen Width: %u.%u inches\n", arg / 10, arg %10);
			if (arg < 10) arg = 10;
			if (arg > 132) arg = 132;
			platen_width = arg;
			update_width();
			break;
		case 0xd9:
			/* left margin */
			SHOW_CODE("Left Margin: %u.%u inches\n", arg / 10, arg %10);
			if (arg > 90) arg = 90;
			left_margin = arg;
			update_width();
			break;
		case 0xda:
			/* right margin */
			SHOW_CODE("Right Margin: %u.%u inches\n", arg / 10, arg %10);
			if (arg > 90) arg = 90;
			right_margin = arg;
			update_width();
			break;
		case 0xdb: /* chars per inch */
			SHOW_CODE("Chars per Inch: %u chars\n", arg);
			cpi = arg;
			break;
		case 0xdc: /* proportional-1 */
			SHOW_CODE("Proportional-1\n");
			break;
		case 0xdd: /* proportional-2 */
			SHOW_CODE("Proportional-1\n");
			break;
		case 0xde:
			/* indent arg chars */
			SHOW_CODE("Indent: %u chars\n", arg);
			indent = arg;
			update_width();
			break;
		case 0xdf:
			/* full justification */
			SHOW_CODE("Justified\n");
			just = JUST_FULL;
			break;
		case 0xe0:
			/* left justification */
			SHOW_CODE("Unjustified\n");
			// just = flag_f ? JUST_FULL : JUST_LEFT;
			just = JUST_LEFT;
			break;
		case 0xe1:
			/* center */
			SHOW_CODE("Centered\n");
			just = JUST_CENTER;
			break;
		case 0xe2: /* paper length */
			SHOW_CODE("Paper Length: %u.%u inches\n", arg / 10, arg %10);
			break;
		case 0xe3: /* top margin */
			SHOW_CODE("Top Margin: %u.%u inches\n", arg / 10, arg %10);
			break;
		case 0xe4: /* bottom margin */
			SHOW_CODE("Bottom Margin: %u.%u inches\n", arg / 10, arg %10);
			break;
		case 0xe5: /* lines per inch */
			SHOW_CODE("Lines Per Inch: %u\n", arg);
			break;
		case 0xe6: /* single space */
			SHOW_CODE("Single Space\n");
			break;
		case 0xe7: /* double space */
			SHOW_CODE("Double Space\n");
			break;
		case 0xe8: /* triple space */
			SHOW_CODE("Triple Space\n");
			break;
		case 0xe9:
			/* new page */
			SHOW_CODE("New Page\n");
			break;
		case 0xea: /* group begin */
			SHOW_CODE("Group Begin\n");
			break;
		case 0xeb: /* group end */
			SHOW_CODE("Group End\n");
			break;
		case 0xec: /* page header */
			SHOW_CODE("Page Header\n");
			break;
		case 0xed: /* page footer */
			SHOW_CODE("Page Footer\n");
			break;
		case 0xee: /* skip lines (arg = count) */
			SHOW_CODE("Skip Lines: %u\n", arg);
			aroff_flush_paragraph(1);
			for (unsigned i = 1; i < arg; ++i) fputc('\n', stdout);
			break;
		case 0xef: /* page number (arg = page #) */
			SHOW_CODE("Page Number: %u\n", arg);
			break;
		case 0xf0: /* pause each page */
			SHOW_CODE("Pause Each Page\n");
			break;
		case 0xf1: /* pause here */
			SHOW_CODE("Pause Here\n");
			break;
		case 0xf2: /* set marker (arg = marker) */
			SHOW_CODE("Set Marker: %u\n", arg);
			break;
		case 0xf3: /* page number (arg + 256 = page #) */
			SHOW_CODE("Page Number: %u\n", arg+256);
			break;
		case 0xf4: /* page break (arg = page #) */
			SHOW_CODE("Page Break: %u\n", arg);
			fputc('\f', stdout);
			break;
		case 0xf5: /* page break (arg + 256 = page #) */
			SHOW_CODE("Page Break: %u\n", arg+256);
			fputc('\f', stdout);
			break;
		case 0xf6: /* page break arg (middle of paragraph)*/
			SHOW_CODE("Page Break: %u\n", 256);
			fputc('\f', stdout);
			break;
		case 0xf7: /* page break arg+256 (middle of paragraph)*/
			SHOW_CODE("Page Break: %u\n", arg+256);
			fputc('\f', stdout);
			break;
		}
	}
}

int is_aw(void) {
	if (header_size <= 300) return 0;
	if (header[4] != 0x4f) return 0; //
	if (header[183] != 0 && header[183] != 0x30) return 0; // sf min ver

	// check tab stops
	for (unsigned i = 5; i < 84; ++i) {
		unsigned c = header[i];
		if (c != '=' && c != '|' && c != '<' && c != '>' && c != '.' && c != '^')
			return 0;
	}
	return 1;
}

