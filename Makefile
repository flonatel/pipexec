
pipexec: pipexec.c
	${CC} -ggdb -std=c99 -Wall -Wextra pipexec.c -o pipexec

.PHONY: clean
clean:
	rm -f pipexec core

