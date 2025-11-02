# Makefile for Chat Client

# Compiler
CC = gcc

# Compiler flags
# -g: Add debug info
# -Wall: Turn on all warnings
# -pthread: Link the POSIX Threads library
CFLAGS = -g -Wall -pthread

# The target executable
TARGET = ../../bin/chat_client

# The source file
SRC = chat_client.c

# Default target
all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../../bin
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)
	@echo "Chat client compiled successfully!"

# Clean up build files
clean:
	rm -f $(TARGET)
	rm -f *.o