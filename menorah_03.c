#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#define NUM_CHANS 4		// number of speaker outputs

#define CPU_MHZ	8		// ATtiny CKSEL 0100: 8 Mhz, 1x prescale (lfuse=E4)

/*  Menorah I/O port assignments */

#define SPEAKER0 PB0  // output, speaker 0
#define SPEAKER1 PB1  // output, speaker 1
#define SPEAKER2 PB2  // output, speaker 2
#define SPEAKER3 PB3  // output, speaker 3

typedef uint8_t byte;
typedef uint8_t boolean;
#define false 0
#define true 1
#define noInterrupts cli
#define Interrupts sei

// variable for timing

static volatile unsigned int scorewait_interrupt_count;
static volatile unsigned int delaywait_interrupt_count;
static uint16_t delay;

// variables for music-playing

static volatile const byte *score_start;
static volatile const byte *score_cursor;
static volatile boolean tune_playing;

static volatile long accumulator [NUM_CHANS];
static volatile long decrement [NUM_CHANS];
static volatile boolean playing [NUM_CHANS];

/* Table of accumulator decrement values, generated by a companion Excel spreadsheet.
   These depend on the polling frequency and the accumulator restart value.
   We basically do incremental division for each channel in the polling interrupt routine:
        accum -= decrement
        if (accum < 0) {
            toggle speaker output
            accum += ACCUM_RESTART
        }
*/

#define POLLTIME_USEC 50		// polling interval in microseconds
#define ACCUM_RESTART 4194304L	// 2^22 allows 1-byte addition on 3- or 4-byte numbers

static void tune_playnote (byte chan, byte note);
static void tune_stopnote (byte chan);
static void tune_stepscore (void);


//--------------------------------------------------------------------------
// Initialize the timers
//--------------------------------------------------------------------------

static void init_timers () {
    
    // We use the 8 bit timer to generate the polling interrupt for notes.
    // It should interrupt often, like every 50 microseconds.
    
    TCCR0A = (1 << WGM01);	// mode 010: CTC   
    TCCR0B = 1 << CS01;		// clock select 010: clk/8 prescaling
    OCR0A = 16500 * 0.05 / 8;
    
    // We use the 16 bit timer both for timing scores from the interrupt routine
    // and doing mainline code waits. It interrupts once a millisecond.
    
    TCCR1 = (1 << CTC1) | (1 << CS13);
    OCR1A = 129; // 16500/(128*129)
    OCR1C = 129;

    tune_playing = false;    
    scorewait_interrupt_count = 0;
    delaywait_interrupt_count = 0;
    delay = 0;
    
    TIMSK =(1<<OCIE0A) | (1<<OCIE1A); // turn on match A interrupts for both timers
    Interrupts();		 // enable interrupts

}


//--------------------------------------------------------------------------
// Start playing a note on a particular channel
//--------------------------------------------------------------------------

static void tune_playnote (byte chan, byte note) {
    if (chan < NUM_CHANS) {
		static const uint32_t dec_PGM[12] PROGMEM = {
			2212093, 2343631, 2482991, 2630637, 2787063, 2952790,
			3128372, 3314395, 3511479, 3720282, 3941502, 4175876
		};
        decrement[chan] = pgm_read_dword(dec_PGM + (note & 0x0f)) >> (note >> 4);
        accumulator[chan] = ACCUM_RESTART;
        playing[chan]=true;
    }
}


//--------------------------------------------------------------------------
// Stop playing a note on a particular channel
//--------------------------------------------------------------------------

static void tune_stopnote (byte chan) {
    playing[chan]= false;
}


//--------------------------------------------------------------------------
//   Play a score
//--------------------------------------------------------------------------

static void tune_stopscore();

static void tune_playscore (const byte *score) {
    if (tune_playing) tune_stopscore();
    score_start = score;
    score_cursor = score;
    tune_stepscore();	/* execute initial commands */
    tune_playing = true;  /* release the interrupt routine */
}

/* Do score commands until a "wait" is found, or the score is stopped.
 This is called initially from tune_playcore, but then is called
 from the interrupt routine when waits expire.
 */

