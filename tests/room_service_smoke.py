#!/usr/bin/env python3

import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

HOST = "127.0.0.1"
PORT = 8080
ROOM_ID = 1

JOIN = 1
LEAVE = 2
CHAT = 3

JOIN_OK = 101
PLAYER_JOINED = 102
LEAVE_OK = 103
PLAYER_LEFT = 104
CHAT_EVENT = 105
ERROR = 107

NOT_IN_ROOM = 4


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def make_frame(message_type, payload):
    frame_length = 2 + len(payload)
    return struct.pack("!IH", frame_length, message_type) + payload


def make_join_payload(room_id, player_name):
    name = player_name.encode("utf-8")
    return struct.pack("!IH", room_id, len(name)) + name


def make_chat_payload(message):
    data = message.encode("utf-8")
    return struct.pack("!H", len(data)) + data


def recv_exact(sock, size):
    data = bytearray()

    while len(data) < size:
        block = sock.recv(size - len(data))
        if not block:
            raise ConnectionError("服务器提前关闭连接")
        data.extend(block)

    return bytes(data)


def recv_frame(sock):
    header = recv_exact(sock, 4)
    frame_length = struct.unpack("!I", header)[0]

    check(frame_length >= 2, "收到非法 frame_length")

    body = recv_exact(sock, frame_length)
    message_type = struct.unpack("!H", body[:2])[0]
    payload = body[2:]

    return message_type, payload


def parse_join_ok(payload):
    check(len(payload) >= 14, "JOIN_OK 长度不足")

    room_id, self_player_id, member_count = struct.unpack_from(
        "!IQH",
        payload,
        0)

    offset = 14
    members = []

    for _ in range(member_count):
        check(
            len(payload) - offset >= 10,
            "JOIN_OK 成员头部不完整")

        player_id, name_size = struct.unpack_from(
            "!QH",
            payload,
            offset)

        offset += 10

        check(
            len(payload) - offset >= name_size,
            "JOIN_OK 成员名字不完整")

        player_name = payload[
            offset:offset + name_size
        ].decode("utf-8")

        offset += name_size
        members.append((player_id, player_name))

    check(offset == len(payload), "JOIN_OK 存在多余字节")

    return room_id, self_player_id, members


def parse_player_joined(payload):
    check(len(payload) >= 14, "PLAYER_JOINED 长度不足")

    room_id, player_id, name_size = struct.unpack_from(
        "!IQH",
        payload,
        0)

    check(
        len(payload) == 14 + name_size,
        "PLAYER_JOINED 名字长度不匹配")

    player_name = payload[14:].decode("utf-8")

    return room_id, player_id, player_name


def parse_leave_ok(payload):
    check(len(payload) == 4, "LEAVE_OK 长度错误")
    return struct.unpack("!I", payload)[0]


def parse_player_left(payload):
    check(len(payload) == 12, "PLAYER_LEFT 长度错误")
    return struct.unpack("!IQ", payload)


def parse_chat_event(payload):
    check(len(payload) >= 14, "CHAT_EVENT 长度不足")

    room_id, player_id, message_size = struct.unpack_from(
        "!IQH",
        payload,
        0)

    check(
        len(payload) == 14 + message_size,
        "CHAT_EVENT 消息长度不匹配")

    message = payload[14:].decode("utf-8")

    return room_id, player_id, message


def parse_error(payload):
    check(len(payload) == 4, "ERROR 长度错误")
    return struct.unpack("!HH", payload)


class Client:
    def __init__(self, name):
        self.name = name
        self.sock = socket.create_connection(
            (HOST, PORT),
            timeout=3.0)
        self.sock.settimeout(3.0)
        self.closed = False

    def send(self, message_type, payload):
        self.sock.sendall(
            make_frame(message_type, payload))

    def send_raw(self, data):
        self.sock.sendall(data)

    def send_fragmented(self, message_type, payload):
        frame = make_frame(message_type, payload)

        self.sock.sendall(frame[:3])
        time.sleep(0.05)
        self.sock.sendall(frame[3:])

    def expect(self, expected_type):
        actual_type, payload = recv_frame(self.sock)

        check(
            actual_type == expected_type,
            f"{self.name} 期望消息类型 {expected_type}，"
            f"实际收到 {actual_type}")

        return payload

    def close(self):
        if not self.closed:
            self.closed = True
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.sock.close()


def wait_for_server(process):
    deadline = time.monotonic() + 5.0

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"服务器启动失败，退出码 = {process.returncode}")

        try:
            probe = socket.create_connection(
                (HOST, PORT),
                timeout=0.2)
            probe.close()
            return
        except OSError:
            time.sleep(0.05)

    raise TimeoutError("等待服务器监听 8080 端口超时")


