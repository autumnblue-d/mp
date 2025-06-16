// For teensy 4.1 -- I2C2MIDI

#include <Arduino.h>
#include <USBHost_t36.h>
#include <MIDI.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>

// Use these with the Teensy 3.5 & 3.6 & 4.1 SD card
#define SDCARD_CS_PIN    BUILTIN_SDCARD
#define SDCARD_MOSI_PIN  11  // not actually used
#define SDCARD_SCK_PIN   13  // not actually used

#define print_dbg(s) Serial.println(s)
#define print_dbg_ulong(s) Serial.println(s)
#define print_dbg_hex(s) Serial.printf("0x%.4x",s)

#include "timers.h"
#include "events.h"
#include "notes.h"
#include "ii.h"

USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);
USBSerial userial(myusb);
// MIDIDevice_BigBuffer midiDevice(myusb);

// MIDI TRS
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

#define LED1 3 
#define LED2 2

#define MPB00 1
#define MPB01 2
#define MPB02 3
#define MPB03 4
#define MPB04 5
#define MPB05 6
#define MPB06 7
#define MPB07 8

/*
#include <stdio.h>

// asf
#include "delay.h"
#include "compiler.h"
#include "flashc.h"
#include "preprocessor.h"
#include "print_funcs.h"
#include "intc.h"
#include "pm.h"
#include "gpio.h"
#include "spi.h"
#include "sysclk.h"

// skeleton
#include "types.h"
#include "events.h"
#include "i2c.h"
#include "init_trilogy.h"
#include "init_common.h"
#include "monome.h"
#include "timers.h"
#include "adc.h"
#include "util.h"
#include "cdc.h"
#include "ftdi.h"
#include "twi.h"

// this
#include "conf_board.h"
#include "ii.h"
*/	

#define rnd random

#define FIRSTRUN_KEY 0x22

#define L2 12
#define L1 8
#define L0 4

u8 edit_row, key_count = 0, mode = 0, prev_mode = 0;
s8 kcount = 0;
s8 scount[8] = {0,0,0,0,0,0,0,0};

const u8 sign[8][8] = {{0,0,0,0,0,0,0,0},         // o
       {0,24,24,126,126,24,24,0},     			// +
       {0,0,0,126,126,0,0,0},       			// -
       {0,96,96,126,126,96,96,0},     			// >
       {0,6,6,126,126,6,6,0},       			// <
       {0,102,102,24,24,102,102,0},   			// * rnd
       {0,120,120,102,102,30,30,0},   			// <> up/down
       {0,126,126,102,102,126,126,0}};  		// [] sync

// const u8 outs[8] = {B00, B01, B02, B03, B04, B05, B06, B07};
const u8 outs[8] = {MPB00, MPB01, MPB02, MPB03, MPB04, MPB05, MPB06, MPB07};

u8 state[8] = {0,0,0,0,0,0,0,0};
u8 clear[8] = {0,0,0,0,0,0,0,0};

typedef struct {
	u8 count[8];		// length of cycle
	s8 position[8];		// current position in cycle
	s8 speed[8];		// speed of cycle
	u8 tick[8]; 		// position in speed countdown
	u8 min[8];
	u8 max[8];
	u8 trigger[8];
	u8 toggle[8];
	u8 rules[8];
	u8 rule_dests[8];
	u8 sync[8]; 		// if true, reset dest rule to count
	u8 sound;
	u8 pushed[8];
	u8 rule_dest_targets[8];
	u8 smin[8];
	u8 smax[8];
} mp_set;

// typedef const struct {
typedef struct {
	u8 fresh;
	u8 preset_select;
	u8 glyph[8][8];
	mp_set m[8];
} nvram_data_t;

mp_set m;

u8 preset_mode, preset_select, front_timer;
u8 glyph[8];

u8 clock_phase;
u16 clock_time, clock_temp;
u8 divknob, divmode;
s8 divtime;
u32 clockdivtime;
u8 clockdivcount;

u16 adc[8];
u8 SIZE, LENGTH, VARI;

u8 held_keys[32], key_times[256];

typedef void(*re_t)(void);
re_t re;

u8 monomeLedBuffer[256] = { 0 };
u8 monomeFrameDirty = 0;

extern void monome_set_quadrant_flag(u8 flag);

// NVRAM data structure located in the flash array.
// __attribute__((__section__(".flash_nvram")))
static nvram_data_t flashy;




////////////////////////////////////////////////////////////////////////////////
// prototypes

static void refresh(void);
static void refresh_mono(void);
static void refresh_preset(void);
static void clock(u8 phase);

// start/stop monome polling/refresh timers
extern void timers_set_monome(void);
extern void timers_unset_monome(void);

// check the event queue
static void check_events(void);

// handler protos
static void handler_None(s32 data) { ;; }
static void handler_KeyTimer(s32 data);
static void handler_Front(s32 data);
static void handler_ClockNormal(s32 data);
static void handler_ClockExt(s32 data);


u8 flash_is_fresh(void);
void flash_unfresh(void);
void flash_write(void);
void flash_read(void);


static void mp_process_ii(uint8_t *data, uint8_t l);



////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// application clock code

