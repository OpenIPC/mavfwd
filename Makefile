CFLAGS=-O1 -g -fsanitize=address -fno-omit-frame-pointer -Wall -Wno-address-of-packed-member
LDFLAGS=-g -fsanitize=address
LDLIBS=-levent_core

mavfwd:
