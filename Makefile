CC ?= cc
CPPFLAGS += -Iinclude
CFLAGS ?= -O2
CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -Werror

OBJ = src/main.o src/sdwire3.o

all: csdwire3

csdwire3: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/main.o: src/main.c include/sdwire3.h
src/sdwire3.o: src/sdwire3.c include/sdwire3.h

clean:
	rm -f $(OBJ) csdwire3

.PHONY: all clean
