

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <time.h>

#include "aroff.h"


#define TAB_LEFT 0
#define TAB_RIGHT 1
#define TAB_DECIMAL 0xffff

static unsigned attr = 0;

static char date_str[32];
static char time_str[32];


#ifdef __ORCAC__

struct TextBlock {
	unsigned short blockSize;
	unsigned short blockUsed;
	/* followed by 0 or more sets of */
	/* paragraph header */
	/* paragraph text */
};

struct ParagraphHeader {
	unsigned short firstFont;
	unsigned char firstStyle;
	unsigned char firstSize;
	unsigned char firstColor;
	unsigned short reserved;
};


struct Tab {
	unsigned short tabLocation;
	unsigned short tabType;
};

struct Ruler {
	unsigned short numParagraphs;
	unsigned short statusBits;
	unsigned short leftMargin;
	unsigned short indentMargin;
	unsigned short rightMargin;
	unsigned short numTabs;
	struct tab tabRecs[10];
};

struct SaveArray {
	unsigned short textBlock;
	unsigned short offset;
	unsigned short attributes;
	unsigned short rulerNum;
	unsigned short pixelHeight;
	unsigned short numLines;
};

#endif

enum {
	o_ph_first_font = 0,
	o_ph_first_style = 2,
	o_ph_first_size = 3,
	o_ph_first_color = 4,
	o_ph_size = 7,

	o_sa_text_block = 0,
	o_sa_offset = 2,
	o_sa_attributes = 4,
	o_sa_ruler_num = 6,
	o_sa_pixel_height = 8,
	o_sa_num_lines = 10,
	o_sa_size = 12,

	o_ruler_num_paragraphs = 0,
	o_ruler_status_bits = 2,
	o_ruler_left_margin = 4,
	o_ruler_indent_margin = 6,
	o_ruler_right_margin = 8,
	o_ruler_num_tabs = 10,
	o_ruler_tabs = 12,
	o_ruler_size = 52,


	o_tab_location = 0,
	o_tab_type = 2,
	o_tab_size = 4,
};

#ifdef __ORCAC__
#define READ16(ptr, offset) *(unsigned short *)((unsigned char *)ptr + offset)
#else
#define READ16(ptr, offset) (ptr[offset] | (ptr[offset+1] << 8))
#endif

static void update_style(unsigned style) {
	attr = 0;
	/* outline/shadow ignored */
	if (style & 0x80) attr |= ATTR_SUBSCRIPT;
	if (style & 0x40) attr |= ATTR_SUPERSCRIPT;
	if (style & 0x04) attr |= ATTR_UNDERLINE;
	if (style & 0x02) attr |= ATTR_ITALIC;
	if (style & 0x01) attr |= ATTR_BOLD;
}

void awgs_init(void) {

	time_t now;
	struct tm *tm;
	unsigned hh;

	aroff_init();
	attr = 0;

	now = time(NULL);
	tm = localtime(&now);

	hh = tm->tm_hour;
	if (hh > 11) hh -= 12;
	if (hh == 0) hh = 12;

	snprintf(date_str, sizeof(date_str), "%s %u, %u",
		months[tm->tm_mon], tm->tm_mday, 1900 + tm->tm_year
	);

	snprintf(time_str, sizeof(time_str), "%u:%02u %cM",
		hh, tm->tm_min, tm->tm_hour < 12 ? 'A' : 'P'
	);

}