def run_scenario(clients):
    alice = Client("Alice")
    clients.append(alice)

    alice.send(
        JOIN,
        make_join_payload(ROOM_ID, "Alice"))

    room_id, alice_id, members = parse_join_ok(
        alice.expect(JOIN_OK))

    check(room_id == ROOM_ID, "Alice JOIN_OK room_id 错误")
    check(alice_id == 1, "Alice player_id 应为 1")
    check(members == [], "Alice 首次加入时成员列表应为空")

    print("[PASS] Alice 正常加入空房间")

    bob = Client("Bob")
    clients.append(bob)

    bob.send_fragmented(
        JOIN,
        make_join_payload(ROOM_ID, "Bob"))

    room_id, bob_id, members = parse_join_ok(
        bob.expect(JOIN_OK))

    check(room_id == ROOM_ID, "Bob JOIN_OK room_id 错误")
    check(bob_id == 2, "Bob player_id 应为 2")
    check(
        members == [(1, "Alice")],
        "Bob 应看到 Alice")

    room_id, player_id, player_name = parse_player_joined(
        alice.expect(PLAYER_JOINED))

    check(
        (room_id, player_id, player_name) ==
        (ROOM_ID, 2, "Bob"),
        "Alice 收到的 PLAYER_JOINED 错误")

    print("[PASS] Bob 分片 JOIN 与跨连接 PLAYER_JOINED")

    first_chat = make_frame(
        CHAT,
        make_chat_payload("first"))

    second_chat = make_frame(
        CHAT,
        make_chat_payload("second"))

    alice.send_raw(first_chat + second_chat)

    for client in (alice, bob):
        first_event = parse_chat_event(
            client.expect(CHAT_EVENT))

        second_event = parse_chat_event(
            client.expect(CHAT_EVENT))

        check(
            first_event == (ROOM_ID, 1, "first"),
            f"{client.name} 第一条 CHAT_EVENT 错误")

        check(
            second_event == (ROOM_ID, 1, "second"),
            f"{client.name} 第二条 CHAT_EVENT 错误")

    print("[PASS] 合并多帧 CHAT 保序广播")

    bob.send(LEAVE, b"")

    check(
        parse_leave_ok(bob.expect(LEAVE_OK)) == ROOM_ID,
        "Bob LEAVE_OK 错误")

    check(
        parse_player_left(alice.expect(PLAYER_LEFT)) ==
        (ROOM_ID, 2),
        "Alice 收到的 Bob PLAYER_LEFT 错误")

    bob.send(
        CHAT,
        make_chat_payload("should fail"))

    request_type, error_code = parse_error(
        bob.expect(ERROR))

    check(
        (request_type, error_code) ==
        (CHAT, NOT_IN_ROOM),
        "Bob 离开后 CHAT 应返回 not_in_room")

    print("[PASS] 主动 LEAVE、广播与失败后状态保持")

    bob.send(
        JOIN,
        make_join_payload(ROOM_ID, "Bob"))

    room_id, bob_rejoin_id, members = parse_join_ok(
        bob.expect(JOIN_OK))

    check(room_id == ROOM_ID, "Bob 重入 room_id 错误")
    check(bob_rejoin_id == 3, "Bob 重入 player_id 应为 3")
    check(
        members == [(1, "Alice")],
        "Bob 重入时应只看到 Alice")

    check(
        parse_player_joined(
            alice.expect(PLAYER_JOINED)) ==
        (ROOM_ID, 3, "Bob"),
        "Alice 收到的 Bob 重入事件错误")

    bob.close()

    check(
        parse_player_left(
            alice.expect(PLAYER_LEFT)) ==
        (ROOM_ID, 3),
        "Bob 异常断线后 PLAYER_LEFT 错误")

    print("[PASS] 重入与异常断线清理")

    charlie = Client("Charlie")
    clients.append(charlie)

    charlie.send(
        JOIN,
        make_join_payload(ROOM_ID, "Charlie"))

    room_id, charlie_id, members = parse_join_ok(
        charlie.expect(JOIN_OK))

    check(room_id == ROOM_ID, "Charlie room_id 错误")
    check(charlie_id == 4, "Charlie player_id 应为 4")
    check(
        members == [(1, "Alice")],
        "Bob 断线后不应残留在成员列表")

    check(
        parse_player_joined(
            alice.expect(PLAYER_JOINED)) ==
        (ROOM_ID, 4, "Charlie"),
        "Alice 收到的 Charlie 加入事件错误")

    charlie.close()

    check(
        parse_player_left(
            alice.expect(PLAYER_LEFT)) ==
        (ROOM_ID, 4),
        "Charlie 异常断线事件错误")

    alice.send(LEAVE, b"")

    check(
        parse_leave_ok(alice.expect(LEAVE_OK)) == ROOM_ID,
        "Alice LEAVE_OK 错误")

    print("[PASS] 断线后无幽灵成员并可继续加入")


def main():
    project_root = Path(__file__).resolve().parent.parent

    if len(sys.argv) > 2:
        print(
            "用法：python3 tests/room_service_smoke.py "
            "[server_binary]")
        return 2

    if len(sys.argv) == 2:
        server_path = Path(sys.argv[1]).resolve()
    else:
        server_path = project_root / "build" / "server"

    if not server_path.is_file():
        print(f"[FAIL] 找不到服务器程序：{server_path}")
        return 1

    process = subprocess.Popen(
        [str(server_path)],
        cwd=str(project_root),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True)

    clients = []
    failure = None
    output = ""

    try:
        wait_for_server(process)
        run_scenario(clients)
    except Exception as error:
        failure = error
    finally:
        for client in reversed(clients):
            client.close()

        if process.poll() is not None and failure is None:
            failure = RuntimeError(
                f"服务器提前退出，退出码 = {process.returncode}")

        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=3.0)

        if process.stdout is not None:
            output = process.stdout.read()

    sanitizer_markers = (
        "ERROR: AddressSanitizer",
        "AddressSanitizer:DEADLYSIGNAL",
        "runtime error:",
        "WARNING: ThreadSanitizer",
        "FATAL: ThreadSanitizer"
    )

    sanitizer_failure = any(
        marker in output
        for marker in sanitizer_markers)

    if failure is not None:
        print(f"[FAIL] {failure}")
        if output:
            print("===== server output =====")
            print(output)
        return 1

    if sanitizer_failure:
        print("[FAIL] Sanitizer 报告异常")
        print("===== server output =====")
        print(output)
        return 1

    print("RoomService 真实网络验收通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
