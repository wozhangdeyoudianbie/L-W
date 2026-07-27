CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -Iinclude
LDFLAGS = -pthread

TARGET = build/server
SRC = main/server.cpp \
      src/logger.cpp \
      src/buffer.cpp \
      src/codec.cpp \
      src/event_loop.cpp \
      src/event_loop_thread.cpp \
      src/event_loop_thread_pool.cpp \
      src/connection.cpp \
      src/tcp_server.cpp

BUFFER_TEST_TARGET = build/buffer_test
BUFFER_TEST_SRC = tests/buffer_test.cpp \
                  src/buffer.cpp

EVENT_LOOP_TEST_TARGET = build/event_loop_test
EVENT_LOOP_TEST_SRC = tests/event_loop_test.cpp \
                      src/event_loop.cpp

EVENT_LOOP_THREAD_TEST_TARGET = build/event_loop_thread_test
EVENT_LOOP_THREAD_TEST_SRC = tests/event_loop_thread_test.cpp \
                             src/event_loop.cpp \
                             src/event_loop_thread.cpp

EVENT_LOOP_THREAD_POOL_TEST_TARGET = build/event_loop_thread_pool_test
EVENT_LOOP_THREAD_POOL_TEST_SRC = tests/event_loop_thread_pool_test.cpp \
                                  src/event_loop.cpp \
                                  src/event_loop_thread.cpp \
                                  src/event_loop_thread_pool.cpp

TCP_SERVER_TEST_TARGET = build/tcp_server_test
TCP_SERVER_TEST_SRC = tests/tcp_server_test.cpp \
                      src/logger.cpp \
                      src/buffer.cpp \
                      src/event_loop.cpp \
                      src/event_loop_thread.cpp \
                      src/event_loop_thread_pool.cpp \
                      src/connection.cpp \
                      src/tcp_server.cpp

CODEC_TEST_TARGET = build/codec_test
CODEC_TEST_SRC = tests/codec_test.cpp \
                 src/buffer.cpp \
                 src/codec.cpp

MESSAGE_CALLBACK_TEST_TARGET = build/message_callback_test
MESSAGE_CALLBACK_TEST_SRC = tests/message_callback_test.cpp \
                            src/logger.cpp \
                            src/buffer.cpp \
                            src/codec.cpp \
                            src/event_loop.cpp \
                            src/event_loop_thread.cpp \
                            src/event_loop_thread_pool.cpp \
                            src/connection.cpp \
                            src/tcp_server.cpp

SLOW_ECHO_CLIENT_TARGET = build/slow_echo_client
SLOW_ECHO_CLIENT_SRC = tests/slow_echo_client.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p build
	$(CXX) $(SRC) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET)

$(BUFFER_TEST_TARGET): $(BUFFER_TEST_SRC)
	mkdir -p build
	$(CXX) $(BUFFER_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(BUFFER_TEST_TARGET)

$(EVENT_LOOP_TEST_TARGET): $(EVENT_LOOP_TEST_SRC)
	mkdir -p build
	$(CXX) $(EVENT_LOOP_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(EVENT_LOOP_TEST_TARGET)

$(EVENT_LOOP_THREAD_TEST_TARGET): $(EVENT_LOOP_THREAD_TEST_SRC)
	mkdir -p build
	$(CXX) $(EVENT_LOOP_THREAD_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(EVENT_LOOP_THREAD_TEST_TARGET)

$(EVENT_LOOP_THREAD_POOL_TEST_TARGET): $(EVENT_LOOP_THREAD_POOL_TEST_SRC)
	mkdir -p build
	$(CXX) $(EVENT_LOOP_THREAD_POOL_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(EVENT_LOOP_THREAD_POOL_TEST_TARGET)

$(TCP_SERVER_TEST_TARGET): $(TCP_SERVER_TEST_SRC)
	mkdir -p build
	$(CXX) $(TCP_SERVER_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(TCP_SERVER_TEST_TARGET)

$(CODEC_TEST_TARGET): $(CODEC_TEST_SRC)
	mkdir -p build
	$(CXX) $(CODEC_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(CODEC_TEST_TARGET)

$(MESSAGE_CALLBACK_TEST_TARGET): $(MESSAGE_CALLBACK_TEST_SRC)
	mkdir -p build
	$(CXX) $(MESSAGE_CALLBACK_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(MESSAGE_CALLBACK_TEST_TARGET)

$(SLOW_ECHO_CLIENT_TARGET): $(SLOW_ECHO_CLIENT_SRC)
	mkdir -p build
	$(CXX) $(SLOW_ECHO_CLIENT_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(SLOW_ECHO_CLIENT_TARGET)

run: all
	./$(TARGET)

test-buffer: $(BUFFER_TEST_TARGET)
	./$(BUFFER_TEST_TARGET)

test-event-loop: $(EVENT_LOOP_TEST_TARGET)
	./$(EVENT_LOOP_TEST_TARGET)

test-event-loop-thread: $(EVENT_LOOP_THREAD_TEST_TARGET)
	./$(EVENT_LOOP_THREAD_TEST_TARGET)

test-event-loop-thread-pool: $(EVENT_LOOP_THREAD_POOL_TEST_TARGET)
	./$(EVENT_LOOP_THREAD_POOL_TEST_TARGET)

test-tcp-server: $(TCP_SERVER_TEST_TARGET)
	./$(TCP_SERVER_TEST_TARGET)

test-codec: $(CODEC_TEST_TARGET)
	./$(CODEC_TEST_TARGET)

test-message-callback: $(MESSAGE_CALLBACK_TEST_TARGET)
	./$(MESSAGE_CALLBACK_TEST_TARGET)

test-all: test-buffer \
          test-event-loop \
          test-event-loop-thread \
          test-event-loop-thread-pool \
          test-tcp-server \
          test-codec \
          test-message-callback

slow-echo-client: $(SLOW_ECHO_CLIENT_TARGET)

clean:
	rm -rf build

.PHONY: all \
        run \
        test-buffer \
        test-event-loop \
        test-event-loop-thread \
        test-event-loop-thread-pool \
        test-tcp-server \
        test-codec \
        test-message-callback \
        test-all \
        slow-echo-client \
        clean
