# Builds and runs the library tests on the laptop. Needs a C/C++ compiler and python3.

LIB := ../lib/kinjo/src
VECTORS := $(wildcard ../../protocol/test-vectors/*.json)
CXX ?= g++
CC ?= cc
SAN := -fsanitize=address,undefined -fno-sanitize-recover=all
FLAGS := -O1 -g -Wall -Wextra $(SAN) -I$(LIB)

LIB_C_OBJ := $(patsubst $(LIB)/%.c,build/%.o,$(wildcard $(LIB)/*.c))
LIB_CPP := $(wildcard $(LIB)/*.cpp)

test: build/test_kinjo
	./build/test_kinjo

build/vectors.inc: gen_vectors_inc.py $(VECTORS)
	@mkdir -p build
	python3 gen_vectors_inc.py $@

build/%.o: $(LIB)/%.c $(LIB)/%.h
	@mkdir -p build
	$(CC) $(FLAGS) -c $< -o $@

build/test_kinjo: test_kinjo.cpp build/vectors.inc $(LIB_CPP) $(LIB)/kinjo.h $(LIB_C_OBJ)
	$(CXX) -std=c++11 $(FLAGS) -Ibuild test_kinjo.cpp $(LIB_CPP) $(LIB_C_OBJ) -o $@

clean:
	rm -rf build

.PHONY: test clean