enum Command {
  CMD_STOPNOTE = 0x80, // stop a note: low nibble is generator #
  CMD_PLAYNOTE = 0x90, // play a note: low nibble is generator #, note is next byte
  CMD_RESTART = 0xa0,  // restart the score from the beginning
  CMD_STOP = 0xb0,     // stop playing
// if CMD < 0x80, then the other 7 bits and the next byte are a 15-bit big-endian number of msec to wait
};

static void tune_stepscore (void) {
  while (1) {
    const byte cmd = pgm_read_byte(score_cursor++);
    if (cmd < 0x80) {
      /* wait count is in msec. */
      delay = ((unsigned)cmd << 8) | (pgm_read_byte(score_cursor++));
      scorewait_interrupt_count = delay;
      break;
    }
    const byte opcode = cmd & 0xf0;
    const byte chan = cmd & 0x0f;
    // XXX: rewriting this in a switch causes 14 byte overhead. WHY?
    if (opcode == CMD_STOPNOTE) {
      byte i;
      for (i = 0; i < 4; ++i) {
        if (chan & (1 << i)) tune_stopnote(i);
      }
    } else if (opcode == CMD_PLAYNOTE) {
      byte i;
      for (i = 0; i < 4; ++i) {
        if (chan & (1 << i)) tune_playnote(i, pgm_read_byte(score_cursor++));
      }
    } else if (opcode == CMD_RESTART) {
      score_cursor = score_start;
      delay = 0;
    } else if (opcode == CMD_STOP) {
      tune_playing = false;
      break;
    } else if (opcode & 0xc0) {
      int8_t delta = cmd & 0x3f;
      if (cmd & 0x20) delta |= 0xc0;
      delay += delta;
      scorewait_interrupt_count = delay;
      break;
    }
  }
}

//--------------------------------------------------------------------------
// Stop playing a score
//--------------------------------------------------------------------------

static void tune_stopscore (void) {
  tune_stopnote(0);
  tune_stopnote(1);
  tune_stopnote(2);
  tune_stopnote(3);  // depends on NUM_CHANS==4
  tune_playing = false;
}

//--------------------------------------------------------------------------
// Delay a specified number of milliseconds, up to about 30 seconds.
//--------------------------------------------------------------------------
/*
static void tune_delay (unsigned duration) {
    boolean notdone;
    
    delaywait_interrupt_count = duration;
    do {
        // wait until the interrupt routines decrements the toggle count to zero
        noInterrupts();
        notdone = delaywait_interrupt_count != 0;  // interrupt-safe test
        Interrupts();
    }  while (notdone);
}
*/
//--------------------------------------------------------------------------
// Stop all channels
//--------------------------------------------------------------------------
/*
static void tune_stopchans(void) {

  TIMSK &= ~(1 << OCIE0A);  // disable all timer interrupts
  TIMSK &= ~(1 << OCIE1A);
}
*/
//--------------------------------------------------------------------------
//  Timer interrupt Service Routines
//--------------------------------------------------------------------------

ISR(TIMER0_COMPA_vect) { //******* 8-bit timer: 50 microsecond interrupts
    
// For even greater efficiency, we could write this in assembly code
// and do 3-byte instead of 4-byte arithmetic.

	for (byte i = 0; i != 4; ++i) {
		if (!playing[i]) continue;
		long a = accumulator[i];
		a -= decrement[i];
		if (a < 0) {
			PORTB ^= (1<<i);
			a += ACCUM_RESTART;
		}
		accumulator[i] = a;
	}
}

ISR(TIMER1_COMPA_vect) { //******* 16-bit timer: millisecond interrupts
    // decrement score wait counter
    if (tune_playing && scorewait_interrupt_count && --scorewait_interrupt_count == 0) {
        // end of a score wait, so execute more score commands
        tune_stepscore ();  // execute commands
    }
    
    // decrement delay wait counter
    if (delaywait_interrupt_count) {
        --delaywait_interrupt_count;	// countdown for tune_delay()
    }
}

