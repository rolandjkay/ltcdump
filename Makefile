all:	ltcdump 


ltcdump: ltcdump.c
	gcc -ggdb ltcdump.c -Wall -Wno-multichar -Wno-format-truncation wav.c -o ltcdump -I. -Ilibltc/src -lm

clean:	
	rm -f ltcdump
