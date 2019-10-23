#ifndef DCF_H
#define DCF_H

#include <inttypes.h>

extern uint8_t dcf_validate( uint8_t* frame );
extern uint32_t dcf_parse( uint8_t* frame );

#endif