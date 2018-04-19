FLAGS =  -g -Wall -std=gnu99

all: mancsrv

mancsrv: mancsrv.o
	gcc ${FLAGS} -o $@ $^

%.o: %.c
	gcc ${FLAGS} -c $<

clean:
	rm -f *.o mancsrv

