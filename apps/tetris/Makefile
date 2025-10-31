# Makefile for Tetris Game (Compiles BOTH versions)

# Compiler: g++ for C++
CXX = g++

# Compiler flags
CFLAGS = -g -Wall

# --- Target 1: Single Player ---
TARGET_1 = ../../bin/tetris
SRC_1 = tetris.cpp

# --- Target 2: Two Player ---
TARGET_2 = ../../bin/tetrisX2
SRC_2 = tetrisX2.cpp

# Assets: All your sound files
ASSETS = pop.wav pop2.wav

# 'all' will build both games and copy assets
all: $(TARGET_1) $(TARGET_2) copy_assets

$(TARGET_1): $(SRC_1)
	@mkdir -p ../../bin
	$(CXX) $(CFLAGS) -o $(TARGET_1) $(SRC_1)
	@echo "Tetris (single-player) compiled successfully!"

$(TARGET_2): $(SRC_2)
	@mkdir -p ../../bin
	$(CXX) $(CFLAGS) -o $(TARGET_2) $(SRC_2)
	@echo "Tetris (2-player) compiled successfully!"

copy_assets:
	@echo "Copying assets..."
	cp $(ASSETS) ../../bin/

clean:
	rm -f $(TARGET_1)
	rm -f $(TARGET_2)
	rm -f ../../bin/pop.wav
	rm -f ../../bin/pop2.wav