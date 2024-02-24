

unsigned char *docSaveArray;
unsigned char *rulers;

void process_awgs(FILE *f) {

	unsigned docSACount;
	unsigned numRulers = 1;
	unsigned numBlocks = 1;

	/* skip the header */
	fseek(f, 668, SEEK_SET);

	docSACount = fgetc(f) | (fgetc(f) << 8);

	docSaveArray = malloc(docSACount * 12);
	if (!docSaveArray) err(EX_OSERR, "malloc doc save array (%u entries)", docSACount);
	if (fread(docSaveArray, 12, docSACount, f) != docSACount)
		errx(EX_DATAERR, "fread");

	/* count up the rulers and text blocks */
	for (unsigned i = 0; i < docSACount; ++i) {

		unsigned char *ptr = docSaveArray + i * 12;
		unsigned x;
		x = (ptr[0] | (ptr[1] << 8)) + 1;
		if (x > numBlocks) numBlocks = x;

		/* ruler only valid if attr == 0 (normal text) */
		x = ptr[4] | ptr[5];
		if (x == 0) {
			x = (ptr[6] | (ptr[7] << 8)) + 1;
			if (x > numRulers) numRulers = x;
		}
	}

	rulers = malloc(numRulers * 52);
	if (!rulers) err(EX_OSERR, "malloc rulers (%u entries)", numRulers);

}

void update_style(unsigned style) {
	attr = 0;
	/* outline/shadow ignored */
	if (style & 0x80) attr |= ATTR_SUBSCRIPT;
	if (style & 0x40) attr |= ATTR_SUPERSCRIPT;
	if (style & 0x04) attr |= ATTR_UNDERLINE;
	if (style & 0x02) attr |= ATTR_ITALIC;
	if (style & 0x01) attr |= ATTR_BOLD;
}

void process_para(unsigned char *ruler, unsigned char *pp) {

	unsigned x;

	/* status bits */
	x = ruler[2];

	if (x & 0x80) just = JUST_FULL;
	if (x & 0x40) just = JUST_RIGHT;
	if (x & 0x20) just = JUST_CENTER;
	if (x & 0x10) just = JUST_LEFT;

	x = ruler[4] | (ruler[5] << 8);
	left_margin = x >> 3;

	x = ruler[8] | (ruler[9] << 8);
	right_margin = x >> 3;

	platen_width = right_margin - left_margin; 

	update_style(pp[2]);
	pp += 7;
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
				break;
			case 0x07:
				/* time token */
				break;
			case 0x09:
				/* tab char.... */
				break;
			}
		} else {
			if (c > 0x7f) c = '?';
			para[pos] = c;
			style[pos] = attr;
			++pos;
			if (pos == 512) flush_paragraph(0);

		}
	}
	flush_paragraph(1);
	attr = 0;
}