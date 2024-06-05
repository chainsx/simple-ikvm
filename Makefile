CC = g++
CFLAGS = -std=c++17 -Wall -Werror
LDFLAGS = -lvncserver

SRCS = ikvm_args.cpp ikvm_input.cpp ikvm_manager.cpp ikvm_server.cpp ikvm_video.cpp simple-ikvm.cpp
OBJS = $(SRCS:.cpp=.o)
EXEC = simple-ikvm

.PHONY: all clean

all: $(EXEC)

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(EXEC)

