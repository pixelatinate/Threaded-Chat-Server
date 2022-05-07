CC = gcc 

INCLUDES = -I/home/jplank/cs360/include -lpthread

CFLAGS = -g -Wall $(INCLUDES)

LIBDIR = /home/jplank/cs360/objs

LIBS = $(LIBDIR)/libfdr.a 

EXECUTABLES: chat_server

all: $(EXECUTABLES)

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $*.c

chat_server: chat_server.o sockettome.o
	$(CC) $(CFLAGS) -o chat_server chat_server.o sockettome.o $(LIBS)

clean:
	rm core $(EXECUTABLES) *.o