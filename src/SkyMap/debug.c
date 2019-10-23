#include "debug.h"

uint16_t dcf_cnt EEMEM;
uint16_t dcf_cur_angle[10] EEMEM;
uint16_t dcf_des_angle[10] EEMEM;
uint32_t dcf_minutes[10] EEMEM;
uint16_t sync_cnt EEMEM;
uint16_t sync_cur_angle[10] EEMEM;
uint16_t sync_des_angle[10] EEMEM;

void log_dcf(uint16_t cur_angle, uint16_t des_angle, uint32_t minutes)
{
	uint16_t cnt = eeprom_read_word(&dcf_cnt);
	eeprom_write_word(&dcf_cnt, cnt + 1);
	cnt %= 10;
	eeprom_write_word(dcf_cur_angle + cnt, cur_angle);
	eeprom_write_word(dcf_des_angle + cnt, des_angle);
	eeprom_write_dword(dcf_minutes + cnt, minutes);
}

void log_sync(uint16_t cur_angle, uint16_t des_angle)
{
	uint16_t cnt = eeprom_read_word(&sync_cnt);
	eeprom_write_word(&sync_cnt, cnt + 1);
	cnt %= 10;
	eeprom_write_word(sync_cur_angle + cnt, cur_angle);
	eeprom_write_word(sync_des_angle + cnt, des_angle);
}