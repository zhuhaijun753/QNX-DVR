recorder: main.o avb.o process.o save.o
	QCC -Vgcc_ntoarmv7le -Wall -o recorder main.o avb.o process.o save.o -lpps -lc
	cp recorder ../bin/

main.o: main.c
	QCC -Vgcc_ntoarmv7le -Wall -c main.c
avb.o: avb.c
	QCC -Vgcc_ntoarmv7le -Wall -c avb.c
process.o: process.c
	QCC -Vgcc_ntoarmv7le -Wall -c process.c
save.o: save.c
	QCC -Vgcc_ntoarmv7le -Wall -c save.c

clean:
	rm recorder *.o
