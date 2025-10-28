# Makefile for Test Programs

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -g
LDFLAGS = -pthread

# Test executables
TESTS = concurrency_test enhanced_concurrency_test full_integration_test

all: $(TESTS)

concurrency_test: concurrency_test.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

enhanced_concurrency_test: enhanced_concurrency_test.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

full_integration_test: full_integration_test.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TESTS) *.o

.PHONY: all clean
