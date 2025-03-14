# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -Wall -g -O2

# Linker flags
LDFLAGS = -lnvidia-ml

# Source files
SRCS = nvidia-powermizer.cpp

# Object files
OBJS = $(SRCS:.cpp=.o)

# Executable name
TARGET = nvidia-powermizer

# Default target
all: nvidia-powermizer

# Link the executable
nvidia-powermizer: nvidia-powermizer.cpp
	$(CXX) $(CXXFLAGS) -o nvidia-powermizer nvidia-powermizer.cpp $(LDFLAGS)

# Clean target
clean:
	rm -f nvidia-powermizer

.PHONY: all clean