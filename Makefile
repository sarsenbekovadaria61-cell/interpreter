CC = cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c23 -Wall -Wextra -Wpedantic -O2
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

.PHONY: all debug clean test
all: mysh
mysh: $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)
src/%.o: src/%.c $(wildcard src/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
debug:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c23 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'
tests/test_shell: tests/test_shell.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< -lutil
test: all tests/test_shell
	./tests/test_shell ./mysh
clean:
	rm -f $(OBJ) mysh tests/test_shell
