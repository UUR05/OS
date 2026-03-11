# Makefile для libcaesar

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS = -shared

LIBRARY_NAME = libcaesar.so
LIBRARY_SOURCE = caesar.c
LIBRARY_OBJECT = caesar.o

INSTALL_PATH = /usr/local/lib

TEST_PROGRAM = test.py
TEST_INPUT = input.txt
TEST_OUTPUT = output.bin

PROGRAM = secure_copy
PROGRAM_SRC = secure_copy.c

.PHONY: all install test clean

all: $(LIBRARY_NAME) $(PROGRAM)

$(LIBRARY_NAME): $(LIBRARY_OBJECT)
	$(CC) $(LDFLAGS) -o $(LIBRARY_NAME) $(LIBRARY_OBJECT)

$(LIBRARY_OBJECT): $(LIBRARY_SOURCE)
	$(CC) $(CFLAGS) -c $(LIBRARY_SOURCE) -o $(LIBRARY_OBJECT)

install: $(LIBRARY_NAME)
	sudo cp $(LIBRARY_NAME) $(INSTALL_PATH)/
	sudo ldconfig

test: $(LIBRARY_NAME) $(TEST_INPUT)
	python3 $(TEST_PROGRAM) ./$(LIBRARY_NAME) 42 $(TEST_INPUT) $(TEST_OUTPUT)

$(PROGRAM): $(PROGRAM_SRC) $(LIBRARY_NAME)
	$(CC) -pthread -Wall -o $(PROGRAM) $(PROGRAM_SRC) -L. -lcaesar

clean:
	rm -f $(LIBRARY_OBJECT) $(LIBRARY_NAME) $(PROGRAM)
