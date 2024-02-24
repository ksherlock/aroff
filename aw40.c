
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

enum {
	JUST_LEFT,
	JUST_RIGHT,
	JUST_FULL,
	JUST_CENTER
};
enum {
	ATTR_PLAIN,
	ATTR_BOLD,
	ATTR_UNDERLINE,
	ATTR_SUPERSCRIPT,
	ATTR_SUBSCRIPT,
	ATTR_ITALIC,
};

/* widths are in .1 inch units */
/* default 10 cpi, ie 1" == 10 characters. */
unsigned left_margin = 10;
unsigned right_margin = 10;
unsigned platen_width = 80;
unsigned cpi = 10;
unsigned attr = 0;
unsigned just = 0;
unsigned indent = 0;
unsigned width = 60;

unsigned char header[300];
unsigned char para[512];
unsigned char style[512];
unsigned pos = 0;
unsigned flip_flop = 0;
unsigned para_indent = 0;

unsigned flag_c = 0;
unsigned flag_f = 0;


void update_width(void) {
	width = platen_width - left_margin - right_margin;
	if ((int)width <= 0) width = platen_width - left_margin;
	if ((int)width <= 10) width = 10;
}

void init(void) {
	left_margin = 10;
	right_margin = 10;
	platen_width = 80;
	cpi = 10;
	attr = ATTR_PLAIN;
	just = flag_f ? JUST_FULL : JUST_LEFT;
	indent = 0;

	pos = 0;
	flip_flop = 0;
	para_indent = 0;

	width = platen_width - left_margin - right_margin;
}


void full_justify(unsigned start, unsigned end) {

	static char ftext[132];
	static char fstyle[132];

	/* TODO ... */
	unsigned i, j;
	unsigned padding, sp;

	unsigned sz = end - start;
	unsigned w = width - para_indent;
	if ((int)w < 10) w = 10;

	if (sz >= w) {
		fwrite(para + start, sz, 1, stdout);
		return;
	}

	/* TODO:
		if indent && para_indent == 0, treat spaces within the indent
		as sticky, so lists print correctly?
	*/
	if (flag_f && indent && !para_indent) {
		for (i = 0; i < indent; ++i)
			if (para[start + i] == ' ')
				para[start + i] = ' '  | 0x80;
	}

	for (sp = 0, i = start; i < end; ++i) {
		if (para[i] == ' ') ++sp;
	}
	if (sp == 0) {
		fwrite(para + start, sz, 1, stdout);
		return;
	}

	padding = w - sz;

	for (i = start, j = 0; i < end; ++i) {
		unsigned c = para[i];
		unsigned s = style[i];

		ftext[j] = c & 0x7f;
		fstyle[j] = s;
		++j;

		if (c == ' ' && padding) {
			unsigned fudge;
			if (flip_flop)
				fudge = padding / sp;
			else
				fudge = (padding - 1) / sp + 1;
			padding -= fudge;
			sp -= 1;
			for (unsigned k = 0; k < fudge; ++k) {
				ftext[j] = ' ';
				fstyle[j] = s;
				++j;
			}
		}
	}

	fwrite(ftext, w, 1, stdout);
	flip_flop = !flip_flop;
}
void print_helper(unsigned start, unsigned end, int last) {

	unsigned ws;
	unsigned i;
	unsigned w = end - start;
	unsigned j = just;

	if (j == JUST_FULL && last) j = JUST_LEFT;

	ws = left_margin;
	ws += para_indent;


	switch(j) {
	case JUST_FULL:
		break;
	case JUST_LEFT:
		break;
	case JUST_CENTER:
		ws += (width - w) >> 1;
		break;
	case JUST_RIGHT:
		ws += (width - w);
		break;
	}

	for(i = 0; i < ws; ++i) fputc(' ', stdout);

	if (j == JUST_FULL) {
		full_justify(start, end);
	} else {
		/* TODO -- handle attribute printing */
		fwrite(para + start, w, 1, stdout);
	}

	fputc('\n', stdout);
}

void flush_paragraph(int cr) {

	/* split and print any complete lines in the paragraph. 
	   if cr, print the final line as well.
	*/

	int start = 0;
	int end = 0;
	unsigned w = width - para_indent;
	if ((int)w < 10) w = 10;

	if (pos == 0 && cr) {
		fputc('\n', stdout);
		return;
	}

	if (pos < w && !cr) return;


	unsigned tlen = pos;
	while (tlen >= w) {

		int ws;
		ws = start + w;
		while (ws >= start) {
			if (para[ws] == ' ') break;
			--ws;
		}
		if (ws < start) {
			// no whitespace so just break
			end = ws = start + w;
		} else {
			end = ws + 1;
			while (ws > start && para[ws-1] == ' ') --ws;
		}

		print_helper(start, ws, 0);
		start = end;

		/* trim leading whitespace on the next line... */
		while (start < pos && para[start] == ' ') ++start;

		tlen = pos - start;
		para_indent = indent;
		w = width - indent;
		if ((int)w < 10) w = 10;
	}

	/* move line / style, update pos. */
	if (tlen != pos) {
		memmove(para, para + start, tlen);
		memmove(style, style + start, tlen);
		pos = tlen;
	}

	if (cr) {
		/* trim trailing whitespace (or sticky space)... */
		while (pos && (para[pos-1]& 0x7f) == ' ') --pos;

		if (pos)
			print_helper(0, pos, 1);
		else fputc('\n', stdout);
		pos = 0;
		para_indent = 0;
	}
}


