/*
	Program sterujący obrotową mapą nieba.
	
	Copyright 2018, Jacek Wieczorek & Mikołaj Wąsacz
*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include "dcf.h"

#ifdef DEBUG
#include "debug.h"
#endif

/*
	Ports configuration

	Red LED - no valid frame received during last rotation
	Yellow LED - DCF output
	Green LED - motor step
*/
#define SYNC_DDR	DDRD
#define SYNC_PORT	PORTD
#define SYNC_PIN	PIND
#define SYNC_BIT	2

#define DCF_DDR		DDRD
#define DCF_PORT	PORTD
#define DCF_PIN		PIND
#define DCF_BIT		3

#define MOTOR_DDR	DDRB
#define MOTOR_PORT	PORTB
#define MOTOR_PIN	PINB
#define MOTOR_BITS	15

#define LEDS_DDR	DDRD
#define LEDS_PORT	PORTD
#define LEDS_PIN	PIND
#define R_LED_BIT	6
#define Y_LED_BIT	5
#define G_LED_BIT	4

/*
	Timer1 comp interrupt period consts

	Interrupt period = Rotation period * F_CPU / Steps per rotation
	Interrupt period = (36525/36625*86400) * 4000000 / 49421
	Interrupt period = 6973886
	Interrupt period = 57163 * 122
*/
#define STEPS_PER_ROT	49421
#define STEPS_OCR		57163
#define STEPS_COMP		122
#define STEPS_OFFSET	14144

/*
	Minutes to angle conversion consts

	Angle = Minutes * Steps for 100 years / Minutes in 100 years % Steps per rotation
	Angle = Minutes * (36625*49421) / (36525*24*60) % 49421
	Angle = Minutes * 1810044125 / 52596000 % 49421
	Angle = Minutes * 14480353 / 420768 % 49421
*/
#define MINUTES_MUL		14480353
#define MINUTES_DIV		420768

/*
	Timer0 comp interrupt period consts

	Interrupt period = 20 ms * F_CPU / Prescaler
	Interrupt period = 0.02 * 4000000 / 1024
	Interrupt period = 78
*/
#define DCF_OCR			78

/*
	Sync sensor debouncing consts (500 ms)
*/
#define SYNC_DEB_THRES	35

/*
	DCF waveform

	Time:          0        100       200       300       400       500       600       700       800       900       1000
	               ---------------------                                                                               ------
	               |         |         |                                                                               |
	Waveform:      |   "0"   |   "1"   |                                                                               |
	               |         |         |                                                                               |
	           -----         -------------------------------------------------------------------------------------------
	               ^    ^         ^         ^                                                                     ^         ^
	Threshold:   Reset  1         2         3                                                                     4         5
*/

/*
	DCF time consts (in units of 20 ms)
	1 - 40 ms
	2 - 140 ms
	3 - 240 ms
	4 - 900 ms
	5 - 1100 ms
	OVF - 1800 ms
	RES - 200 ms
*/
#define DCF_1_THRES	2
#define DCF_2_THRES	7
#define DCF_3_THRES	12
#define DCF_4_THRES	45
#define DCF_5_THRES	55
#define DCF_OVF_THRES	90
#define DCF_RES_VAL	10

#define	FWD 0
#define BWD 1

volatile uint16_t current_angle = 0;
volatile uint16_t desired_angle = 0;
volatile uint16_t led_angle = 0;

volatile uint8_t direction = FWD;

volatile uint8_t steps_cnt = 0;
volatile uint8_t stop_cnt = 0;
volatile uint8_t motor_pin = 0;

uint8_t data[59];

volatile uint8_t ms_dcf = 0;
volatile uint8_t ms_sync = 0;

volatile uint8_t first_dcf = 0;
volatile uint8_t first_sync = 0;

