# Host-side C unit tests (no Pebble SDK / no device required).
# The watchapp itself is built with `pebble build` (waf), not this Makefile.

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -g -Itest/c/unity -Isrc/c
BUILDDIR := build-test
TESTBIN  := $(BUILDDIR)/test_logic

.PHONY: test-c clean-test

test-c: $(TESTBIN)
	./$(TESTBIN)

$(TESTBIN): src/c/logic.c test/c/test_logic.c test/c/unity/unity.c
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $^ -o $@

clean-test:
	rm -rf $(BUILDDIR)