void gpio_set_gpio_pin(u8 pin) {
  // Serial.printf("Trigger on: %d\n", pin);
  // MIDI.sendNoteOn(60, 100, pin);
  MIDI.sendNoteOn(pin<5?pin-1:pin+3, 100, pin);

  if (pin>0 && pin<5) {
    char cmd[4] = { 0x00, (char)(pin-1), 1, 0 };
    Wire.beginTransmission(TELEXO);
    Wire.write(cmd, 4);
    Wire.endTransmission();
  }
  else if (pin>4 && pin<9) {
    char cmd[4] = { 0x11, (char)(pin-5), 0x3f, 0xff };
    Wire.beginTransmission(TELEXO);
    Wire.write(cmd, 4);
    Wire.endTransmission();
  }
}

void gpio_clr_gpio_pin(u8 pin) {
  // Serial.printf("midi channel off: %d\n", pin);
  // MIDI.sendNoteOff(60, 0, pin);
  MIDI.sendNoteOff(pin<5?pin-1:pin+3, 0, pin);

  if (pin>0 && pin<5) {
    char cmd[4] = { 0x00, (char)(pin-1), 0, 0 };
    Wire.beginTransmission(TELEXO);
    Wire.write(cmd, 4);
    Wire.endTransmission();
  }
  else if (pin>4 && pin<9) {
    char cmd[4] = { 0x11, (char)(pin-5), 0, 0 };
    Wire.beginTransmission(TELEXO);
    Wire.write(cmd, 4);
    Wire.endTransmission();
  }
}

void clock(u8 phase) {
	static u8 i;

	if(phase) {
		// gpio_set_gpio_pin(B10);
    digitalWrite(LED2, HIGH);

		for(i=0;i<8;i++) {
			if(m.pushed[i]) {
				for(int n=0;n<8;n++) {
					if(m.sync[i] & (1<<n)) {
						m.position[n] = m.count[n];
						m.tick[n] = m.speed[n];
					}

					if(m.trigger[i] & (1<<n)) {
						state[n] = 1;
						clear[n] = 1;
					}
					else if(m.toggle[i] & (1<<n)) {
						state[n] ^= 1;
					}
				}

				m.pushed[i] = 0;
			}

			if(m.tick[i] == 0) {
				m.tick[i] = m.speed[i];
				if(m.position[i] == 0) {
					// RULES
				    if(m.rules[i] == 1) {     // inc
				    	if(m.rule_dest_targets[i] & 1) {
					    	m.count[m.rule_dests[i]]++;
					    	if(m.count[m.rule_dests[i]] > m.max[m.rule_dests[i]]) {
					    		m.count[m.rule_dests[i]] = m.min[m.rule_dests[i]];
					    	}
					    }
					    if(m.rule_dest_targets[i] & 2) {
					    	m.speed[m.rule_dests[i]]++;
					    	if(m.speed[m.rule_dests[i]] > m.smax[m.rule_dests[i]]) {
					    		m.speed[m.rule_dests[i]] = m.smin[m.rule_dests[i]];
					    	}
					    }
				    }
				    else if(m.rules[i] == 2) {  // dec
				    	if(m.rule_dest_targets[i] & 1) {
				    		m.count[m.rule_dests[i]]--;
					    	if(m.count[m.rule_dests[i]] < m.min[m.rule_dests[i]]) {
					    		m.count[m.rule_dests[i]] = m.max[m.rule_dests[i]];
					    	}
					    }
					    if(m.rule_dest_targets[i] & 2) {
					    	m.speed[m.rule_dests[i]]--;
					    	if(m.speed[m.rule_dests[i]] < m.smin[m.rule_dests[i]]) {
					    		m.speed[m.rule_dests[i]] = m.smax[m.rule_dests[i]];
					    	}
					    }
				    }
				    else if(m.rules[i] == 3) {  // max
				    	if(m.rule_dest_targets[i] & 1)
				    		m.count[m.rule_dests[i]] = m.max[m.rule_dests[i]];
				    	if(m.rule_dest_targets[i] & 2)
				    		m.speed[m.rule_dests[i]] = m.smax[m.rule_dests[i]];
				    }
				    else if(m.rules[i] == 4) {  // min
				    	if(m.rule_dest_targets[i] & 1)
					    	m.count[m.rule_dests[i]] = m.min[m.rule_dests[i]];
					    if(m.rule_dest_targets[i] & 2)
					    	m.speed[m.rule_dests[i]] = m.smin[m.rule_dests[i]];
				    }
				    else if(m.rules[i] == 5) {  // rnd
				    	if(m.rule_dest_targets[i] & 1)
				    		m.count[m.rule_dests[i]] = 
				    			(rnd() % (m.max[m.rule_dests[i]] - m.min[m.rule_dests[i]] + 1)) + m.min[m.rule_dests[i]];
				    	if(m.rule_dest_targets[i] & 2)
				    		m.speed[m.rule_dests[i]] = 
				    			(rnd() % (m.smax[m.rule_dests[i]] - m.smin[m.rule_dests[i]] + 1)) + m.smin[m.rule_dests[i]];
				    					
				      // print_dbg("\r\n RANDOM: ");
				      // print_dbg_hex(m.count[m.rule_dests[i]]);
				      // print_dbg_hex(rnd() % 11);
				    }
				    else if(m.rules[i] == 6) {  // pole
				    	if(m.rule_dest_targets[i] & 1) {
					    	if(abs(m.count[m.rule_dests[i]] - m.min[m.rule_dests[i]]) < 
					    		abs(m.count[m.rule_dests[i]] - m.max[m.rule_dests[i]]) ) {
					    		m.count[m.rule_dests[i]] = m.max[m.rule_dests[i]];
					    	}
					    	else {
					    		m.count[m.rule_dests[i]] = m.min[m.rule_dests[i]];
					    	}
					    }
					    if(m.rule_dest_targets[i] & 2) {
					    	if(abs(m.speed[m.rule_dests[i]] - m.smin[m.rule_dests[i]]) < 
					    		abs(m.speed[m.rule_dests[i]] - m.smax[m.rule_dests[i]]) ) {
					    		m.speed[m.rule_dests[i]] = m.smax[m.rule_dests[i]];
					    	}
					    	else {
					    		m.speed[m.rule_dests[i]] = m.smin[m.rule_dests[i]];
					    	}
					    }
				    }
				    else if(m.rules[i] == 7) {  // stop
				    	if(m.rule_dest_targets[i] & 1)
				    		m.position[m.rule_dests[i]] = -1;
				    }

					m.position[i]--;

					for(int n=0;n<8;n++) {
						if(m.sync[i] & (1<<n)) {
							m.position[n] = m.count[n];
							m.tick[n] = m.speed[n];
						}

						if(m.trigger[i] & (1<<n)) {
							state[n] = 1;
							clear[n] = 1;
						}
						else if(m.toggle[i] & (1<<n)) {
							state[n] ^= 1;
						}
					}
				}
				else if(m.position[i] > 0) m.position[i]--;
			}
			else m.tick[i]--;
		}

		for(i=0;i<8;i++)
			if(state[i])
				gpio_set_gpio_pin(outs[i]);
			else
				gpio_clr_gpio_pin(outs[i]);

		monomeFrameDirty++;
	}
	else {
		// gpio_clr_gpio_pin(B10);
    digitalWrite(LED2, LOW);

		for(i=0;i<8;i++) {
			if(clear[i]) {
				gpio_clr_gpio_pin(outs[i]);
				state[i] = 0;
			}
			clear[i] = 0;
		}
 	}
}



