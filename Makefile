all:	ltcdump trim_wav


ltcdump: ltcdump.c
	gcc -ggdb -O3  ltcdump.c -Wall -Wno-multichar -Wno-format-truncation wav.c -o ltcdump -I. -lm

trim_wav: trim_wav.c
	gcc -ggdb -O3  trim_wav.c -Wall -Wno-multichar -Wno-format-truncation wav.c -o trim_wav -I. -lm

clean:	
	rm -f ltcdump trim_wav
