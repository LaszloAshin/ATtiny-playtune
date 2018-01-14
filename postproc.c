#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

enum Cmd {
	CMD_STOPNOTE = 0x80,
	CMD_PLAYNOTE = 0x90,
	CMD_RESTART = 0xe0,
	CMD_STOP = 0xf0,
};

static int width = 0;
static const int width_limit = 80;
static int put_count = 0;

static void
put(uint8_t c)
{
	if (width + 6 >= width_limit) {
		width = 0;
		fprintf(stdout, "\n");
	}
	fprintf(stdout, "0x%02x, ", c);
	width += 6;
	++put_count;
}

static uint8_t notes_to_play[4] = { 0 };
static uint8_t notemask_to_play = 0;
static uint8_t notemask_to_stop = 0;

static void
flushnotes()
{
	notemask_to_stop &= ~notemask_to_play;
	if (notemask_to_stop) {
		put(CMD_STOPNOTE | notemask_to_stop);
		fprintf(stderr, "CMD_STOPNOTE %x\n", notemask_to_stop);
	}
	if (notemask_to_play) {
		put(CMD_PLAYNOTE | notemask_to_play);
		fprintf(stderr, "CMD_PLAYNOTE %x\n", notemask_to_play);
		int i;
		for (i = 0; i < 4; ++i) {
			if (notemask_to_play & (1 << i)) {
				put(notes_to_play[i]);
			}
		}
	}
	notemask_to_play = 0;
	notemask_to_stop = 0;
}

static int last_ms = 0;
static int delay_ms = 0;

static void
flushdelay()
{
	if (!delay_ms) return;
	const int delta = delay_ms - last_ms;
	if (delta < 32 && delta >= -32) {
		put(0xc0 | (delta & 0x3f));
	} else {
		put((delay_ms >> 8) & 0x7f);
		put(delay_ms);
	}
	fprintf(stderr, "delay %d ms, delta %d ms\n", delay_ms, delta);
	last_ms = delay_ms;
	delay_ms = 0;
}

static const uint32_t decrement_PGM[124] = {
    3429L, 3633L, 3849L, 4078L, 4320L, 4577L, 4850L, 5138L, 5443L, 5767L, 6110L, 6473L, 
    6858L, 7266L, 7698L, 8156L, 8641L, 9155L, 9699L, 10276L, 10887L, 11534L, 12220L, 
    12947L, 13717L, 14532L, 15396L, 16312L, 17282L, 18310L, 19398L, 20552L, 21774L, 
    23069L, 24440L, 25894L, 27433L, 29065L, 30793L, 32624L, 34564L, 36619L, 38797L, 
    41104L, 43548L, 46137L, 48881L, 51787L, 54867L, 58129L, 61586L, 65248L, 69128L, 
    73238L, 77593L, 82207L, 87096L, 92275L, 97762L, 103575L, 109734L, 116259L, 123172L, 
    130496L, 138256L, 146477L, 155187L, 164415L, 174191L, 184549L, 195523L, 207150L, 
    219467L, 232518L, 246344L, 260992L, 276512L, 292954L, 310374L, 328830L, 348383L, 
    369099L, 391047L, 414299L, 438935L, 465035L, 492688L, 521984L, 553023L, 585908L, 
    620748L, 657659L, 696766L, 738198L, 782093L, 828599L, 877870L, 930071L, 985375L, 
    1043969L, 1106047L, 1171815L, 1241495L, 1315318L, 1393531L, 1476395L, 1564186L, 
    1657197L, 1755739L, 1860141L, 1970751L, 2087938L, 2212093L, 2343631L, 2482991L, 
    2630637L, 2787063L, 2952790L, 3128372L, 3314395L, 3511479L, 3720282L, 3941502L, 
    4175876L
};

static const uint32_t dec_PGM[12] = {
	2212093, 2343631, 2482991, 2630637, 2787063, 2952790,
	3128372, 3314395, 3511479, 3720282, 3941502, 4175876
};

int
main()
{
	int was_there_a_note = 0;
	for (;;) {
		const int first = getchar();
		if (first == EOF) break;
		if (first < 0x80) {
			flushnotes();
			if (!was_there_a_note) continue;
			delay_ms += first * 256 + getchar();
			continue;
		}
		flushdelay();
		const enum Cmd cmd = first & 0xf0;
		uint8_t chan = first & 0x0f;
		switch (cmd) {
		case CMD_PLAYNOTE:
			assert(chan < 4);
			notemask_to_play |= 1 << chan;
			const uint8_t note = getchar();
			const uint8_t n2 = note - 4;
//			fprintf(stderr, "note %d (%d/%2d) dec %d / %d (%d/%2d)\n", note, note / 12, note % 12, decrement_PGM[note], dec_PGM[n2 % 12] >> (9 - n2 / 12), 9 - n2 / 12, n2 % 12);
			notes_to_play[chan] = ((9 - n2 / 12) << 4) | (n2 % 12);
			was_there_a_note = !0;
			break;
		case CMD_STOPNOTE:
			assert(chan < 4);
			notemask_to_stop |= 1 << chan;
			was_there_a_note = !0;
			break;
		case CMD_RESTART:
			put(0xa0);
			break;
		case CMD_STOP:
			put(0xb0);
			break;
		}
	}
	fprintf(stderr, "%d\n", put_count);
}
