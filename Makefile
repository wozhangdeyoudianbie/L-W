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
      src/tcp_server.cpp \
      src/protocol.cpp \
      src/room.cpp \
      src/room_manager.cpp \
	  src/room_service.cpp \
	  src/room_state_machine.cpp \
  	  src/game_state.cpp \
	  src/tick_timer.cpp

ROOM_STATE_MACHINE_TEST_TARGET = build/room_state_machine_test
ROOM_STATE_MACHINE_TEST_SRC = tests/room_state_machine_test.cpp \
                              src/room_state_machine.cpp

GAME_STATE_TEST_TARGET = build/game_state_test
GAME_STATE_TEST_SRC = tests/game_state_test.cpp \
                      src/game_state.cpp

TICK_TEST_TARGET = build/tick_test
TICK_TEST_SRC = tests/tick_test.cpp \
                src/logger.cpp \
                src/buffer.cpp \
                src/event_loop.cpp \
                src/connection.cpp \
                src/protocol.cpp \
                src/room_state_machine.cpp \
                src/game_state.cpp \
                src/room.cpp \
                src/room_manager.cpp \
                src/tick_timer.cpp

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

PROTOCOL_TEST_TARGET = build/protocol_test
PROTOCOL_TEST_SRC = tests/protocol_test.cpp \
                    src/protocol.cpp

PROTOCOL_ENCODE_TEST_TARGET = build/protocol_encode_test
PROTOCOL_ENCODE_TEST_SRC = tests/protocol_encode_test.cpp \
                           src/protocol.cpp

ROOM_TEST_TARGET = build/room_test
ROOM_TEST_SRC = tests/room_test.cpp \
                src/logger.cpp \
                src/buffer.cpp \
                src/event_loop.cpp \
                src/connection.cpp \
                src/protocol.cpp \
                src/room_state_machine.cpp \
                src/game_state.cpp \
                src/room.cpp

ROOM_MANAGER_TEST_TARGET = build/room_manager_test
ROOM_MANAGER_TEST_SRC = tests/room_manager_test.cpp \
                        src/logger.cpp \
                        src/buffer.cpp \
                        src/event_loop.cpp \
                        src/connection.cpp \
                        src/protocol.cpp \
                        src/room_state_machine.cpp \
                        src/game_state.cpp \
                        src/room.cpp \
                        src/room_manager.cpp

ROOM_SERVICE_TEST_TARGET = build/room_service_test
ROOM_SERVICE_TEST_SRC = tests/room_service_test.cpp \
                        src/logger.cpp \
                        src/buffer.cpp \
                        src/codec.cpp \
                        src/event_loop.cpp \
                        src/connection.cpp \
                        src/protocol.cpp \
                        src/room_state_machine.cpp \
                        src/game_state.cpp \
                        src/room.cpp \
                        src/room_manager.cpp \
                        src/room_service.cpp

SLOW_ECHO_CLIENT_TARGET = build/slow_echo_client
SLOW_ECHO_CLIENT_SRC = tests/slow_echo_client.cpp

HEARTBEAT_TIMEOUT_CLIENT_TARGET = build/heartbeat_timeout_client
HEARTBEAT_TIMEOUT_CLIENT_SRC = tests/heartbeat_timeout_client.cpp \
                               src/buffer.cpp \
                               src/codec.cpp

TCP_SERVER_CLOSED_CALLBACK_TEST_TARGET = build/tcp_server_closed_callback_test
TCP_SERVER_CLOSED_CALLBACK_TEST_SRC = tests/tcp_server_closed_callback_test.cpp \
                                      src/logger.cpp \
                                      src/buffer.cpp \
                                      src/event_loop.cpp \
                                      src/event_loop_thread.cpp \
                                      src/event_loop_thread_pool.cpp \
                                      src/connection.cpp \
                                      src/tcp_server.cpp

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

