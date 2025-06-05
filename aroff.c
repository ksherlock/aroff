

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <termcap.h>
#include <unistd.h>

#include "aroff.h"


enum {
	FMT_TERMCAP,
	FMT_ASCII,
	FMT_PLAIN,
};

#define MIN_WIDTH 10

char *months[12] = {
	"January",
	"February",
	"March",
	"April",
	"May",
	"June",
	"July",
	"August",
	"September",
	"October",
	"November",
	"December"
};

#undef MIN
#define MIN(a, b) ( ( (a) < (b) ) ? (a) : (b) )

unsigned char header[512];
unsigned header_size;

unsigned aroff_indent[2] = { 1, 1 };
unsigned aroff_width[2] = { 78, 78 };

unsigned char para[512];
unsigned char style[512];
unsigned pos;
unsigned just;

unsigned aroff_line = 0;
static unsigned flip_flop = 0;

int flag_c;
int flag_T;
const char *flag_t;

static char *tc_so = NULL; // enter standout mode
static char *tc_se = NULL; // exit standout mode
static char *tc_us = NULL; // enter underline node
static char *tc_ue = NULL; // end underline mode
static char *tc_md = NULL; // enter bold mode
static char *tc_mr = NULL; // enter reverse mode
static char *tc_me = NULL; // exit all attributes 
static char *tc_ZH = NULL; // enter italic mode
static char *tc_ZN = NULL; // enter subscript mode
static char *tc_ZO = NULL; // enter superscript mode
static char *tc_ZR = NULL; // exit italic mode
static char *tc_ZV = NULL; // exit subscript mode
static char *tc_ZW = NULL; // exit superscript mode


static struct {
	char *name;
	char **ptr;
} table[] = {
	{ "md", &tc_md },
	{ "me", &tc_me },
	{ "mr", &tc_mr },
	{ "se", &tc_se },
	{ "so", &tc_so },
	{ "ue", &tc_ue },
	{ "us", &tc_us },
	{ "ZH", &tc_ZH },
	{ "ZN", &tc_ZN },
	{ "ZO", &tc_ZO },
	{ "ZR", &tc_ZR },
	{ "ZV", &tc_ZV },
	{ "ZW", &tc_ZW },
};



static char tcap_buffer[1024];
static unsigned tcap_buffer_offset = 0;

static int tputs_helper(int c) {
	if (c) tcap_buffer[tcap_buffer_offset++] = c;
	return 1;
}

void tc_init(void) {
	static char buffer[1024];
	static char buffer2[1024];
	const char *term;
	char *cp;
	char *pcap;
	int ok;

	term = flag_t ? flag_t : getenv("TERM");
	if (!term || !*term) {
		warnx("TERM undefined."); return;
	}

	ok = tgetent(buffer, term);
	if (ok < 0) {
		warnx("No termcap for %s", term);
		return;
	}

	tcap_buffer_offset = 0;

	pcap = buffer2;

	for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
		cp = tgetstr(table[i].name, &pcap);
		if (cp) {
			*(table[i].ptr) = tcap_buffer + tcap_buffer_offset;
			tputs(cp, 0, tputs_helper);
			tcap_buffer[tcap_buffer_offset++] = 0;
		}
	}
}


void aroff_init(void) {
	pos = 0;
	aroff_indent[0] = 1;
	aroff_indent[1] = 1;
	aroff_width[0] = 78;
	aroff_width[1] = 78;

	aroff_line = 0;
	flip_flop = 0;
	just = JUST_LEFT;

	tab_count = 0;
}

#define _(a,b) if ((attr & a) && b) fputs(b, stdout)

/* on some termcaps, eg, vt100, underline end, bold end, etc disable all attr */
void tc_disable(unsigned attr) {
	if (tc_me) {
		/* me disables *all* appearance modes */
		fputs(tc_me, stdout);
		return;
	}
	_(ATTR_BOLD, tc_se);
	_(ATTR_UNDERLINE, tc_ue);
	_(ATTR_ITALIC, tc_ZR);
	_(ATTR_SUBSCRIPT, tc_ZV);
	_(ATTR_SUPERSCRIPT, tc_ZW);

}

void tc_enable(unsigned attr) {
	if (attr & ATTR_BOLD) {
		if (tc_md) fputs(tc_md, stdout);
		else if (tc_so) fputs(tc_so, stdout);
	}
	_(ATTR_UNDERLINE, tc_us);
	_(ATTR_ITALIC, tc_ZH);
	_(ATTR_SUBSCRIPT, tc_ZN);
	_(ATTR_SUPERSCRIPT, tc_ZO);
	_(ATTR_REVERSE, tc_mr);
}

