#ifndef DEBUG_H
#define DEBUG_H

#include <avr/eeprom.h>

extern void log_dcf(uint16_t cur_angle, uint16_t des_angle, uint32_t minutes);
extern void log_sync(uint16_t cur_angle, uint16_t des_angle);

#endif