////////////////////////////////////////////////////////////////////////////////
// timers

static softTimer_t clockTimer = { .next = NULL, .prev = NULL };
static softTimer_t midiTimer = {.next = NULL, .prev = NULL};
static softTimer_t keyTimer = { .next = NULL, .prev = NULL };
static softTimer_t adcTimer = { .next = NULL, .prev = NULL };
static softTimer_t monomePollTimer = { .next = NULL, .prev = NULL };
static softTimer_t monomeRefreshTimer  = { .next = NULL, .prev = NULL };

volatile u8 clock_external = 0;
volatile clock_pulse_t clock_pulse = &clock;

static void clockTimer_callback(void* o) {  
	if(clock_external == 0 || (clock_external && !divmode)) {
		clock_phase++;
		if(clock_phase>1) 
			clock_phase=0;
		(*clock_pulse)(clock_phase);
	}
}

static void keyTimer_callback(void* o) {  
	static event_t e;
	e.type = kEventKeyTimer;
	e.data = 0;
	event_post(&e);
}

static void adcTimer_callback(void* o) {  
	static event_t e;
	e.type = kEventPollADC;
	e.data = 0;
	event_post(&e);
}


// monome polling callback
static void monome_poll_timer_callback(void* obj) {
  // asynchronous, non-blocking read
  // UHC callback spawns appropriate events
	serial_read();
}

// monome refresh callback
static void monome_refresh_timer_callback(void* obj) {
	if(monomeFrameDirty > 0) {
		static event_t e;
		e.type = kEventMonomeRefresh;
		event_post(&e);
	}
}

// monome: start polling
void timers_set_monome(void) {
	// print_dbg("\r\n setting monome timers");
	timer_add(&monomePollTimer, 20, &monome_poll_timer_callback, NULL );
	timer_add(&monomeRefreshTimer, 30, &monome_refresh_timer_callback, NULL );
}

// monome stop polling
void timers_unset_monome(void) {
	// print_dbg("\r\n unsetting monome timers");
	timer_remove( &monomePollTimer );
	timer_remove( &monomeRefreshTimer ); 
}



////////////////////////////////////////////////////////////////////////////////
// event handlers

void ftdi_setup() {;;}

static void handler_FtdiConnect(s32 data) { ftdi_setup(); }
static void handler_FtdiDisconnect(s32 data) { 
	timers_unset_monome();
	// event_t e = { .type = kEventMonomeDisconnect };
	// event_post(&e);
}

static void handler_SerialConnect(s32 data) {
  // monome_setup_mext();
}

u8 monome_size_x() {
  return 16;
}

u8 monome_is_vari() {
  return 1;
}

static void handler_MonomeConnect(s32 data) {
	// print_dbg("\r\n// monome connect /////////////////"); 
	key_count = 0;
	SIZE = monome_size_x();
	LENGTH = SIZE - 1;
	// print_dbg("\r monome size: ");
	// print_dbg_ulong(SIZE);
	VARI = monome_is_vari();
	// print_dbg("\r monome vari: ");
	// print_dbg_ulong(VARI);

	if(VARI) re = &refresh;
	else re = &refresh_mono;


	// monome_set_quadrant_flag(0);
	// monome_set_quadrant_flag(1);
	timers_set_monome();
}

void monome_read_serial() {;;}
extern void monome_mext_refresh();

