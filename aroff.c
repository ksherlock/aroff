

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

static char *bold_begin = NULL;
static char *bold_end = NULL;
static char *italic_begin = NULL;
static char *italic_end = NULL;
static char *underline_begin = NULL;
static char *underline_end = NULL;

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
	if (1) {
		/* ZH/ZR are italic begin/end.  Not usually supported. */

		cp = tgetstr("ZH", &pcap);
		if (cp) {
			italic_begin = tcap_buffer + tcap_buffer_offset;
			tputs(cp, 0, tputs_helper);
			tcap_buffer[tcap_buffer_offset++] = 0;
		}
		cp = tgetstr("ZR", &pcap);
		if (cp) {
			italic_end = tcap_buffer + tcap_buffer_offset;
			tputs(cp, 0, tputs_helper);
			tcap_buffer[tcap_buffer_offset++] = 0;
		}

		if (!italic_begin) {
			cp = tgetstr("us", &pcap);
			if (cp) {
				italic_begin = tcap_buffer + tcap_buffer_offset;
				tputs(cp, 0, tputs_helper);
				tcap_buffer[tcap_buffer_offset++] = 0;
			}
			cp = tgetstr("ue", &pcap);
			if (cp) {
				italic_end = tcap_buffer + tcap_buffer_offset;
				tputs(cp, 0, tputs_helper);
				tcap_buffer[tcap_buffer_offset++] = 0;
			}
		}
	}

	if (1) {
		cp = tgetstr("md", &pcap);
		if (cp) {
			bold_begin = tcap_buffer + tcap_buffer_offset;
			tputs(cp, 0, tputs_helper);
			tcap_buffer[tcap_buffer_offset++] = 0;
		}
		/* TODO -- me, if present, turns off *all* appearance modes */
		cp = tgetstr("me", &pcap);
		if (cp) {
			bold_end = tcap_buffer + tcap_buffer_offset;
			tputs(cp, 0, tputs_helper);
			tcap_buffer[tcap_buffer_offset++] = 0;
		}
		if (!bold_begin) {
			cp = tgetstr("so", &pcap);
			if (cp) {
				bold_begin = tcap_buffer + tcap_buffer_offset;
				tputs(cp, 0, tputs_helper);
				tcap_buffer[tcap_buffer_offset++] = 0;
			}
			cp = tgetstr("se", &pcap);
			if (cp) {
				bold_end = tcap_buffer + tcap_buffer_offset;
				tputs(cp, 0, tputs_helper);
				tcap_buffer[tcap_buffer_offset++] = 0;
			}
		}
	}

	if (1) {
		/* underlining */
		cp = tgetstr("us", &pcap);
		if (cp) {
			underline_begin = tcap_buffer + tcap_buffer_offset;
			tputs(cp, 0, tputs_helper);
			tcap_buffer[tcap_buffer_offset++] = 0;
		}
		cp = tgetstr("ue", &pcap);
		if (cp) {
			underline_end = tcap_buffer + tcap_buffer_offset;
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
}

/* on some termcaps, eg, vt100, underline end, bold end, etc disable all attr */
void tc_disable(unsigned attr) {
	if ((attr & ATTR_BOLD) && bold_end) fputs(bold_end, stdout);
	if ((attr & ATTR_UNDERLINE) && underline_end) fputs(underline_end, stdout);
	if ((attr & ATTR_ITALIC) && italic_end) fputs(italic_end, stdout);
}

void tc_enable(unsigned attr) {
	if ((attr & ATTR_BOLD) && bold_begin) fputs(bold_begin, stdout);
	if ((attr & ATTR_UNDERLINE) && underline_begin) fputs(underline_begin, stdout);
	if ((attr & ATTR_ITALIC) && italic_begin) fputs(italic_begin, stdout);
}


void aroff_render(unsigned char *text, unsigned char *style, unsigned length) {

	/* TODO */
	unsigned attr = 0;
	for (unsigned i = 0; i < length; ++i) {
		attr |= style[i];
		text[i] &= 0x7f; // sticky space
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