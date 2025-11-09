CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
SRC = src/main.cpp src/shell.cpp src/jobs.cpp
OBJ = $(SRC:.cpp=.o)
TARGET = myshell

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)
