MIPSCC=mips-openwrt-linux-gcc
CC=gcc

FLAGS=-lpthread 

RM=rm -f

all: wardriving wardrivingZsun

wardriving: wardriving.c 
	$(CC) $< -o $@ $(FLAGS)

wardrivingZsun: wardriving.c 
	$(MIPSCC) $< -o $@ $(FLAGS)

clean:
	$(RM) wardriving wardrivingZsun