/*
	Enable motor output pins sequentially
*/
void motor_step_start(uint8_t direction)
{
	uint8_t pin = motor_pin;
	if (direction == FWD)
	{
		pin >>= 1;
		if (pin == 0)
			pin = 8;
	}
	else
	{
		pin <<= 1;
		if (pin == 16)
			pin = 1;
	}
	motor_pin = pin;
	
	//MOTOR_PORT = (MOTOR_PORT & ~0x0F) | pin;
	PORTB = pin;
}

/*
	Disable motor output pins
*/
void motor_step_stop()
{
	//MOTOR_PORT &= ~0x0F;
	PORTB = 0;
}

/*
	Convert minutes since 00:00 01.01.2000 to angle
*/
uint16_t calculate_angle(uint32_t minutes)
{
	// Could be optimized
	uint32_t x = (uint64_t)minutes * MINUTES_MUL / MINUTES_DIV;
	return x % STEPS_PER_ROT;
}

/*
	Reset Timer0
*/
void reset_timer()
{
	GTCCR = (1<<PSR10);
	TCNT0 = 0;
	TIFR = (1<<OCF0A);
}

/*
	Increment DCF timer until it reaches DCF_OVF_THRES
*/
ISR(TIMER0_COMPA_vect)
{
	uint8_t dcf = ms_dcf;
	if (dcf < DCF_OVF_THRES)
		ms_dcf = dcf + 1;
}

/*
	Increment desired_angle every STEPS_COMP_VAL calls
	If current_angle != desired_angle then step motor in preferred direction
	Else stop motor
*/
ISR(TIMER1_COMPA_vect)
{
	uint16_t des_angle = desired_angle;
	
	if (first_dcf)
	{
		// Increment desired_angle
		uint8_t cnt = steps_cnt;
		if (++cnt == STEPS_COMP)
		{
			cnt = 0;
			if (++des_angle == STEPS_PER_ROT)
				des_angle = 0;
			desired_angle = des_angle;

			if (led_angle == des_angle)
				LEDS_PORT |= (1<<R_LED_BIT);
		}
		steps_cnt = cnt;
	}
	
	if (first_sync)
	{
		uint16_t cur_angle = current_angle;
	
		// Step motor
		if (cur_angle != des_angle)
		{
			// Determine direction
			uint16_t diff;
			if (cur_angle < des_angle)
				diff = des_angle - cur_angle;
			else
				diff = STEPS_PER_ROT + des_angle - cur_angle;
		
			uint8_t dir;

			// Step FWD
			if (diff < STEPS_PER_ROT / 2)
			{
				if (++cur_angle == STEPS_PER_ROT)
					cur_angle = 0;
				dir = FWD;
			}
			// Step BWD
			else
			{
				if (--cur_angle == -1)
					cur_angle = STEPS_PER_ROT - 1;
				dir = BWD;
			}

			motor_step_start(dir);

			LEDS_PORT |= (1<<G_LED_BIT);
		
			current_angle = cur_angle;
			direction = dir;
			stop_cnt = 0;
		}
		// Stop motor
		else 
		{
			motor_step_stop();
		
			LEDS_PORT &= ~(1<<G_LED_BIT);
		}
	}
	else
		motor_step_start(FWD);

	uint8_t sync = ms_sync;
	if (sync < SYNC_DEB_THRES)
		ms_sync = sync + 1;
}