typedef void(*refresh_t)(void);
refresh_t monome_refresh = monome_mext_refresh;

static void handler_MonomePoll(s32 data) { monome_read_serial(); }
static void handler_MonomeRefresh(s32 data) {
	if(monomeFrameDirty) {
		if(preset_mode == 0) (*re)(); //refresh_mono();
		else refresh_preset();

		(*monome_refresh)();
	}
}

void adc_convert(u16 (*dst)[8]) {
  int i1;
  for (i1=0; i1<8; i1++) {
    Wire.beginTransmission(TELEXI);
    Wire.write(i1);
    Wire.endTransmission();
    Wire.requestFrom(TELEXI, 2);
    u8 b[2] = {0, 0};
    u8 i2 = 0;
    while(Wire.available()) {
      b[i2++]= Wire.read();
    }
    (*dst)[i1] = (((b[0] << 8 | b[1])) & 0x3fff) >> 2;
  }
  // Serial.printf("ADC: %d, %d, %d, %d\n", (*dst)[0], (*dst)[1], (*dst)[2], (*dst)[3]);
}

static void handler_PollADC(s32 data) {
	u16 i;
	adc_convert(&adc);

  if (adc[4] > 3000 && preset_mode != 1) {
    Serial.printf("==> FRONT HOLD: %d\n", adc[4]);
    static event_t e;
    e.type = kEventFront;
    e.data = 0;
    event_post(&e);
  }

	if(clock_external) {
		i = adc[0] / 455;
		if(i != divknob) {
			divknob = i;
			divtime = divknob - 4;
			if(divtime <= 0) {
				divmode = 1;
				divtime = -1 * divtime;
			}
			else {
				divmode = 0;
			}
			divtime = 1 << divtime;
		}
	}
	else {
	// CLOCK POT INPUT
	i = adc[0];
	i = i>>4;
	if(i != clock_temp) {
		// 500ms - 12ms
		clock_time = 12500 / (i + 25);
    clock_time = (clock_time / 3) * 3;
		// print_dbg("\r\nclock (ms): ");
		// print_dbg_ulong(clock_time);

		timer_set(&clockTimer, clock_time);
    timer_set(&midiTimer, clock_time / 3);
	}
	clock_temp = i;
	}
}

static void handler_Front(s32 data) {
	print_dbg("\r\n FRONT HOLD");

	if(data == 0) {
		front_timer = 15;
		if(preset_mode) preset_mode = 0;
		else preset_mode = 1;
	}
	else {
		front_timer = 0;
	}

	monomeFrameDirty++;
}

static void handler_SaveFlash(s32 data) {
	flash_write();
}

static void handler_KeyTimer(s32 data) {
	static u16 i1;

	if(front_timer) {
		if(front_timer == 1) {
			static event_t e;
			e.type = kEventSaveFlash;
			event_post(&e);

			preset_mode = 0;
			front_timer--;
		}
		else front_timer--;
	}

	for(i1=0;i1<key_count;i1++) {
		if(key_times[held_keys[i1]])
		if(--key_times[held_keys[i1]]==0) {
			if(preset_mode == 1) {
				if(held_keys[i1] % 16 == 0) {
					preset_select = held_keys[i1] / 16;
					// flash_write();
					static event_t e;
					e.type = kEventSaveFlash;
					event_post(&e);
					preset_mode = 0;
				}
			}

			// print_dbg("\rlong press: "); 
			// print_dbg_ulong(held_keys[i1]);
		}
	}
}

static void handler_ClockNormal(s32 data) {
	//DF clock_external = !gpio_get_pin_value(B09); 
}

