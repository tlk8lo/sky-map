MCU=attiny2313
CFLAGS = -Os -flto -Wall -DF_CPU=4000000UL -mmcu=$(MCU) -funsigned-char -fpack-struct -fshort-enums -fdata-sections -mrelax --std=gnu99

# Debug?
ifeq ($(DEBUG),)
	CFLAGS += -DNDEBUG
else
	CFLAGS += -DDEBUG
endif

all: clean skymap.elf

skymap.elf: dcf.c debug.c main.c
	avr-gcc $(CFLAGS) $^ -o $@
	avr-size -C --mcu=$(MCU) $@

clean:
	-rm skymap.elf
