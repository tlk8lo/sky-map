#include "dcf.h"

// 8-bit BCD decoder
// Requires n > 0
static uint8_t dcf_decode_bcd( uint8_t *frame, uint8_t start, uint8_t n )
{
	uint8_t val = 0;
	uint8_t b = 1;
	uint8_t *end;
	
	frame += start;
	end = frame + n;

	// Spared 50 bytes with bitwise tricks
	do
	{
		if ( *frame++ ) val += b;
		if ( ( b <<= 1 ) == 16 ) b = 10;
	}
	while ( frame < end );
	
	return val;
}

// Checks parity of `n' bits starting at `start'
static uint8_t dcf_parity( uint8_t *frame, uint8_t start, uint8_t n )
{
	uint8_t parity = 0;
	uint8_t *end;
	
	frame += start;
	end = frame + n;
	
	do
		parity ^= *frame++;
	while ( frame < end );
	
	return parity;
}

// Month length (1 - January, 12 - December)
static uint8_t month_len( uint8_t month, uint8_t year )
{
	if ( month == 2 )
	{
		return 28 + ( year % 4 == 0 );
	}
	else
	{
		if ( month >= 8 )
			month--;
		return 30 + ( month & 1 );
	}
}

// Check if frame is valid
uint8_t dcf_validate( uint8_t* frame )
{
	// Validate frame
	if ( frame[0] ) return 0;   // Start of minute - bit0 shall be 0
	if ( !frame[20] ) return 0; // Start of time - bit20 shall be 1
	if ( frame[17] == frame[18] ) return 0;      // Exclusive CET / CEST
	if ( dcf_parity( frame, 21, 8 ) ) return 0;  // Minute parity
	if ( dcf_parity( frame, 29, 7 ) ) return 0;  // Hour parity
	if ( dcf_parity( frame, 36, 23 ) ) return 0; // Date parity
	
	return 1;
}

// Convert DCF77 frame to minutes since 00:00 01.01.2000
uint32_t dcf_parse( uint8_t *frame )
{	
	// Calculate YEARS
	uint8_t years = dcf_decode_bcd( frame, 50, 8 );			// 8 bits - year
	
	// Calculate DAYS
	uint16_t days = (uint16_t)years * 365 + (years + 3) / 4; // days in previous years
	uint8_t month = dcf_decode_bcd( frame, 45, 5 );			// 5 bits - month
	while ( --month )
		days += month_len( month, years ); // days in previous months
	days += dcf_decode_bcd( frame, 36, 6 );					// 6 bits - day of month
	days--; // first day of month is 1
	
	// Calculate HOURS
	uint32_t hours = (uint32_t)days * 24; // hours in previous days
	hours += dcf_decode_bcd( frame, 29, 6 );				// 6 bits - hour
	hours -= frame[17];										// Daylight saving bit
	
	// Calculate MINUTES
	uint32_t minutes = hours * 60; // minutes in previous hours
	minutes += dcf_decode_bcd( frame, 21, 7 );				// 7 bits - minute
	
	return minutes;
}