CFLAGS=-march=native -O2 -g -Wall -Wextra
LDFLAGS=-lrt -ljack

all: record

record: record.o file.o

clean: 
	rm -f *~ *.o record *.stamps core *.mp3
