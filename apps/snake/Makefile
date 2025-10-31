# Makefile for Snake Game

# Compiler: g++ for C++
CXX = g++

# Compiler flags
CFLAGS = -g -Wall

# Target: The executable in our main bin/ directory
TARGET = ../../bin/snake

# Source: Your C++ file
SRC = theSnakeGame.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p ../../bin
	$(CXX) $(CFLAGS) -o $(TARGET) $(SRC)
	@echo "Snake game compiled successfully!"

clean:
	rm -f $(TARGET)