void text(FILE *f, int arg) {

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
		return;
	}

	if (pos + l >= sizeof(para)) flush_paragraph(0);

	for (i = 0; i < l; ++i) {
		unsigned c = buffer[i];
		if (c > 0x7f) c = '?'; /* mouse text? */
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
				break;
			case 0x0a:
				/* enter keyboard */
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
				/* mail merge */
				break;
			case 0x0d:
				/* reserved */
				break;
			case 0x0e:
				/* print date */
				break;
			case 0x0f:
				/* print time */
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
				/* tab */
				if (flag_c) {
					para[pos] = '^';
					style[pos] = attr;
					++pos;
					break;
				}
			case 0x17:
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
		flush_paragraph(1);
	}
}

#define flag_c(...) if (flag_c) printf("--------" __VA_ARGS__)
void process(FILE *f) {
	

	int ok;
	int sfminver;

	ok = fread(header, 300, 1, f);
	if (ok != 1) errx(EX_DATAERR, "Unexpected EOF");

	sfminver = header[183];

	if (sfminver) {
		/* skip first line record (2 bytes) */
		fgetc(f);
		fgetc(f);
	}

	init();


	if (flag_c && sfminver) {
		/* print the tab stops */
		fwrite(header + 5, 80, 1, stdout);
		fputc('\n', stdout);
	}

	for(;;) {
		int arg = fgetc(f);
		int command = fgetc(f);

		if (command < 0) errx(EX_DATAERR, "Unexpected EOF");
		if (command == 0xff) return;

		switch(command) {
		case 0:
			text(f, arg);
			break;

		case 0xd0:
			/* CR.  arg is horizontal screen position */
			if (flag_c) {
				fputs("$\n", stdout);
			} else {
				flush_paragraph(1);
			}
			break;
		case 0xd4: /* reserved */
		case 0xd5: /* page header end */
		case 0xd6: /* page footer end */
			break;
		case 0xd7:
			/* right justification */
			just = JUST_RIGHT;
			flag_c("Right Justified\n");
			break;
		case 0xd8:
			/* platen width */
			flag_c("Platen Width: %u.%u inches\n", arg / 10, arg %10);
			if (arg < 10) arg = 10;
			if (arg > 132) arg = 132;
			platen_width = arg;
			update_width();
			break;
		case 0xd9:
			/* left margin */
			flag_c("Left Margin: %u.%u inches\n", arg / 10, arg %10);
			if (arg < 1) arg = 1;
			if (arg > 90) arg = 90;
			left_margin = arg;
			update_width();
			break;
		case 0xda:
			/* right margin */
			flag_c("Right Margin: %u.%u inches\n", arg / 10, arg %10);
			if (arg < 1) arg = 1;
			if (arg > 90) arg = 90;
			right_margin = arg;
			update_width();
			break;
		case 0xdb: /* chars per inch */
			cpi = arg;
			flag_c("Chars per Inch: %u chars\n", arg);
			break;
		case 0xdc: /* proportional-1 */
		case 0xdd: /* proportional-2 */
			break;
		case 0xde:
			/* indent arg chars */
			indent = arg;
			flag_c("Indent: %u chars\n", arg);
			break;
		case 0xdf:
			/* full justification */
			just = JUST_FULL;
			flag_c("Justified\n");
			break;
		case 0xe0:
			/* left justification */
			just = flag_f ? JUST_FULL : JUST_LEFT;
			flag_c("Unjustified\n");
			break;
		case 0xe1:
			/* center */
			just = JUST_CENTER;
			flag_c("Centered\n");
			break;
		case 0xe2: /* paper length */
		case 0xe3: /* top margin */
		case 0xe4: /* bottom margin */
		case 0xe5: /* lines per inch */
		case 0xe6: /* single space */
		case 0xe7: /* double space */
		case 0xe8: /* triple space */
			break;
		case 0xe9:
			/* new page */
			break;
		case 0xea: /* group begin */
		case 0xeb: /* group end */
		case 0xec: /* page header */
		case 0xed: /* page footer */
			break;
		case 0xee: /* skip lines (arg = count) */
			flush_paragraph(1);
			for (unsigned i = 1; i < arg; ++i) fputc('\n', stdout);
			break;
		case 0xef: /* page number (arg = page #) */
		case 0xf0: /* pause each page */
		case 0xf1: /* pause here */
		case 0xf2: /* set marker (arg = marker) */
		case 0xf3: /* page number (arg + 256 = page #) */
		case 0xf4: /* page break (arg = page #) */
		case 0xf5: /* page break (arg + 256 = page #) */
		case 0xf6: /* page break arg */
		case 0xf7: /* page break arg+256 */
			break;
		}
	}
}

void usage(int ec) {

	fputs(
		"Usage:\n"
		"aw [-ch] file ...\n"
		" -c   show codes\n"
		" -f   justify the unjustifiable\n"
		" -h   help\n",
		stdout);

	exit(ec);
}

int main(int argc, char **argv) {

	int ch;
	unsigned i;

	while ( (ch = getopt(argc, argv, "cfh")) != -1) {
		switch(ch) {
		case 'c':
			flag_c = 1;
			break;
		case 'f':
			flag_f = 1;
			break;
		case 'h':
			usage(0);
			break;
		default:
			usage(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) usage(EX_NOINPUT);

	for (i = 0; i < argc; ++i) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) err(EX_NOINPUT, "fopen %s", argv[i]);
		process(f);
		fclose(f);
	}
	exit(0);
}