static void handler_ClockExt(s32 data) {
	clockdivtime = time_now();
	time_clear();
	// div by 9. 16 8 4 2 0 2 4 8 16
	if(divmode && divtime == 1)
		clock(data);
	else if(divmode && data) {
		clockdivcount++;
		if(clockdivcount == (divtime >> 1)) {
			clock(0);
		}
		else if(clockdivcount >= divtime) {
			clock(1);
			clockdivcount = 0;
		}
	}
	else if(data) {
		clockdivtime = (clockdivtime / divtime);
		timer_reset_set(&clockTimer, clockdivtime);
	}
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// application grid code

void monome_grid_key_parse_event_data(u32 data, u8* x, u8* y, u8* val) {
  u8* bdata = (u8*)(&data);
  *x = bdata[0];
  *y = bdata[1];
  *val = bdata[2];
}

static void handler_MonomeGridKey(s32 data) { 
	u8 x, y, z, index, i1, found;
	monome_grid_key_parse_event_data(data, &x, &y, &z);
	// print_dbg("\r\n monome event; x: "); 
	// print_dbg_hex(x); 
	// print_dbg("; y: 0x"); 
	// print_dbg_hex(y); 
	// print_dbg("; z: 0x"); 
	// print_dbg_hex(z);

	//// TRACK LONG PRESSES
	index = y*16 + x;
	if(z) {
		held_keys[key_count] = index;
		key_count++;
		key_times[index] = 10;		//// THRESHOLD key hold time
	} else {
		found = 0; // "found"
		for(i1 = 0; i1<key_count; i1++) {
			if(held_keys[i1] == index) 
				found++;
			if(found) 
				held_keys[i1] = held_keys[i1+1];
		}
		key_count--;

		// FAST PRESS
		if(key_times[index] > 0) {
			if(preset_mode == 1) {
				if(x == 0 && y != preset_select) {
					preset_select = y;
					for(i1=0;i1<8;i1++)
						glyph[i1] = flashy.glyph[preset_select][i1];
				}
 				else if(x==0 && y == preset_select) {
					flash_read();

					preset_mode = 0;
				}

				monomeFrameDirty++;	
			}
			// print_dbg("\r\nfast press: ");
			// print_dbg_ulong(index);
			// print_dbg(": ");
			// print_dbg_ulong(key_times[index]);
		}
	}

	// PRESET SCREEN
	if(preset_mode) {
		// glyph magic
		if(z && x>7) {
			glyph[y] ^= 1<<(x-8);
		}

		monomeFrameDirty++;	
	}
	// NOT PRESET
	else {
		prev_mode = mode;

		// mode check
		if(x == 0) {
			kcount += (z<<1)-1;

			if(kcount < 0)
				kcount = 0;

			// print_dbg("\r\nkey count: ");
			// print_dbg_ulong(kcount);

			if(kcount == 1 && z == 1)
				mode = 1; 
			else if(kcount == 0) {
				mode = 0;
				scount[y] = 0;	
			}

			if(z == 1 && mode == 1) {
				edit_row = y;
			}
		}
		else if(x == 1 && mode != 0) {
			if(mode == 1 && z == 1) {
				mode = 2;
				edit_row = y;
			}
			else if(mode == 2 && z == 0)
				mode = 1;
		}
		// set position / minmax / stop
		else if(mode == 0) {
			scount[y] += (z<<1)-1;
			if(scount[y]<0) scount[y] = 0;		// in case of grid glitch?

			if(z == 1 && scount[y] == 1) {
				m.position[y] = x;
				m.count[y] = x;
				m.min[y] = x;
				m.max[y] = x;
				m.tick[y] = m.speed[y];

				if(m.sound) {
					m.pushed[y] = 1;
				}
			}
			else if(z == 1 && scount[y] == 2) {
				if(x < m.count[y]) {
					m.min[y] = x;
					m.max[y] = m.count[y];
				}
				else {
					m.max[y] = x;
					m.min[y] = m.count[y];
				}
			}
		}
		// set speeds and trig/tog
		else if(mode == 1) {
			scount[y] += (z<<1)-1;
			if(scount[y]<0) scount[y] = 0;

			if(z==1) {
				if(x > 7) {
					if(scount[y] == 1) {
						m.smin[y] = x-8;
						m.smax[y] = x-8;
						m.speed[y] = x-8;
						m.tick[y] = m.speed[y];
					}
					else if(scount[y] == 2) {
						if(x-8 < m.smin[y]) {
							m.smax[y] = m.smin[y];
							m.smin[y] = x-8;
						}
						else
							m.smax[y] = x-8;
					}
				}
				else if(x == 5) {
					m.toggle[edit_row] ^= 1<<y;
					m.trigger[edit_row] &= ~(1<<y);
				}
				else if(x == 6) {
					m.trigger[edit_row] ^= 1<<y;
					m.toggle[edit_row] &= ~(1<<y);
				}
				else if(x == 4) {
					m.sound ^= 1;
				}
				else if(x == 2) {
					if(m.position[y] == -1) {
						m.position[y] = m.count[y];
					}
					else {
						m.position[y] = -1;
					}
				}
				else if(x == 3) {
					m.sync[edit_row] ^= (1<<y);
				}
			}
		}
		else if(mode == 2 && z == 1) {
			if(x > 3 && x < 7) {
				m.rule_dests[edit_row] = y;
				m.rule_dest_targets[edit_row] = x-3;
			  // post("\nrule_dests", edit_row, ":", rule_dests[edit_row]);
			}
			else if(x > 6) {
				m.rules[edit_row] = y;
			  // post("\nrules", edit_row, ":", rules[edit_row]);
			}
		}

		monomeFrameDirty++;
	}
}

////////////////////////////////////////////////////////////////////////////////
// application grid redraw
static void refresh() {
	u8 i1, i2, i3;

	// clear grid
	for(i1=0;i1<128;i1++)
		monomeLedBuffer[i1] = 0;

	// SHOW POSITIONS
	if(mode == 0) {
		for(i1=0;i1<8;i1++) {
			for(i2=m.min[i1];i2<=m.max[i1];i2++)
				monomeLedBuffer[i1*16 + i2] = L0;
			monomeLedBuffer[i1*16 + m.count[i1]] = L1;
			if(m.position[i1] >= 0) {
				monomeLedBuffer[i1*16 + m.position[i1]] = L2;
			}
		}
	}
	// SHOW SPEED
	else if(mode == 1) {
		for(i1=0;i1<8;i1++) {
			if(m.position[i1] >= 0)
				monomeLedBuffer[i1*16 + m.position[i1]] = L0;

			if(m.position[i1] != -1)
				monomeLedBuffer[i1*16 + 2] = 2;

			for(i2=m.smin[i1];i2<=m.smax[i1];i2++)
				monomeLedBuffer[i1*16 + i2+8] = L0;

			monomeLedBuffer[i1*16 + m.speed[i1]+8] = L1;

			if(m.sound)
				monomeLedBuffer[i1*16 + 4] = 2;

			if(m.toggle[edit_row] & (1 << i1))
				monomeLedBuffer[i1*16 + 5] = L2;
			else
				monomeLedBuffer[i1*16 + 5] = L0;

			if(m.trigger[edit_row] & (1 << i1))
				monomeLedBuffer[i1*16 + 6] = L2;
			else
				monomeLedBuffer[i1*16 + 6] = L0;

			if(m.sync[edit_row] & (1<<i1))
				monomeLedBuffer[i1*16 + 3] = L1;
			else  
				monomeLedBuffer[i1*16 + 3] = L0;
		}

		monomeLedBuffer[edit_row * 16] = L2;
	}
	// SHOW RULES
	else if(mode == 2) {
		for(i1=0;i1<8;i1++) 
			if(m.position[i1] >= 0)
				monomeLedBuffer[i1*16 + m.position[i1]] = L0;

		monomeLedBuffer[edit_row * 16] = L1;
		monomeLedBuffer[edit_row * 16 + 1] = L1;

		if(m.rule_dest_targets[edit_row] == 1) {
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 4] = L2;
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 5] = L0;
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 6] = L0;
		}
		else if (m.rule_dest_targets[edit_row] == 2) {
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 4] = L0;
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 5] = L2;
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 6] = L0;
		}
		else {
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 4] = L2;
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 5] = L2;
			monomeLedBuffer[m.rule_dests[edit_row] * 16 + 6] = L0;
		}

		for(i1=8;i1<16;i1++)
			monomeLedBuffer[m.rules[edit_row] * 16 + i1] = L0;


		for(i1=0;i1<8;i1++) {
			i3 = sign[m.rules[edit_row]][i1];
			for(i2=0;i2<8;i2++) {
				if((i3 & (1<<i2)) != 0)
					monomeLedBuffer[i1*16 + 8 + i2] = L2;
			}
		}
	}

	monome_set_quadrant_flag(0);
	monome_set_quadrant_flag(1);
}


