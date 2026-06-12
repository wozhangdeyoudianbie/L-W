CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Iinclude
LDFLAGS = -pthread
TARGET = build/server
SRC = main/server.cpp \
      src/logger.cpp \
      src/thread_pool.cpp \
      src/http.cpp \
      src/router.cpp
all: $(TARGET)
$(TARGET): $(SRC)
	mkdir -p build
	$(CXX) $(SRC) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET)
run: all
	./$(TARGET)
clean:
	rm -rf build