static const unsigned char PROGMEM score [] = {
#	include "pp"
};
//#include "Mario-Sheet-Music-Underwater-Theme.c"

//#include "Mario-Sheet-Music-1-Up-Mushroom-Sound.c"
//#include "Mario-Sheet-Music-Castle-Theme.c"
//#include "Mario-Sheet-Music-Coin-Sound.c"
//#include "Mario-Sheet-Music-Damage-Warp-Sound.c"
//#include "Mario-Sheet-Music-Death-Sound.c"
//!#include "Mario-Sheet-Music-Ending-Theme.c"
//#include "Mario-Sheet-Music-Flagpole-Fanfare.c"
//#include "Mario-Sheet-Music-Game-Over-Sound.c"
//#include "Mario-Sheet-Music-Item-Block-Sound.c"
//#include "Mario-Sheet-Music-Overworld-Main-Theme.c"
//#include "Mario-Sheet-Music-Pause-Sound.c"
//#include "Mario-Sheet-Music-Power-Up-Sound.c"
//#include "Mario-Sheet-Music-Rescue-Fanfare.c"
//!#include "Mario-Sheet-Music-Starman-Theme.c"
//#include "Mario-Sheet-Music-Time-Warning-Sound.c"
//!!#include "Mario-Sheet-Music-Underwater-Theme.c"
//!#include "Mario-Sheet-Music-Underworld-Theme.c"
//#include "Mario-Sheet-Music-Vine-Sound.c"
//#include "bunny.c"
//#include "dm2int.c"
//#include "dm2ttl.c"
//#include "e1m1.c"
//#include "e1m2.c"
//#include "e1m3.c"
//#include "e1m4.c"
//#include "e1m5.c"
//#include "e1m6.c"
//#include "e1m7.c"
//!#include "e1m8.c"
//#include "e1m9.c"
//#include "e2m1.c"
//#include "e2m2.c"
//#include "e2m3.c"
//#include "e2m4.c"
//#include "e2m5.c"
//#include "e2m6.c"
//#include "e2m7.c"
//#include "e2m8.c"
//#include "e2m9.c"
//#include "e3m1.c"
//#include "e3m2.c"
//#include "e3m3.c"
//!#include "e3m4.c"
//#include "e3m5.c"
//#include "e3m6.c"
//#include "e3m7.c"
//#include "e3m8.c"
//#include "e3m9.c"
//#include "inter.c"
//!#include "intro.c"
//#include "map01.c"
//#include "map02.c"
//#include "map03.c"
//#include "map04.c"
//#include "map05.c"
//xxx#include "map06.c"
//#include "map07.c"
//#include "map08.c"
//#include "map09.c"
//#include "map10.c"
//#include "map18.c"
//#include "map20.c"
//#include "map23.c"
//#include "map25.c"
//#include "map28.c"
//#include "map30.c"
//#include "map31.c"
//#include "map32.c"
//#include "read_m.c"
//#include "victor.c"

//
//-----------------------------------------------------------------------------------------------------
//                    **************   main logic   ***********************
//-----------------------------------------------------------------------------------------------------

int main (void) {
    byte i;
    
    // configure I/O ports
    DDRB = (1 << SPEAKER0) | (1 << SPEAKER1) | (1 << SPEAKER2) | (1 << SPEAKER3);
    PORTB = 0;

    for (i=0; i<NUM_CHANS; ++i)
        playing[i] = false;
        
    init_timers();      // initialize both timers, for music and for delays
    

#if 0	//	test all notes
{byte note;
    for (note=21; note<109; ++note) {
        tune_playnote(0, note);
        tune_delay(200);
        tune_stopnote(0);
        tune_delay(50);
    }
    tune_delay(1000);
}
#endif
#if 0	// test wait timer accuracy
{byte i;
    for (i=0; i<10; ++i){
        tune_playnote(0,60);
        tune_delay(100);
        tune_stopnote(0);
        tune_delay(900);
    }
}
#endif

    tune_playscore (score);	
    while (tune_playing);
    tune_stopscore();
    PORTB = 0;
    DDRB = 0;
    for (;;);
}
