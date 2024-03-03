

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <termcap.h>
#include <unistd.h>

#include "aroff.h"

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



unsigned aroff_indent[2] = { 1, 1 };
unsigned aroff_width[2] = { 78, 78 };

unsigned char para[512];
unsigned char style[512];
unsigned pos;
unsigned just;

unsigned aroff_line = 0;
static unsigned flip_flop = 0;

int flag_c;
int flag_f;

static char *tc_se = NULL;
static char *tc_so = NULL;
static char *tc_ue = NULL;
static char *tc_us = NULL;
static char *tc_ZH = NULL;
static char *tc_ZR = NULL;
static char *tc_md = NULL;
static char *tc_mr = NULL;
static char *tc_me = NULL;

static char tcap_buffer[1024];
static unsigned tcap_buffer_offset = 0;

static int tputs_helper(int c) {
	if (c) tcap_buffer[tcap_buffer_offset++] = c;
	return 1;
}

void tc_init(void) {
	static char buffer[1024];
	static char buffer2[1024];
	char *term;
	char *cp;
	char *pcap;
	int ok;

	term = getenv("TERM");
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

	/* ZH/ZR are italic begin/end.  Not usually supported. */
	cp = tgetstr("ZH", &pcap);
	if (cp) {
		tc_ZH = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
	}

	cp = tgetstr("ZR", &pcap);
	if (cp) {
		tc_ZR = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
	}

	cp = tgetstr("md", &pcap);
	if (cp) {
		tc_md = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
	}

	cp = tgetstr("mr", &pcap);
	if (cp) {
		tc_mr = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
	}

	cp = tgetstr("me", &pcap);
	if (cp) {
		tc_me = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
	}

	cp = tgetstr("so", &pcap);
	if (cp) {
		tc_so = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
	}

	cp = tgetstr("se", &pcap);
	if (cp) {
		tc_se = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
	}

	/* underlining */
	cp = tgetstr("us", &pcap);
	if (cp) {
		tc_us = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
	}

	cp = tgetstr("ue", &pcap);
	if (cp) {
		tc_ue = tcap_buffer + tcap_buffer_offset;
		tputs(cp, 0, tputs_helper);
		tcap_buffer[tcap_buffer_offset++] = 0;
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
}

/* on some termcaps, eg, vt100, underline end, bold end, etc disable all attr */
void tc_disable(unsigned attr) {
	if (tc_me) {
		/* me disables *all* appearance modes */
		fputs(tc_me, stdout);
		return;
	}
	if ((attr & ATTR_BOLD) && tc_se) fputs(tc_se, stdout);
	if ((attr & ATTR_UNDERLINE) && tc_ue) fputs(tc_ue, stdout);
	if ((attr & ATTR_ITALIC) && tc_ZR) fputs(tc_ZR, stdout);
}

void tc_enable(unsigned attr) {
	if (attr & ATTR_BOLD) {
		if (tc_md) fputs(tc_md, stdout);
		else if (tc_so) fputs(tc_so, stdout);
	}
	if ((attr & ATTR_UNDERLINE) && tc_us) fputs(tc_us, stdout);
	if ((attr & ATTR_ITALIC) && tc_ZH) fputs(tc_ZH, stdout);
	if ((attr & ATTR_REVERSE) && tc_mr) fputs(tc_mr, stdout);
}


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
		// if (c == ' ') a = 0;
		if (c == '\t') {
			c = ' ';
			a = 0;
		}
		if (attr != a) {
			if (attr) tc_disable(attr);
			if (a) tc_enable(a);
			attr = a;
		}
		fputc(c, stdout);
	}
	if (attr) tc_disable(attr);
}



static void full_justify(unsigned start, unsigned end) {

	static unsigned char ftext[132];
	static unsigned char fstyle[132];

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
		/* TODO -- handle attribute printing */
		aroff_render(para + start, style + start, sz);
	}

	fputc('\n', stdout);
	aroff_line = 1;
}



void aroff_flush_paragraph(int cr) {


	/* split and print any complete lines in the paragraph. 
	   if cr, print the final line as well.
	*/

	int start = 0;
	int end = 0;
	unsigned w = aroff_width[aroff_line];
	if ((int)w < MIN_WIDTH) w = MIN_WIDTH;

	if (pos == 0 && cr) {
		fputc('\n', stdout);
		return;
	}

	if (pos < w && !cr) return;


	unsigned tlen = pos;
	while (tlen >= w) {

		int bk = start + w;
		while (bk >= start) {
			unsigned c = para[bk];
			if (c == ' ' || c == '-' || c == '/') break;
			--bk;
		}
		if (bk < start) {
			// no whitespace so just break
			end = bk = start + w;
		} else {
			++bk;
			end = bk;
			while (bk > start && para[bk-1] == ' ') --bk;
		}

		print_helper(start, bk, 0);
		start = end;

		/* trim leading whitespace on the next line... */
		while (start < pos && para[start] == ' ') ++start;

		tlen = pos - start;
		w = aroff_width[aroff_line];
		if ((int)w < MIN_WIDTH) w = MIN_WIDTH;
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
		aroff_line = 0;
	}
}

void aroff_append_string(const char *str, unsigned attr) {
	unsigned i,l;
	l = strlen(str);
	if (pos + l > 512) aroff_flush_paragraph(0);
	memcpy(para + pos, str, l);
	for (i = 0; i < l; ++i)
		style[pos++] = attr;
}

void usage(int ec) {

	fputs(
		"Usage:\n"
		"aroff [-ch] file ...\n"
		" -c   show codes\n"
		" -f   justify the unjustifiable\n"
		" -h   help\n",
		stdout);

	exit(ec);
}


void aw_process(FILE *f);
void awgs_process(FILE *f);


int main(int argc, char **argv) {

	int ch;
	unsigned i;

	flag_c = 0;
	flag_f = 0;

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

	tc_init();

	for (i = 0; i < argc; ++i) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) err(EX_NOINPUT, "fopen %s", argv[i]);
		awgs_process(f);
		fclose(f);
	}
	exit(0);
}