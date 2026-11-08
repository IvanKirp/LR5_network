import argparse
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest

MAX_PAYLOAD_SIZE = 65536
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 20054


def parse_statistics(response):
    result = {}
    for line in response.decode("ascii").splitlines():
        name, value = line.split("=", 1)
        result[name] = int(value)
    return result


def expected_statistics(text):
    data = text.encode("utf-8")
    words = 0
    digits = 0
    latin_letters = 0
    inside_word = False

    for value in data:
        is_latin = (ord("A") <= value <= ord("Z")) or (ord("a") <= value <= ord("z"))
        is_digit = ord("0") <= value <= ord("9")
        is_word_character = is_latin or is_digit
        if is_digit:
            digits += 1
        if is_latin:
            latin_letters += 1
        if is_word_character and not inside_word:
            words += 1
        inside_word = is_word_character

    lines = data.count(b"\n")
    if data and not data.endswith(b"\n"):
        lines += 1

    return {
        "bytes": len(data),
        "lines": lines,
        "words": words,
        "digits": digits,
        "latinLetters": latin_letters,
    }


def receive_exact(connection, length, chunk_size=4096):
    data = bytearray()
    while len(data) < length:
        part = connection.recv(min(chunk_size, length - len(data)))
        if not part:
            raise AssertionError("Соединение закрыто до получения полного кадра")
        data.extend(part)
    return bytes(data)


def receive_frame(connection, chunk_size=4096):
    header = receive_exact(connection, 4, chunk_size)
    payload_length = struct.unpack("!I", header)[0]
    if payload_length > MAX_PAYLOAD_SIZE:
        raise AssertionError(f"Сервер вернул недопустимую длину кадра: {payload_length}")
    return receive_exact(connection, payload_length, chunk_size)


def send_frame_in_parts(connection, payload, header_parts=(1, 3), payload_parts=(1, 7, 31)):
    header = struct.pack("!I", len(payload))
    offset = 0
    for part_length in header_parts:
        connection.sendall(header[offset:offset + part_length])
        offset += part_length
        time.sleep(0.01)
    if offset != len(header):
        raise AssertionError("Некорректное разбиение заголовка теста")

    offset = 0
    for part_length in payload_parts:
        if offset >= len(payload):
            break
        next_offset = min(offset + part_length, len(payload))
        connection.sendall(payload[offset:next_offset])
        offset = next_offset
        time.sleep(0.005)
    if offset < len(payload):
        connection.sendall(payload[offset:])


def find_server_executable():
    root = Path(__file__).resolve().parents[1]
    candidates = [
        root / "bin" / "x64" / "Debug" / "Server.exe",
        root / "bin" / "x64" / "Release" / "Server.exe",
        root / "Server" / "x64" / "Debug" / "Server.exe",
        root / "Server" / "x64" / "Release" / "Server.exe",
        root / "x64" / "Debug" / "Server.exe",
        root / "x64" / "Release" / "Server.exe",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


_parser = argparse.ArgumentParser(add_help=False)
_parser.add_argument("--server", type=Path, default=None)
_parser.add_argument("--host", default=DEFAULT_HOST)
_parser.add_argument("--port", type=int, default=DEFAULT_PORT)
_parser.add_argument("--reuse-server", action="store_true")
_options, _unknown_options = _parser.parse_known_args()
sys.argv = [sys.argv[0]] + _unknown_options


class NetworkIntegrationTests(unittest.TestCase):
    server_process = None
    host = _options.host
    port = _options.port
    server_started_by_test = False

    @classmethod
    def setUpClass(cls):
        if _options.server is None:
            server_path = find_server_executable()
        else:
            server_path = _options.server

        if not _options.reuse_server:
            if server_path is None or not server_path.is_file():
                raise unittest.SkipTest(
                    "Server.exe не найден. Передайте путь через --server или сначала соберите Server."
                )
            cls.server_process = subprocess.Popen(
                [str(server_path), "--host", cls.host, "--port", str(cls.port)],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
            )
            cls.server_started_by_test = True

        deadline = time.monotonic() + 10
        last_error = None
        while time.monotonic() < deadline:
            if cls.server_process is not None and cls.server_process.poll() is not None:
                raise AssertionError(f"Server.exe завершился с кодом {cls.server_process.returncode}")
            try:
                with socket.create_connection((cls.host, cls.port), timeout=0.25):
                    return
            except OSError as error:
                last_error = error
                time.sleep(0.1)
        raise AssertionError(f"Сервер не начал принимать подключения: {last_error}")

    @classmethod
    def tearDownClass(cls):
        if cls.server_process is not None and cls.server_started_by_test:
            cls.server_process.terminate()
            try:
                cls.server_process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                cls.server_process.kill()
                cls.server_process.wait(timeout=3)

    @classmethod
    def request(cls, text, partial=False, partial_receive=False):
        payload = text.encode("utf-8")
        if len(payload) > MAX_PAYLOAD_SIZE:
            raise ValueError("Тестовые данные превышают лимит протокола")
        with socket.create_connection((cls.host, cls.port), timeout=5) as connection:
            connection.settimeout(5)
            if partial:
                send_frame_in_parts(connection, payload)
            else:
                connection.sendall(struct.pack("!I", len(payload)) + payload)
            response = receive_frame(connection, 1 if partial_receive else 4096)
        return parse_statistics(response)

    def test_regular_request(self):
        text = "Hello 123"
        self.assertEqual(self.request(text), expected_statistics(text))

    def test_empty_request(self):
        text = ""
        self.assertEqual(self.request(text), expected_statistics(text))

    def test_spaces_and_lines(self):
        text = "Abc  123 x-y\nZ9"
        self.assertEqual(self.request(text), expected_statistics(text))

    def test_cyrillic_text(self):
        text = "Привет мир 42"
        self.assertEqual(self.request(text), expected_statistics(text))

    def test_different_message_sizes(self):
        for size in (1, 100, 1000, MAX_PAYLOAD_SIZE):
            text = ("A9- " * ((size + 3) // 4)).encode("ascii")[:size].decode("ascii")
            with self.subTest(size=size):
                self.assertEqual(self.request(text), expected_statistics(text))

    def test_partial_header_and_payload(self):
        text = "partial TCP frame\n123"
        self.assertEqual(self.request(text, partial=True), expected_statistics(text))

    def test_partial_receive_and_header(self):
        text = "Ответ должен быть прочитан по одному байту"
        self.assertEqual(
            self.request(text, partial=True, partial_receive=True),
            expected_statistics(text),
        )

    def test_three_concurrent_clients(self):
        texts = ["client one 1", "клиент два 22", "client-three\n333"]
        results = [None] * len(texts)
        errors = []

        def worker(index, text):
            try:
                results[index] = self.request(text, partial=True)
            except Exception as error:  # noqa: BLE001 - ошибка потока передаётся в основной тест
                errors.append(error)

        threads = [threading.Thread(target=worker, args=(index, text)) for index, text in enumerate(texts)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(timeout=10)
        self.assertFalse(any(thread.is_alive() for thread in threads), "Поток клиента не завершился")
        self.assertEqual(errors, [])
        self.assertEqual(results, [expected_statistics(text) for text in texts])


if __name__ == "__main__":
    unittest.main(verbosity=2)
