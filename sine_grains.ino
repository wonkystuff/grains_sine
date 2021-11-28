/* Minimal sine generator for testing AE Modular Grains. Very much based
 *  on the dr1.a wonkystuff firmware, so there may be some residual stuff
 *  hanging around from that…
 *
 * Pitch is controlled by P1/IN 1
 *
 * Copyright (C) 2017-2020  John A. Tuffen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Questions/queries can be directed to info@wonkystuff.net
 */

#include "calc.h"

// Base-timer is running at 16MHz
#define F_TIM (16000000L)

// let the preprocessor calculate the various register values 'coz
// they don't change after compile time
#if ((F_TIM/(SRATE)) < 255)
#define T0_MATCH ((F_TIM/(SRATE))-1)
#define T0_PRESCALE _BV(CS00)  //prescaler clk/1 (i.e. 8MHz)
#else
#define T0_MATCH (((F_TIM/8L)/(SRATE))-1)
#define T0_PRESCALE _BV(CS01)  //prescaler clk/8 (i.e. 1MHz)
#endif

#define OSCOUTREG (OCR2A)     // This is the PWM register associated with timer2

uint16_t          phase;      // The accumulated phase (distance through the wavetable)
uint16_t          pi;         // wavetable current phase increment (how much phase will increase per sample)

void setup()
{
  CLKPR = _BV(CLKPCE);
  CLKPR = 0;

  ///////////////////////////////////////////////
  // Set up Timer/Counter 2 for PWM output
  TCCR2A = 0;                          // stop the timer
  TCCR2B = 0;                          // stop the timer
  TCNT2  = 0;                          // zero the timer
  GTCCR  = _BV(PSRASY);                // reset the prescaler
  TCCR2A = _BV(WGM20)  | _BV(WGM21) |  // fast PWM to OCRA
           _BV(COM2A1) | _BV(COM2A0);  // OCR2A set at match; cleared at start
  TCCR2B = _BV(CS20);                  // fast pwm part 2; no prescale on input clock

  pinMode(11, OUTPUT);                 // Set pin 11 as an output — Grains PWM is here

  ///////////////////////////////////////////////
  // Set up Timer/Counter0 for sample-rate ISR
  TCCR0B  = 0;                         // stop the timer (no clock source)
  TCNT0   = 0;                         // zero the timer

  TCCR0A  = _BV(WGM01);                // CTC Mode
  TCCR0B  = T0_PRESCALE;               // Set the alculated prescaler value to start the timer
  OCR0A   = T0_MATCH;                  // calculated match value
  TIMSK0 |= _BV(OCIE0A);               // Enable the TIMER0_COMPA interrupt
}

// There are no real time constraints here, this is an idle loop after
// all...
void loop()
{
  // Fetch a fixed-point phase-increment value from the lookup
  // table over in calc.ino
  pi = pgm_read_word(&octaveLookup[analogRead(2)]);
}

// deal with oscillator.
// For frequency calculations, we need a fractional representation because
// integers don't have the resolution. Since we're using a device without
// native floating-point support, we implement a 'fixed-point' fractional
// system. This means we can do integer maths, which the processor is Ok with!
//
// The fixed-point scheme uses 16 bits, where the top 10 bits are the integer
// part, and the lower 6 bits are the fractional part.
//
// So, our 16 bits are arranged like this (integer vs. fractional)
//
// iiiiiiiiiiffffff
//
// In this scheme, the number 0b100000 (0x20, or 32) is equivalent to 0.5

ISR(TIMER0_COMPA_vect)
{
  // increment the phase counter
  phase += pi;

  // By shifting the 16 bit number by 6, we remove the fractional part
  // and we are left with the integer part (in the range 0-1023). This
  // is actually truncating the value, for better fidelity (ha!) we could
  // round it instead (by adding 0.5 before the truncation)
  uint16_t p = (phase) >> FRACBITS;

  // We could store our sinewave in a 1024-entry lookup table however
  // we save space by recognising the symmetry of the waveshape and
  // only storing 512 entries and playing it inverted for the second
  // half.

  // Calculate the index value (from 0-1023 to 0-511):
  uint16_t ix = p & 0x1ff;

  // hangover from the dr1.a code. Can't be bothered to change it tbh.
  // Two different half-height waves are added, we could of course
  // just fetch a single full-height value.
  uint8_t s1 = pgm_read_byte(&sine[ix]);
  uint8_t s2 = pgm_read_byte(&sine[ix]);
  uint8_t s = s1 + s2;

  // invert the wave for the second half
  OSCOUTREG = p & 0x200 ? -s : s;
}