$(PROTOCOL_TEST_TARGET): $(PROTOCOL_TEST_SRC)
	mkdir -p build
	$(CXX) $(PROTOCOL_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(PROTOCOL_TEST_TARGET)

$(PROTOCOL_ENCODE_TEST_TARGET): $(PROTOCOL_ENCODE_TEST_SRC)
	mkdir -p build
	$(CXX) $(PROTOCOL_ENCODE_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(PROTOCOL_ENCODE_TEST_TARGET)

$(ROOM_TEST_TARGET): $(ROOM_TEST_SRC)
	mkdir -p build
	$(CXX) $(ROOM_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(ROOM_TEST_TARGET)

$(ROOM_MANAGER_TEST_TARGET): $(ROOM_MANAGER_TEST_SRC)
	mkdir -p build
	$(CXX) $(ROOM_MANAGER_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(ROOM_MANAGER_TEST_TARGET)

$(SLOW_ECHO_CLIENT_TARGET): $(SLOW_ECHO_CLIENT_SRC)
	mkdir -p build
	$(CXX) $(SLOW_ECHO_CLIENT_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(SLOW_ECHO_CLIENT_TARGET)

$(HEARTBEAT_TIMEOUT_CLIENT_TARGET): $(HEARTBEAT_TIMEOUT_CLIENT_SRC)
	mkdir -p build
	$(CXX) $(HEARTBEAT_TIMEOUT_CLIENT_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(HEARTBEAT_TIMEOUT_CLIENT_TARGET)

$(TCP_SERVER_CLOSED_CALLBACK_TEST_TARGET): $(TCP_SERVER_CLOSED_CALLBACK_TEST_SRC)
	mkdir -p build
	$(CXX) $(TCP_SERVER_CLOSED_CALLBACK_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(TCP_SERVER_CLOSED_CALLBACK_TEST_TARGET)

$(ROOM_SERVICE_TEST_TARGET): $(ROOM_SERVICE_TEST_SRC)
	mkdir -p build
	$(CXX) $(ROOM_SERVICE_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(ROOM_SERVICE_TEST_TARGET)

$(ROOM_STATE_MACHINE_TEST_TARGET): $(ROOM_STATE_MACHINE_TEST_SRC)
	mkdir -p build
	$(CXX) $(ROOM_STATE_MACHINE_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(ROOM_STATE_MACHINE_TEST_TARGET)

$(GAME_STATE_TEST_TARGET): $(GAME_STATE_TEST_SRC)
	mkdir -p build
	$(CXX) $(GAME_STATE_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(GAME_STATE_TEST_TARGET)

$(TICK_TEST_TARGET): $(TICK_TEST_SRC)
	mkdir -p build
	$(CXX) $(TICK_TEST_SRC) $(CXXFLAGS) $(LDFLAGS) -o $(TICK_TEST_TARGET)

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

test-protocol: $(PROTOCOL_TEST_TARGET)
	./$(PROTOCOL_TEST_TARGET)

test-protocol-encode: $(PROTOCOL_ENCODE_TEST_TARGET)
	./$(PROTOCOL_ENCODE_TEST_TARGET)

test-room: $(ROOM_TEST_TARGET)
	./$(ROOM_TEST_TARGET)

test-room-manager: $(ROOM_MANAGER_TEST_TARGET)
	./$(ROOM_MANAGER_TEST_TARGET)

test-tcp-server-closed-callback: $(TCP_SERVER_CLOSED_CALLBACK_TEST_TARGET)
	./$(TCP_SERVER_CLOSED_CALLBACK_TEST_TARGET)

test-room-service: $(ROOM_SERVICE_TEST_TARGET)
	./$(ROOM_SERVICE_TEST_TARGET)
test-room-state-machine: $(ROOM_STATE_MACHINE_TEST_TARGET)
	./$(ROOM_STATE_MACHINE_TEST_TARGET)

test-game-state: $(GAME_STATE_TEST_TARGET)
	./$(GAME_STATE_TEST_TARGET)

test-tick: $(TICK_TEST_TARGET)
	./$(TICK_TEST_TARGET)

test-heartbeat-timeout: $(HEARTBEAT_TIMEOUT_CLIENT_TARGET)
	./$(HEARTBEAT_TIMEOUT_CLIENT_TARGET)

test-all: test-buffer \
          test-event-loop \
          test-event-loop-thread \
          test-event-loop-thread-pool \
          test-tcp-server \
          test-codec \
          test-message-callback \
          test-protocol \
          test-protocol-encode \
          test-room \
          test-room-manager \
		  test-tcp-server-closed-callback \
		  test-room-service \
          test-room-state-machine \
          test-game-state \
          test-tick

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
        test-protocol \
        test-protocol-encode \
        test-room \
        test-room-manager \
        test-all \
        slow-echo-client \
        clean \
		test-tcp-server-closed-callback \
		test-room-service \
		test-room-state-machine \
		test-game-state \
		test-tick \
		test-heartbeat-timeout \
