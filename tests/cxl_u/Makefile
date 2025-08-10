CC ?= gcc
OS ?= $(shell uname -s)
CFLAGS ?= -O2 -fno-omit-frame-pointer
LDFLAGS ?=

# If you really want a static binary on Linux, call: make STATIC=1
ifeq ($(OS),Linux)
ifneq ($(STATIC),)
LDFLAGS += -static
endif
endif

all: pf_storm

pf_storm: pf_storm.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f pf_storm


