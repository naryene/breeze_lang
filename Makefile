CC	=	gcc
CFLAGS	=	-Wall	-Wextra -g
SRCS	=	src/main.c	src/chunk.c	src/compiler.c	src/debug.c	src/memory.c	src/object.c	src/scanner.c	src/table.c	src/value.c	src/virtual_machine.c
OBJS	=	$(SRCS:.c=.o)
TARGET	= target	

all:	$(TARGET)

$(TARGET):	$(OBJS)
				$(CC)	$(CFLAGS)	-o	$@	$^

%.o:	%.c
				$(CC)	$(CFLAGS)	-c	-o	$@	$<

clean:
				rm	-f	$(OBJS)	