void process_para(unsigned char *ruler, unsigned char *pp) {

	unsigned x;
	unsigned left_margin, indent_margin, right_margin;
	unsigned nt;

	unsigned tabs[10];
	unsigned tab_type[10];

	/* status bits */
	x = ruler[o_ruler_status_bits];

	if (x & 0x80) just = JUST_FULL;
	if (x & 0x40) just = JUST_RIGHT;
	if (x & 0x20) just = JUST_CENTER;
	if (x & 0x10) just = JUST_LEFT;

	/* margins are based on pixels but 0 actually means 1/2", ie 40 pixels */

	x = READ16(ruler, o_ruler_right_margin) + 40;
	right_margin = x >> 3;

	x = READ16(ruler, o_ruler_indent_margin) + 40;
	indent_margin = x >> 3;
	aroff_indent[0] = indent_margin;
	aroff_width[0] = right_margin - indent_margin;

	x = READ16(ruler, o_ruler_left_margin) + 40;
	left_margin = x >> 3;
	aroff_indent[1] = left_margin;
	aroff_width[1] = right_margin - left_margin;

	nt = READ16(ruler, o_ruler_num_tabs);
	if (nt > 10) nt = 10;

	for (unsigned i = 0, offset = o_ruler_tabs; i < nt; ++i, offset += o_tab_size) {

		x = READ16(ruler, offset + o_tab_location) + 40;
		tabs[i] = x >> 3;
		tab_type[i] = READ16(ruler, offset + o_tab_type);
	}

	/* TODO --
		stty.1 awgs man page uses full justification and tabs.
		disable full/center justification if tabs are used?

		stty.1 awgs man pages uses a left tab on the right edge
		of the document.

	*/

	update_style(pp[o_ph_first_style]);
	pp += o_ph_size;
	for (unsigned i = 0;;) {
		unsigned int c = pp[i++];
		if (c == 0x0d) break;

		if (c < 0x20) {
			switch(c) {
			case 0x01:
				/* font change */
				i += 2;
				break;
			case 0x02:
				/* style change */
				update_style(pp[i++]);
				break;
			case 0x03:
				/* size change */
				i += 1;
				break;
			case 0x04:
				/* color change */
				i += 1;
				break;
			case 0x05:
				/* page token */
				break;
			case 0x06:
				/* date token */
				aroff_append_string(date_str, attr);
				break;
			case 0x07:
				/* time token */
				aroff_append_string(time_str, attr);
				break;
			case 0x09:
				/* tab char.... */
				/* flush any pending data, then calculate the next tab */
				aroff_flush_paragraph(0);
				para[pos] = ' ';
				style[pos] = attr;
				++pos;

				/* based on testing, center and right justified just insert a space */
				if (just == JUST_CENTER || just == JUST_RIGHT)
					break;

				if (just != JUST_LEFT) just = JUST_LEFT;
				for (unsigned i = 0; i < nt; ++i) {
					/* tab location is pixels from the left */

					x = tabs[i] - aroff_indent[aroff_line];
					if (x >= pos) {
						while(pos < x) {
							para[pos] = ' ';
							style[pos] = attr;
							++pos;
						}
						break;
					}
				}
				break;
			}
		} else {
			if (c > 0x7f) {
				/* macroman nbsp */
				if (c == 0xca) c = ' '|0x80;
				else c = '?';
			}
			para[pos] = c;
			style[pos] = attr;
			++pos;
			if (pos == 512) aroff_flush_paragraph(0);
		}
	}
	aroff_flush_paragraph(1);
	attr = 0;
}


void awgs_process(FILE *f) {

	unsigned i;
	unsigned offset;
	unsigned numArrays;
	unsigned numRulers = 1;
	unsigned numBlocks = 1;

	unsigned char *arrays;
	unsigned char *rulers;
	unsigned char **textBlocks;

	awgs_init();

	/* skip the header */
	fseek(f, 668, SEEK_SET);

	numArrays = fgetc(f) | (fgetc(f) << 8);

	arrays = malloc(numArrays * o_sa_size);
	if (!arrays) err(EX_OSERR, "malloc doc save array (%u entries)", numArrays);
	if (fread(arrays, o_sa_size, numArrays, f) != numArrays)
		errx(EX_DATAERR, "fread");

	/* count up the rulers and text blocks */
	for (unsigned i = 0, offset = 0; i < numArrays; ++i, offset += o_sa_size) {

		unsigned x;
		x = READ16(arrays, offset + o_sa_text_block) + 1;
		if (x > numBlocks) numBlocks = x;

		/* ruler only valid if attr == 0 (normal text) */
		x = READ16(arrays, offset + o_sa_attributes);
		if (x == 0) {
			x = READ16(arrays, offset + o_sa_ruler_num) + 1;
			if (x > numRulers) numRulers = x;
		}
	}

	rulers = malloc(numRulers * 52);
	if (!rulers) err(EX_OSERR, "malloc rulers (%u entries)", numRulers);
	if (fread(rulers, o_ruler_size, numRulers, f) != numRulers)
		errx(EX_DATAERR, "fread");

	/* need to read all the text blocks as well */
	textBlocks = calloc(numBlocks, sizeof(unsigned char *));
	if (!textBlocks) err(EX_OSERR, "malloc text blocks array (%u entries)", numBlocks);

	for (i = 0; i < numBlocks; ++i) {
		/* redundant 32-bit size field */
		long size;
		unsigned char *cp;

		size = fgetc(f) | (fgetc(f) << 8) | (fgetc(f) << 16) | fgetc(f) << 24;

		cp = malloc(size);
		if (!cp) err(EX_OSERR, "malloc text blocks %u (%lu)", i, size);

		if (fread(cp, size, 1, f) != 1)
			errx(EX_DATAERR, "fread");
		textBlocks[i] = cp;
	}


	for (i = 0, offset = 0; i < numArrays; ++i, offset += o_sa_size) {
		unsigned o = READ16(arrays, offset + o_sa_offset);
		unsigned t = READ16(arrays, offset + o_sa_text_block);
		unsigned r = READ16(arrays, offset + o_sa_ruler_num);

		/* check for a page break */
		if (READ16(arrays, offset + o_sa_attributes)) {
			fputc('\f', stdout);
			continue;
		}

		// fprintf(stderr, "%d: %d:%d %d\n", i, t, o, r);
		process_para(rulers + r * o_ruler_size, textBlocks[t] + o);
	}

	for(i = 0; i < numBlocks; ++i) {
		free(textBlocks[i]);
	}
	free(textBlocks);
	free(arrays);
	free(rulers);
}
