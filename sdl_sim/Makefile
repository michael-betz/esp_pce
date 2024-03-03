LDLIBS = -lm -lSDL2 -lstdc++

CFLAGS += -g -Wall -I../src -I../src/macplus -DSDL_SIM=1

ALL_SRCS_ = $(wildcard *.c) $(wildcard ../src/*/*.c) $(wildcard ../src/*/*/*.c)
ALL_SRCS = $(filter-out ../src/esp/%,$(ALL_SRCS_))
ALL_OBJS = $(ALL_SRCS:.c=.o)

all: main

main: $(ALL_OBJS)

clean:
	rm -rf $(ALL_OBJS) main *.out *.dat
