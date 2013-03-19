CC = i686-pc-linux-gnu-gcc
LD = ${CC}

CFLAGS = -Wall -static -O2
LDFLAGS = ${CFLAGS}
LIBS = -ltomcrypt

SYMBOLS = -DUSE_SYSEXITS

SOURCES = flashgc.c gopt.c gcgen.c
OBJECTS = $(addsuffix .o, $(basename ${SOURCES}))
TARGET_BIN = flashgc_x86

all: ${TARGET_BIN}

%.o: %.c
	${CC} -c ${CFLAGS} ${SYMBOLS} -I /usr/include/ -o $@ $<

${TARGET_BIN}: ${OBJECTS}
	${LD} ${LDFLAGS} -o $@ $(OBJECTS) -L . ${LIBS}

clean:
	rm -f *.o *~ *.out ${TARGET_BIN}
