CC ?= cc
CFLAGS ?= -O2

# Kept out of CFLAGS so that overriding CFLAGS does not drop them.
SDW_CPPFLAGS = -Iinclude
SDW_CFLAGS = -std=c99 -Wall -Wextra -Wpedantic

OBJ = src/main.o src/sdwire3.o
TESTS = tests/test_sdwire3

all: csdwire3

csdwire3: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

.c.o:
	$(CC) $(SDW_CPPFLAGS) $(CPPFLAGS) $(SDW_CFLAGS) $(CFLAGS) -c -o $@ $<

src/main.o: src/main.c include/sdwire3.h
src/sdwire3.o: src/sdwire3.c include/sdwire3.h

tests/test_sdwire3: tests/test_sdwire3.c src/sdwire3.c include/sdwire3.h
	$(CC) $(SDW_CPPFLAGS) $(CPPFLAGS) $(SDW_CFLAGS) $(CFLAGS) $(LDFLAGS) \
	    -o $@ tests/test_sdwire3.c $(LDLIBS)

check: csdwire3 $(TESTS)
	./tests/test_sdwire3
	sh tests/cli.sh ./csdwire3

clean:
	rm -f $(OBJ) csdwire3 $(TESTS)

.PHONY: all check clean