// application grid redraw without varibright
static void refresh_mono() {
	refresh();

	// monome_set_quadrant_flag(0);
	// monome_set_quadrant_flag(1);
}


static void refresh_preset() {
	u8 i1,i2;

	for(i1=0;i1<128;i1++)
		monomeLedBuffer[i1] = 0;

	monomeLedBuffer[preset_select * 16] = 11;

	for(i1=0;i1<8;i1++)
		for(i2=0;i2<8;i2++)
			if(glyph[i1] & (1<<i2))
				monomeLedBuffer[i1*16+i2+8] = 11;

	monome_set_quadrant_flag(0);
	monome_set_quadrant_flag(1);
}

static void mp_process_ii(uint8_t *data, uint8_t l) {
	uint8_t i;
	int d;

	i = data[0];
	d = (data[1] << 8) + data[2];
	
	switch(i) {
		case MP_PRESET:
			if(d<0 || d>7)
				break;
			preset_select = d;
			flash_read();
			break;
		case MP_RESET:
			if(d>0 && d<9) {
				d--;
				m.position[d] = m.count[d];
				m.tick[d] = m.speed[d];
			}
			else if(d==0) {
				for(int n=0;n<8;n++) {
					m.position[n] = m.count[n];
					m.tick[n] = m.speed[n];
				}
			}
			break;
		case MP_STOP:
			if(d>0 && d<9) {
				d--;
				m.position[d] = -1;
			}
			else if(d==0) {
				for(int n=0;n<8;n++)
					m.position[n] = -1;
			}
			break;
		default:
			break;
	}
  // print_dbg("\r\nmp: ");
  // print_dbg_ulong(i);
  // print_dbg(" ");
  // print_dbg_ulong(d);
}







// assign event handlers
static inline void assign_main_event_handlers(void) {
	app_event_handlers[ kEventFront ]	= &handler_Front;
	app_event_handlers[ kEventKeyTimer ] = &handler_KeyTimer;
	app_event_handlers[ kEventSaveFlash ] = &handler_SaveFlash;
	app_event_handlers[ kEventPollADC ]	= &handler_PollADC;
	app_event_handlers[ kEventClockNormal ] = &handler_ClockNormal;
	app_event_handlers[ kEventClockExt ] = &handler_ClockExt;
	app_event_handlers[ kEventFtdiConnect ]	= &handler_FtdiConnect ;
	app_event_handlers[ kEventFtdiDisconnect ]	= &handler_FtdiDisconnect ;
	app_event_handlers[ kEventMonomeConnect ]	= &handler_MonomeConnect ;
	app_event_handlers[ kEventMonomeDisconnect ]	= &handler_None ;
	app_event_handlers[ kEventMonomePoll ]	= &handler_MonomePoll ;
	app_event_handlers[ kEventMonomeRefresh ]	= &handler_MonomeRefresh ;
	app_event_handlers[ kEventMonomeGridKey ]	= &handler_MonomeGridKey ;
	app_event_handlers[ kEventSerialConnect ]	= &handler_SerialConnect ;
	app_event_handlers[ kEventSerialDisconnect ]	= &handler_FtdiDisconnect ;
}

// app event loop
void check_events(void) {
	static event_t e;
	if( event_next(&e) ) {
		(app_event_handlers)[e.type](e.data);
	}
}

