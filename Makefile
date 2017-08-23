#CC=mips-openwrt-linux-gcc
CC=gcc

FLAGS=-lpthread 

RM=rm -f

all: wardriving

wardriving: wardriving.c 
	$(CC) $< -o $@ $(FLAGS)

clean:
	$(RM) wardriving
