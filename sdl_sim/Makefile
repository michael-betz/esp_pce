LDLIBS = -lm -lSDL2 -lstdc++

CFLAGS += -Wall -I../src -DSDL_SIM=1

ALL_SRCS_ = $(wildcard ../src/*.c) $(wildcard ../src/*/*.c) $(wildcard ../src/*/*/*.c)
ALL_SRCS = $(filter-out ../src/esp_port.c,$(ALL_SRCS_))
ALL_OBJS = $(ALL_SRCS:.c=.o)

all: main

main: $(ALL_OBJS)

clean:
	rm -rf $(ALL_OBJS) main
