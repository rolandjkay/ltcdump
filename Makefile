all:	ltcdump 


ltcdump: ltcdump.c
	gcc -ggdb ltcdump.c -Wno-multichar -Wno-format-truncation wav.c -o ltcdump -I. -Ilibltc/src -lm
