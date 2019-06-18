CC = gcc
CFLAGS = -g -Wall
CCLINK = $(CC)
OBJS = main.o
RM = rm -f
# Creating the  executable
ttftps: $(OBJS)
	$(CCLINK) $(CFLAGS) -o ttftps $(OBJS)
# Creating the object files
main.o: main.c
# Cleaning old files before new make
clean:
	$(RM) $(TARGET) *.o *~ "#"* core.*