FTDI_FLAGS := `pkg-config libftdi1 --cflags --libs`

ftdi245r_serial: ./main.c
	gcc -g -Wall -Wextra -Werror $(FTDI_FLAGS) ./main.c -o ftdi245r_serial

%.eeprom.txt: %.eeprom
	hexdump -Cv $< > $@

cmp: monome_mk_orig_ftdi245r.eeprom.txt ftdi245r.eeprom.txt
	-colordiff -u monome_mk_orig_ftdi245r.eeprom.txt ftdi245r.eeprom.txt
