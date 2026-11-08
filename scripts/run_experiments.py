import argparse
import csv
from pathlib import Path
import socket
import struct
import subprocess
import time

MAX_PAYLOAD_SIZE = 65536
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 20054
DEFAULT_SIZES = (100, 1000, 10000)
DEFAULT_REPETITIONS = 3
CSV_HEADER = ("message_size", "repeat", "time_seconds", "bytes", "result")


def find_server_executable(root):
    candidates = (
        root / "bin" / "x64" / "Debug" / "Server.exe",
        root / "bin" / "x64" / "Release" / "Server.exe",
        root / "Server" / "x64" / "Debug" / "Server.exe",
        root / "Server" / "x64" / "Release" / "Server.exe",
        root / "x64" / "Debug" / "Server.exe",
        root / "x64" / "Release" / "Server.exe",
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def receive_exact(connection, length):
    data = bytearray()
    while len(data) < length:
        chunk = connection.recv(min(4096, length - len(data)))
        if not chunk:
            raise RuntimeError("Соединение закрыто до получения полного кадра")
        data.extend(chunk)
    return bytes(data)


def receive_frame(connection):
    header = receive_exact(connection, 4)
    payload_length = struct.unpack("!I", header)[0]
    if payload_length > MAX_PAYLOAD_SIZE:
        raise RuntimeError(f"Сервер вернул недопустимый размер ответа: {payload_length}")
    return receive_exact(connection, payload_length)


def parse_statistics(response):
    statistics = {}
    for line in response.decode("ascii").splitlines():
        name, value = line.split("=", 1)
        statistics[name] = int(value)
    return statistics


def make_payload(size):
    payload = bytes(65 + index % 26 for index in range(size))
    if len(payload) != size:
        raise RuntimeError(f"Сформирован payload длиной {len(payload)} вместо {size}")
    return payload


def wait_for_server(host, port, process=None, timeout=10):
    deadline = time.monotonic() + timeout
    last_error = None
    while time.monotonic() < deadline:
        if process is not None and process.poll() is not None:
            raise RuntimeError(f"Server.exe завершился с кодом {process.returncode}")
        try:
            with socket.create_connection((host, port), timeout=0.25):
                return
        except OSError as error:
            last_error = error
            time.sleep(0.1)
    raise RuntimeError(f"Сервер не начал принимать подключения: {last_error}")


def start_server(server_path, host, port):
    if server_path is None or not server_path.is_file():
        raise FileNotFoundError(
            "Server.exe не найден. Передайте путь через --server или сначала соберите Server."
        )
    process = subprocess.Popen(
        [str(server_path), "--host", host, "--port", str(port)],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
    )
    try:
        wait_for_server(host, port, process)
    except Exception:
        process.terminate()
        process.wait(timeout=3)
        raise
    return process


def stop_server(process):
    if process is None:
        return
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def append_result(csv_path, message_size, repeat, elapsed, result_bytes, result):
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    write_header = not csv_path.exists() or csv_path.stat().st_size == 0
    with csv_path.open("a", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        if write_header:
            writer.writerow(CSV_HEADER)
        writer.writerow((message_size, repeat, f"{elapsed:.9f}", result_bytes, result))


def run_experiments(args):
    root = Path(__file__).resolve().parents[1]
    server_path = args.server or find_server_executable(root)
    server_process = None
    csv_path = Path(args.output)
    sizes = tuple(args.sizes)

    if any(size <= 0 or size > MAX_PAYLOAD_SIZE for size in sizes):
        raise ValueError(f"Размеры должны находиться в диапазоне 1..{MAX_PAYLOAD_SIZE}")
    if args.repetitions <= 0:
        raise ValueError("Количество повторов должно быть положительным")

    try:
        if args.reuse_server:
            wait_for_server(args.host, args.port)
        else:
            server_process = start_server(server_path, args.host, args.port)

        for size in sizes:
            payload = make_payload(size)
            if len(payload) != size:
                raise RuntimeError("Фактический размер payload не совпадает с заданным")

            for repeat in range(1, args.repetitions + 1):
                with socket.create_connection((args.host, args.port), timeout=args.timeout) as connection:
                    connection.settimeout(args.timeout)
                    request = struct.pack("!I", len(payload)) + payload
                    started = time.perf_counter()
                    connection.sendall(request)
                    response = receive_frame(connection)
                    elapsed = time.perf_counter() - started

                statistics = parse_statistics(response)
                if statistics.get("bytes") != len(payload):
                    raise RuntimeError(
                        f"Некорректный ответ для {size} байт: сервер указал {statistics.get('bytes')}"
                    )
                required_fields = {"bytes", "lines", "words", "digits", "latinLetters"}
                if set(statistics) != required_fields:
                    raise RuntimeError(f"Неполный ответ сервера: {statistics}")

                append_result(csv_path, size, repeat, elapsed, statistics["bytes"], "PASS")
                print(f"size={size}, repeat={repeat}, time={elapsed:.6f}s, result=PASS")
    finally:
        stop_server(server_process)


def parse_arguments():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Измерение времени TCP-обмена с Server.exe")
    parser.add_argument("--server", type=Path, default=None, help="Путь к Server.exe")
    parser.add_argument("--host", default=DEFAULT_HOST, help="Адрес сервера")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="Порт сервера")
    parser.add_argument("--output", type=Path, default=root / "data" / "experiments.csv")
    parser.add_argument("--repetitions", type=int, default=DEFAULT_REPETITIONS)
    parser.add_argument("--sizes", type=int, nargs="+", default=DEFAULT_SIZES)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument(
        "--reuse-server",
        action="store_true",
        help="Использовать уже запущенный сервер и не запускать новый процесс",
    )
    return parser.parse_args()


if __name__ == "__main__":
    try:
        run_experiments(parse_arguments())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"Ошибка эксперимента: {error}")
        raise SystemExit(1)