// flash commands
u8 flash_is_fresh(void) {
  return (flashy.fresh != FIRSTRUN_KEY);
  // flashc_memcpy((void *)&flashy.fresh, &i, sizeof(flashy.fresh),   true);
  // flashc_memset32((void*)&(flashy.fresh), fresh_MAGIC, 4, true);
  // flashc_memset((void *)nvram_data, 0x00, 8, sizeof(*nvram_data), true);
}

// write fresh status
void flash_unfresh(void) {
  // flashc_memset8((void*)&(flashy.fresh), FIRSTRUN_KEY, 4, true);
  flashy.fresh = FIRSTRUN_KEY;
}

void flash_write(void) {
	// print_dbg("\r write preset ");
	// print_dbg_ulong(preset_select);
	// flashc_memcpy((void *)&flashy.m[preset_select], &m, sizeof(m), true);
	// flashc_memcpy((void *)&flashy.glyph[preset_select], &glyph, sizeof(glyph), true);
	memcpy((void *)&flashy.m[preset_select], &m, sizeof(m));
	memcpy((void *)&flashy.glyph[preset_select], &glyph, sizeof(glyph));
	// flashc_memset8((void*)&(flashy.preset_select), preset_select, 1, true);
  flashy.preset_select =  preset_select;

  // if (SD.exists("MP.BIN")) {
  //   print_dbg("Removing preset file.");
  //   SD.remove("MP.BIN");
  // }
  print_dbg("Writing preset file.");
  File preset_file = SD.open("MP.BIN", FILE_WRITE);
  preset_file.write(&flashy, sizeof(flashy));
  preset_file.close();

}

