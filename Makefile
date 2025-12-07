# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g

# Source files
SRC = memory_allocation.c
HDR = memory_allocation.h

# Targets
all: basic_test mmap_test stress_test

basic_test: test_basic.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) test_basic.c $(SRC) -o p1

mmap_test: test_mmap.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) test_mmap.c $(SRC) -o p2

stress_test: test_stress.c $(SRC) $(HDR)
	$(CC) $(CFLAGS) test_stress.c $(SRC) -o p3

tests: basic_test mmap_test stress_test

clean:
	rm -f p1 p2 p3

.PHONY: all basic_test mmap_test stress_test clean
