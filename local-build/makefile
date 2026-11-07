# Local macOS/Linux build. Replaces the UTK-only paths in ../makefile
# (/home/jplank/cs360/...) with a vendored copy of libfdr.
# libfdr source: https://web.eecs.utk.edu/~jplank/plank/classes/cs360/360/notes/Libfdr/
#
#   cd local-build && make        # builds ./chat_server
#   ./chat_server 8005 Football Bridge Politics

CC      = cc
FDR     = libfdr
# -Wno-error=int-conversion: sockettome.c does `return NULL` from an int
# function. Old gcc warned; clang 15+ errors. Kept as a warning, source untouched.
CFLAGS  = -g -Wall -Wno-error=int-conversion -I$(FDR)/include -I..
LDLIBS  = -lpthread

FDR_OBJS = $(FDR)/src/jval.o $(FDR)/src/dllist.o $(FDR)/src/jrb.o $(FDR)/src/fields.o

all: chat_server

$(FDR)/libfdr.a: $(FDR_OBJS)
	ar rcs $@ $^

chat_server.o: ../chat_server.c
	$(CC) $(CFLAGS) -c $< -o $@

sockettome.o: ../sockettome.c
	$(CC) $(CFLAGS) -c $< -o $@

chat_server: chat_server.o sockettome.o $(FDR)/libfdr.a
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f chat_server *.o $(FDR_OBJS) $(FDR)/libfdr.a

.PHONY: all clean
