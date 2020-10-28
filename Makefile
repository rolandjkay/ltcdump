all:	ltcdump trim_wav riff_merge


ltcdump: ltcdump.c
	gcc -ggdb -O3  ltcdump.c -Wall -Wno-multichar -Wno-format-truncation wav.c -o ltcdump -I. -lm

trim_wav: trim_wav.c
	gcc -ggdb -O3  trim_wav.c -Wall -Wno-multichar -Wno-format-truncation wav.c -o trim_wav -I. -lm

riff_merge: riff_merge.c
	gcc -ggdb -O3  riff_merge.c -Wall -Wno-multichar -o riff_merge -I. 

clean:	
	rm -f ltcdump trim_wav riff_merge
