CC=cc
CFLAGS=-O3 -Wall -Wextra -Wno-unknown-pragmas -Wcast-qual
LIBS=-lpthread -lz

# Build without zopfli (-DNOZOPFLI) to avoid vendoring the zopfli subtree.
# TWRP only uses parallel gzip compression, not the -Z zopfli mode.
pigz: pigz.o yarn.o try.o
	$(CC) -o pigz pigz.o yarn.o try.o $(LIBS)
	ln -f pigz unpigz

pigz.o: pigz.c yarn.h try.h
	$(CC) $(CFLAGS) -DNOZOPFLI -c -o pigz.o pigz.c

yarn.o: yarn.c yarn.h
	$(CC) $(CFLAGS) -c -o yarn.o yarn.c

try.o: try.c try.h
	$(CC) $(CFLAGS) -c -o try.o try.c

test: pigz
	./pigz -kf pigz.c ; ./pigz -t pigz.c.gz
	@rm -f pigz.c.gz

clean:
	@rm -f *.o pigz unpigz pigz.c.gz pigz.c.zz pigz.c.zip
