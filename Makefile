
CFLAGS=-ggdb -std=c99 -Wall -Wextra

all: pipexec ptee ptest

pipexec: pipexec.c
	${CC} ${CFLAGS} pipexec.c -o pipexec

ptee: ptee.c
	${CC} ${CFLAGS} ptee.c -o ptee

ptest: ptest.c
	${CC} ${CFLAGS} ptest.c -o ptest

.PHONY: clean
clean:
	rm -f pipexec ptee ptest core

