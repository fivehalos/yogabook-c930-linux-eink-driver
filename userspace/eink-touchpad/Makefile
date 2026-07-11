# SPDX-License-Identifier: GPL-2.0-only
#
# YogaBook E-Ink touchpad trial — hidraw touch in, uinput pointer/keys out.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS += -I. -I../common

SRCS := main.c hid_touch.c touch_parse.c touch_coord.c uinput_out.c gestures.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean

all: eink-touchpad

eink-touchpad: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f eink-touchpad $(OBJS)
