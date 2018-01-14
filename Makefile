TARGET_DEVICE ?= attiny85
CPUFREQ ?= 16500000
CPPFLAGS += -Wall -Wextra -pedantic -Werror
CFLAGS += -std=c99
CPPFLAGS += -Os
CPPFLAGS += -g

.PHONY: all
all: menorah_03.hex

menorah_03: CC := avr-gcc
menorah_03: CXX := avr-g++
menorah_03: CPPFLAGS += -mmcu=$(TARGET_DEVICE) -DF_CPU=$(CPUFREQ)
menorah_03: LDFLAGS += -mmcu=$(TARGET_DEVICE)
menorah_03: CPPFLAGS += -mcall-prologues
menorah_03: LDFLAGS += -mcall-prologues

menorah_03.hex: menorah_03
	avr-nm --size-sort $< | awk '{ s=strtonum("0x" $$1); sum[tolower($$2)]+=s; print s, $$2, $$3} END { for (s in sum) print sum[s], s, "total" }' | avr-c++filt
	avr-size $<
	avr-objcopy -O ihex -R .eeprom $< $@

.PHONY: clean
clean:
	rm -f menorah_03 *.hex

.PHONY: deploy
deploy: menorah_03.hex
	stdbuf -o0 micronucleus $<
	@echo "Disconnect USB now!"