#undef _

void aroff_render(unsigned char *text, unsigned char *style, unsigned length) {

	unsigned attr = 0;
	for (unsigned i = 0; i < length; ++i) {
		attr |= style[i];
		text[i] &= 0x7f; /* sticky space */
	}
	if (!attr) {
		fwrite(text, length, 1, stdout);
		return;
	}
	attr = 0;
	for (unsigned i = 0; i < length; ++i) {

		char c = text[i];
		unsigned a = style[i];
		if (c == ' ') a = 0;
		if (c == '\t') {
			c = ' ';
			a = 0;
		}
		if (attr != a && flag_T == FMT_TERMCAP) {
			if (attr) tc_disable(attr);
			if (a) tc_enable(a);
		}
		attr = a;
		if (flag_T == FMT_ASCII) {
			if (attr & ATTR_UNDERLINE) {
				fputs("_\x08", stdout);
			}
			if (attr & ATTR_BOLD) {
				fputc(c, stdout);
				fputc(8, stdout);
			}
		}
		fputc(c, stdout);

	}
	if (attr && flag_T == FMT_TERMCAP) tc_disable(attr);
}


static unsigned char ftext[132];
static unsigned char fstyle[132];


static unsigned full_justify_chunk(unsigned start, unsigned end, unsigned width) {

	unsigned i, j;
	unsigned padding, sp;

	unsigned sz = end - start;


	if (sz >= width) {
		aroff_render(para + start, style + start, sz);
		return sz;
	}

	for (sp = 0, i = start; i < end; ++i) {
		if (para[i] == ' ') ++sp;
	}

	if (sp == 0) {
		aroff_render(para + start, style + start, sz);
		return sz;
	}

	padding = width - sz;

	for (i = start, j = 0; i < end; ++i) {
		unsigned c = para[i];
		unsigned s = style[i];

		ftext[j] = c;
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

	aroff_render(ftext, fstyle, width);
	flip_flop = !flip_flop;
	return width;
}

static void full_justify(unsigned start, unsigned end) {



	unsigned i, j;
	unsigned padding, sp;

	unsigned sz = end - start;
	unsigned w = aroff_width[aroff_line];
	if ((int)w < MIN_WIDTH) w = MIN_WIDTH;

	if (sz >= w) {
		aroff_render(para + start, style + start, sz);
		return;
	}

	/* TODO:
		if indent && para_indent == 0, treat spaces within the indent
		as sticky, so lists print correctly?
	*/
	#if 0
	if (flag_f && indent && !para_indent) {
		for (i = 0; i < indent; ++i)
			if (para[start + i] == ' ')
				para[start + i] = ' '  | 0x80;
	}
	#endif

	for (sp = 0, i = start; i < end; ++i) {
		if (para[i] == ' ') ++sp;
	}
	if (sp == 0) {
		aroff_render(para + start, style + start, sz);
		return;
	}

	padding = w - sz;

	for (i = start, j = 0; i < end; ++i) {
		unsigned c = para[i];
		unsigned s = style[i];

		ftext[j] = c;
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

	aroff_render(ftext, fstyle, w);
	flip_flop = !flip_flop;
}

static void print_helper(unsigned start, unsigned end, int last) {

	unsigned i;
	unsigned sz = end - start;
	unsigned ws = aroff_indent[aroff_line];
	unsigned w = aroff_width[aroff_line];
	if ((int)w < MIN_WIDTH) w = MIN_WIDTH;


	switch(just) {
	case JUST_FULL:
		break;
	case JUST_LEFT:
		break;
	case JUST_CENTER:
		ws += (w - sz) >> 1;
		break;
	case JUST_RIGHT:
		ws += (w - sz);
		break;
	}

	for (i = 0; i < ws; ++i) fputc(' ', stdout);

	if (just == JUST_FULL && !last && sz < w) {
		full_justify(start, end);
	} else {
		aroff_render(para + start, style + start, sz);
	}

	fputc('\n', stdout);
	// aroff_line = 1;
}

unsigned tab_count = 0;
unsigned tab_stops[AROFF_MAX_TABS];
unsigned tab_types[AROFF_MAX_TABS];

static int next_tab(unsigned x, unsigned *style) {
	for (unsigned i = 0; i < tab_count; ++i) {
		if (tab_stops[i] > x) {
			if (style) *style = tab_types[i];
			return tab_stops[i];
		}
	}
	return -1;
}

/* TODO -- verify if this handles - correctly */
static int find_break(unsigned start, unsigned end, unsigned *termchar) {

	int rv = -1;
	unsigned c = 0;
	for (unsigned i = start; i < end; ++i) {
		c = para[i];
		if (c == ' ') rv = i;
		else if (c == '-') rv = i + 1;
		else if (c == '\t') {
			*termchar = c;
			return i;
		}
	}

	/* special check for end space break */
	if (para[end] == ' ' && para[end-1] != ' ') {
		*termchar = 0;
		return end;
	}

	if (rv >= 0) *termchar = para[rv];
	return rv;
}

/* returns *offset* to dot. */
static int dot_offset(unsigned start, unsigned end) {
	for (unsigned i = start; i < end; ++i) {
		unsigned c = para[i];
		if (c == '.') return i - start;
		if (c == '\t') break;
	}
	return -1;
}


/* returns new end */
static unsigned one_line(unsigned start, unsigned end, int last) {

	unsigned w = aroff_width[aroff_line];
	if ((int)w < MIN_WIDTH) w = MIN_WIDTH;
	int bk;
	unsigned c;


	bk = find_break(start, end, &c);
	if (bk < 0) {
		/* no break. */
		print_helper(start, end, last);
		return end;
	}

	/* strip trailing whitespace */
	unsigned local_end;
	local_end = bk;
	if (c == ' ') {
		while (local_end > start && para[local_end - 1] == ' ') --local_end;
		if (local_end == start) {
			/* whitespace line? */
			return bk;
		}
	}

	if (c != '\t') {
		/* simple case - no tabs! */
		print_helper(start, local_end, last);
		return bk;
	}

	/* complex case - tabs */
	/* n.b. - indent is handled as if it were a tab */
	unsigned tab_stop = aroff_indent[aroff_line];
	unsigned tab_style = TAB_LEFT;

	unsigned x = 0;
	unsigned max_x = w + tab_stop;

	for(;;) {

		unsigned l = local_end - start;

		unsigned ws = tab_stop - x;

		switch(tab_style) {
		case TAB_LEFT:
		case TAB_DECIMAL:
			/* TAB_DECIMAL handled below */
			break;
		case TAB_RIGHT:
			ws -= l;
			break;
		case TAB_CENTER:
			ws -= (l >> 1);
			break;
		}

		if ((int)ws > 0) {
			for (unsigned i = 0; i < ws; ++i)
				fputc(' ', stdout);
			x += ws;
		}


		/*
			if this is the final component and fully justified,
			we need to fully justify it ...
		*/
		if (just == JUST_FULL && tab_style == TAB_LEFT) {

			int rm = (c != '\t' || next_tab(x + l, NULL) < 0);
			if (last && bk == end) rm = 0;

			if (rm) {
				full_justify_chunk(start, bk, w - tab_stop);
				fputc('\n', stdout);
				return bk;
			}
		}

		aroff_render(para + start, style + start, l);
		x += l;

		if (c != '\t') {
			fputc('\n', stdout);
			return bk;
		}

		/* reminder: para[bk] is a tab */
		tab_stop = next_tab(x, &tab_style);
		if ((int)tab_stop < 0 || tab_stop > max_x) {
			fputc('\n', stdout);
			return bk;
		}

		/* skip over the tab */
		start = bk + 1;
		if (start == end) {
			fputc('\n', stdout);
			return end;
		}

		unsigned max_width = 0;
		switch(tab_style) {
		case TAB_LEFT:
			max_width = max_x - tab_stop;
			break;
		case TAB_RIGHT:
			max_width = max_x - x;
			break;
		case TAB_CENTER: {
				unsigned a = max_x - tab_stop;
				unsigned b = tab_stop - x;
				max_width = MIN(a, b);
				max_width <<= 1;
				break;
			}
		case TAB_DECIMAL: {
				unsigned a = start + (tab_stop - x);
				unsigned b = end;
				int d = dot_offset(start, MIN(a, b));
				if (d < 0) {
					tab_style = TAB_RIGHT;
					max_width = max_x - x;
				} else {
					tab_style = TAB_LEFT;
					tab_stop -= d;
					max_width = max_x - tab_stop;
				}
				break;
			}
		}

		if ((int)max_width <= 0) {
			fputc('\n', stdout);
			return start;
		}

		local_end = start + max_width;
		if (local_end > end) local_end = end;
		bk = find_break(start, local_end, &c);

		/* if no break was found, end this line and push
		it to the next. 
		*/

		if (bk < 0) {
			fputc('\n', stdout);
			return start;
		}

		/* strip trailing whitespace */
		local_end = bk;
		if (c == ' ') while (local_end > start && para[local_end - 1] == ' ') --local_end;
		if (local_end == start) {
			fputc('\n', stdout);
			return bk;
		}
	}
}

void aroff_flush_paragraph(int cr) {


	/* split and print any complete lines in the paragraph. 
	   if cr, print the final line as well.
	*/

	int start = 0;
	unsigned w = aroff_width[aroff_line];
	if ((int)w < MIN_WIDTH) w = MIN_WIDTH;


	if (!tab_count) {
		for (unsigned i = 0; i < pos; ++i) {
			if (para[i] == '\t') para[i] = ' ';
		}
	}


	if (cr) {
		/* remove trailing space (and sticky space) */
		while (pos > 0 && (para[pos-1] & 0x7f) == ' ') --pos;

		if (pos == 0) {
			fputc('\n', stdout);
			return;			
		}
	}
	/* secretly append a space so find_break can peek ahead. */
	para[pos] = ' ';

	if (pos < w && !cr) return;

	unsigned remaining = pos;

	while (remaining >= w) {
		int sz = one_line(start, start + w, 0);
		if (sz < 0) break;
		start = sz;
		remaining = pos - sz;
		/* trim leading whitespace...*/
		while (remaining > 0 && para[start] == ' ') {
			--remaining;
			++start;
		}
		aroff_line = 1;
		w = aroff_width[1];
		if ((int)w < MIN_WIDTH) w = MIN_WIDTH;
	}


	if (cr) {

		while (remaining > 0) {
			int sz = one_line(start, pos, 1);
			if (sz <= 0) break;
			start = sz;
			remaining = pos - sz;

			/* trim leading whitespace...*/
			while (remaining > 0 && para[start] == ' ') {
				--remaining;
				++start;
			}
			aroff_line = 1;
		}

		aroff_line = 0;
		pos = 0;

		return;
	}


	if (remaining && start) {
		memmove(para, para + start, remaining);
		memmove(style, style + start, remaining);
	}
	pos = remaining;

}


void aroff_append_string(const char *str, unsigned attr) {
	unsigned i,l;
	l = strlen(str);
	if (pos + l >= AROFF_BUFFER_SIZE) aroff_flush_paragraph(0);
	memcpy(para + pos, str, l);
	for (i = 0; i < l; ++i)
		style[pos++] = attr;
}

void usage(int ec) {

	fputs(
		"Usage:\n"
		"aroff [-ch] [-t terminal] [-T output] file ...\n"
		" -c            show printer options\n"
		" -t terminal   set terminal type\n"
		" -T output     set output type (termcap, ascii)\n"
		" -h            help\n",
		stdout);

	exit(ec);
}


void aw_process(FILE *f);
void awgs_process(FILE *f);

extern int is_awgs(void);
extern int is_aw(void);

static int parseT(const char *arg) {
	/* */
	if (!arg || !*arg) return -1;
	if (arg[0] == 't' && !strcmp(arg, "termcap")) return FMT_TERMCAP;
	if (arg[0] == 'a' && !strcmp(arg, "ascii")) return FMT_ASCII;
	if (arg[0] == 'p' && !strcmp(arg, "plain")) return FMT_PLAIN;
	return -1;
}

int main(int argc, char **argv) {

	int ch;
	unsigned i;

	flag_c = 0;
	flag_t = NULL;
	flag_T = FMT_TERMCAP;

	while ( (ch = getopt(argc, argv, "cT:t:")) != -1) {
		switch(ch) {
		case 'c':
			flag_c = 1;
			break;
		case 'h':
			usage(0);
			break;
		case 't':
			flag_t = optarg;
			break;
		case 'T':
			flag_T = parseT(optarg);
			if (flag_T < 0) usage(1);
			break;

		default:
			usage(EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0) usage(EX_NOINPUT);

	tc_init();

	for (i = 0; i < argc; ++i) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) err(EX_NOINPUT, "fopen %s", argv[i]);

		header_size = fread(header, 1, 512, f);
		if ((int)header_size <= 0) {
			errx(EX_DATAERR, "fread");
		}
		fseek(f, 0, SEEK_SET);

		if (is_awgs())
			awgs_process(f);
		else if (is_aw())
			aw_process(f);
		else
			warnx("unknown file type: %s", argv[i]);

		fclose(f);
	}
	exit(0);
}