int main(void)
{
	// Setup ports
	//MOTOR_DDR |= MOTOR_PINS;
	//SYNC_PORT |= (1<<SYNC_BIT);
	//DCF_PORT |= (1<<DCF_BIT);
	//LEDS_DDR |= (1<<R_LED_BIT) | (1<<Y_LED_BIT) | (1<<G_LED_BIT);
	//LEDS_PORT |= (1<<R_LED_BIT);
	DDRB = MOTOR_BITS;
	DDRD = (1<<R_LED_BIT) | (1<<Y_LED_BIT) | (1<<G_LED_BIT);
	PORTD = (1<<R_LED_BIT) | (1<<G_LED_BIT) | (1<<DCF_BIT) | (1<<SYNC_BIT);

	// Timer0 config - CTC, prescaler = 1024
	OCR0A = DCF_OCR - 1;
	TCCR0A = (1<<WGM01);
	TCCR0B = (1<<CS02) | (1<<CS00);
	
	// Timer1 config - CTC, prescaler = 1
	OCR1A = STEPS_OCR - 1;
	TIMSK = (1<<OCIE1A) | (1<<OCIE0A);
	TCCR1B = (1<<WGM12) | (1<<CS10);
	
	// Interrupt config - INT0 - both edges
	MCUCR = (1<<ISC00);

	motor_pin = 1;
	desired_angle = STEPS_OFFSET;
	
	uint8_t cnt = 0;
	
	EIFR = (1<<INTF0);

	//uint8_t dcf_state = DCF_PIN & (1<<DCF_BIT);
	//uint8_t sync_state = SYNC_PIN & (1<<SYNC_BIT);
	uint8_t in_state = PIND;
	uint8_t dcf_state = in_state & (1<<DCF_BIT);
	uint8_t sync_state = in_state & (1<<SYNC_BIT);

	if (dcf_state)
		LEDS_PORT |= (1<<Y_LED_BIT);

	// Main loop
	while (1)
	{
		uint8_t tim = ms_dcf;

		// DCF timer overflow
		if (tim == DCF_OVF_THRES)
		{
			sei();
			if (cnt == 59 && !dcf_state && dcf_validate(data))
			{
				uint32_t minutes = dcf_parse(data);
				uint16_t des_angle = calculate_angle(minutes);

				cli();
				steps_cnt = 0;
#ifdef DEBUG
				uint16_t cur_angle = current_angle;
				uint16_t old_des_angle = desired_angle;
#endif
				desired_angle = des_angle;
				led_angle = des_angle;
				first_dcf = 1;

				LEDS_PORT &= ~(1<<R_LED_BIT);
#ifdef DEBUG
				sei();
				log_dcf(cur_angle, old_des_angle, minutes);
#endif
			}

			cnt = 0;
			cli();
		}
		
		uint8_t new_dcf_state = DCF_PIN & (1<<DCF_BIT);
		sei();

		// DCF edge interrupt
		if (new_dcf_state != dcf_state)
		{
			// Rising edge
			if (new_dcf_state)
			{
				cli();
				reset_timer();
				ms_dcf = 0;
				sei();

				if ((cnt && cnt < 59 && tim >= DCF_4_THRES && tim < DCF_5_THRES) || tim == DCF_OVF_THRES)
					cnt++;
				else
					cnt = 0;

				LEDS_PORT |= (1<<Y_LED_BIT);
			}
			// Falling edge
			else
			{
				if (cnt && tim >= DCF_1_THRES && tim < DCF_3_THRES)
					data[cnt - 1] = tim >= DCF_2_THRES;
				else
				{
					cli();
					reset_timer();
					ms_dcf = DCF_RES_VAL;
					sei();
					
					cnt = 0;
				}

				LEDS_PORT &= ~(1<<Y_LED_BIT);
			}
			
			dcf_state = new_dcf_state;
		}

		// SYNC timer overflow
		if (ms_sync == SYNC_DEB_THRES)
		{
			uint8_t new_sync_state = SYNC_PIN & (1<<SYNC_BIT);
			
			if (!new_sync_state && sync_state && direction == FWD)
			{
				cli();
#ifdef DEBUG
				uint16_t cur_angle = current_angle;
				uint16_t des_angle = desired_angle;
#endif
				current_angle = STEPS_OFFSET;
				first_sync = 1;
				sei();
#ifdef DEBUG
				log_sync(cur_angle, des_angle);
#endif
			}
			
			sync_state = new_sync_state;
		}
		cli();
		
		// SYNC edge interrupt
		if (EIFR & (1<<INTF0))
		{
			EIFR = (1<<INTF0);
			ms_sync = 0;
		}
	}
}