void flash_read(void) {
	u8 i1;

	print_dbg("\r\n read preset ");
	print_dbg_ulong(preset_select);

	// for(i1=0;i1<16;i1++) {
	// 	for(i2=0;i2<16;i2++) {
	// 		w.wp[i1].steps[i2] = flashy.w[preset_select].wp[i1].steps[i2];
	// 		w.wp[i1].step_probs[i2] = flashy.w[preset_select].wp[i1].step_probs[i2];
	// 		w.wp[i1].cv_probs[0][i2] = flashy.w[preset_select].wp[i1].cv_probs[0][i2];
	// 		w.wp[i1].cv_probs[1][i2] = flashy.w[preset_select].wp[i1].cv_probs[1][i2];
	// 		w.wp[i1].cv_curves[0][i2] = flashy.w[preset_select].wp[i1].cv_curves[0][i2];
	// 		w.wp[i1].cv_curves[1][i2] = flashy.w[preset_select].wp[i1].cv_curves[1][i2];
	// 		w.wp[i1].cv_steps[0][i2] = flashy.w[preset_select].wp[i1].cv_steps[0][i2];
	// 		w.wp[i1].cv_steps[1][i2] = flashy.w[preset_select].wp[i1].cv_steps[1][i2];
	// 		w.wp[i1].cv_values[i2] = flashy.w[preset_select].wp[i1].cv_values[i2];
	// 	}

	// 	w.wp[i1].step_choice = flashy.w[preset_select].wp[i1].step_choice;
	// 	w.wp[i1].loop_end = flashy.w[preset_select].wp[i1].loop_end;
	// 	w.wp[i1].loop_len = flashy.w[preset_select].wp[i1].loop_len;
	// 	w.wp[i1].loop_start = flashy.w[preset_select].wp[i1].loop_start;
	// 	w.wp[i1].loop_dir = flashy.w[preset_select].wp[i1].loop_dir;
	// 	w.wp[i1].step_mode = flashy.w[preset_select].wp[i1].step_mode;
	// 	w.wp[i1].cv_mode[0] = flashy.w[preset_select].wp[i1].cv_mode[0];
	// 	w.wp[i1].cv_mode[1] = flashy.w[preset_select].wp[i1].cv_mode[1];
	// }

	for(i1=0;i1<8;i1++) {
		m.count[i1] = flashy.m[preset_select].count[i1];
		m.position[i1] = flashy.m[preset_select].position[i1];
		m.speed[i1] = flashy.m[preset_select].speed[i1];
		m.tick[i1] = flashy.m[preset_select].tick[i1];
		m.min[i1] = flashy.m[preset_select].min[i1];
		m.max[i1] = flashy.m[preset_select].max[i1];
		m.trigger[i1] = flashy.m[preset_select].trigger[i1];
		m.toggle[i1] = flashy.m[preset_select].toggle[i1];
		m.rules[i1] = flashy.m[preset_select].rules[i1];
		m.rule_dests[i1] = flashy.m[preset_select].rule_dests[i1];
		m.sync[i1] = flashy.m[preset_select].sync[i1];
		m.rule_dest_targets[i1] = flashy.m[preset_select].rule_dest_targets[i1];
		m.smin[i1] = flashy.m[preset_select].smin[i1];
		m.smax[i1] = flashy.m[preset_select].smax[i1];
	}

	m.sound = flashy.m[preset_select].sound;
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
// main

IntervalTimer tcTicks;

static void midiTick(void *o) { 
  MIDI.sendClock();
}

// int setup(void) {
void setup() {
	u8 i1;

	// sysclk_init();
  tcTicks.begin(process_timers, 1000);

	// init_dbg_rs232(FMCK_HZ);

	// init_gpio();
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);

	assign_main_event_handlers();
	init_events();
	// init_tc();
	// init_spi();
	// init_adc();

	// irq_initialize_vectors();
	// register_interrupts();
	// cpu_irq_enable();

	// init_usb_host();
	// init_monome();

	// init_i2c_follower(0x30);

  myusb.begin();
  delay(5);

  userial.begin(115200);
  Serial.begin(115200);

  MIDI.begin(MIDI_CHANNEL_OMNI);

  Wire.begin();

  // Serial.print("Initializing SD card...");
  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here if no SD card, but print a message
    while (1) {
      print_dbg("Unable to access the SD card");
      delay(500);
    }
  }

  memset(&flashy, 0, sizeof(flashy));

  if (SD.exists("MP.BIN")) {
    print_dbg("Reading presets from SD card");
    File preset_file = SD.open("MP.BIN", FILE_READ);
    preset_file.read(&flashy, sizeof(flashy));
    preset_file.close();
  }

  print_dbg("initialization done.");

  if (userial) {
    const uint8_t *psz = userial.product();
    if (psz && *psz) Serial.printf("  product: % s\n", psz);
    const uint8_t *psm = userial.manufacturer();
    if (psm && *psm) Serial.printf("  manufacturer: % s\n", psm);
    const uint8_t *pss = userial.serialNumber();
    if (pss && *pss) Serial.printf("  serial number: % s\n", pss);
  }

  // SD.remove("MP.BIN");

	print_dbg("\r\n\n// meadowphysics //////////////////////////////// ");
	print_dbg_ulong(sizeof(flashy));

	print_dbg(" ");
	print_dbg_ulong(sizeof(m));


	if(flash_is_fresh()) {
		print_dbg("\r\nfirst run.");
		flash_unfresh();
		// flashc_memset32((void*)&(flashy.preset_select), 0, 4, true);
    flashy.preset_select = 0;


		// clear out some reasonable defaults
		for(i1=0;i1<8;i1++) {
			m.count[i1] = 7+i1;
			m.position[i1] = 7+i1;
			m.speed[i1] = 0;
			m.tick[i1] = 0;
			m.min[i1] = 7+i1;
			m.max[i1] = 7+i1;
			m.trigger[i1] = (1<<i1);
			m.toggle[i1] = 0;
			m.rules[i1] = 1;
			m.rule_dests[i1] = i1;
			m.sync[i1] = (1<<i1);
			m.rule_dest_targets[i1] = 3;
			m.smin[i1] = 0;
			m.smax[i1] = 0;
		}

		// save all presets, clear glyphs
		for(i1=0;i1<8;i1++) {
			// flashc_memcpy((void *)&flashy.m[i1], &m, sizeof(m), true);
			memcpy((void *)&flashy.m[i1], &m, sizeof(m));
			glyph[i1] = (1<<i1);
			// flashc_memcpy((void *)&flashy.glyph[i1], &glyph, sizeof(glyph), true);
			memcpy((void *)&flashy.glyph[i1], &glyph, sizeof(glyph));
		}
	}
	else {
		// load from flash at startup

    File preset_file = SD.open("MP.BIN", FILE_READ);
    print_dbg("Reading preset file.");
    preset_file.read(&flashy, sizeof(flashy));
    preset_file.close();
    print_dbg("Done");
    delay(5);

		preset_select = flashy.preset_select;
		flash_read();
		for(i1=0;i1<8;i1++)
			glyph[i1] = flashy.glyph[preset_select][i1];
	}

	LENGTH = 15;
	SIZE = 16;

	re = &refresh;

	//DF process_ii = &mp_process_ii;

	clock_pulse = &clock;
	clock_external = 0; //DF !gpio_get_pin_value(B09);

	timer_add(&clockTimer,120,&clockTimer_callback, NULL);
  timer_add(&midiTimer, 120/3, &midiTick, NULL);
	timer_add(&keyTimer,50,&keyTimer_callback, NULL);
	timer_add(&adcTimer,100,&adcTimer_callback, NULL);
	clock_temp = 10000; // out of ADC range to force tempo

  handler_MonomeConnect(1);
  adc_convert(&adc);
}

void check_serial();

void loop() {
  myusb.Task();
  check_events();
  check_serial();
}

void check_serial() {
  u8 i1 = 0;
  u8 buffer[64];
  memset(buffer, 0, sizeof(buffer));
  String s = "";
  while (i1<sizeof(buffer) && Serial.available()) {
    buffer[i1++] = Serial.read();
  }
  if (i1) {
    // Serial.printf("buffer size: 0x%.4x\n", i1);
    for(u8 i2=0; i2<i1; i2++) {
      // Serial.printf("buffer[%d]: 0x%.4x\n", i2, buffer[i2]);
      if (i2>0 && isDigit(buffer[i2])) s += (char)buffer[i2];
    }
    u16 a = s.toInt();
    switch (buffer[0]) {
      case 0x73: // s
        Serial.println("Saving Preset");
        flash_write();
        break;
      case 0x66: // f
        Serial.printf("Front Timer\n");
        // static event_t e;
        // e.type = kEventFront;
        // e.data = 0;
        // event_post(&e);
        preset_mode = 1;
        monomeFrameDirty++;
        break;
      case 0x65: // e
        Serial.printf("External Clock: %d\n", a);
        clock_external = a;
        break;
      case 0x78: // x
        if (a > 0) {
          preset_select = a % 8;
          static event_t e;
          e.type = kEventFront;
          e.data = 0;
          event_post(&e);
          break;
        }
        break;
    }
  }
}

