CC := gcc
RM := rm -rf
TARGET := tscast

CFLAGS = -pipe -D_LARGEFILE64_SOURCE -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -O2 -Wall

SRCS = utils.c rtp.c tccore.c tscast.c
OBJS = $(patsubst %.c,%.o,$(SRCS))
LD_LIBS = -lpthread

all: $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LD_LIBS)

.PHONY: clean
clean:
	$(RM) *.o $(TARGET)

%.o: %.c
	@echo '<$(CC)> compiling object file "$@" ...'
	$(Q_) $(CC) $(CFLAGS) -c $< -o $@
