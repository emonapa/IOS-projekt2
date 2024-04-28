CC = gcc
CFLAGS = -std=gnu99 -Wall -Wextra -Werror -pedantic
LDFLAGS = -Wall -Wextra 
TARGETS = proj2

.PHONY: all run clean

# Default target
all: $(TARGETS)

build: $(TARGETS)

run: $(TARGETS)
	./proj2 8 4 10 4 5

# Remove objects and executable files
clean:
	rm -f $(TARGETS) *.o

# Rules for compiling source files
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<
    
proj2: proj2.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

