import argparse
import csv
from collections import defaultdict
from pathlib import Path


REQUIRED_COLUMNS = {"message_size", "repeat", "time_seconds", "bytes", "result"}


def read_results(input_path):
    grouped = defaultdict(list)
    with input_path.open("r", newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or not REQUIRED_COLUMNS.issubset(reader.fieldnames):
            missing = REQUIRED_COLUMNS - set(reader.fieldnames or ())
            raise ValueError(f"В CSV отсутствуют столбцы: {', '.join(sorted(missing))}")

        for row_number, row in enumerate(reader, start=2):
            if row.get("result", "").strip().upper() != "PASS":
                continue
            try:
                message_size = int(row["message_size"])
                elapsed = float(row["time_seconds"])
                actual_bytes = int(row["bytes"])
            except (TypeError, ValueError) as error:
                raise ValueError(f"Некорректная строка CSV {row_number}: {error}") from error
            if message_size <= 0 or actual_bytes <= 0 or elapsed <= 0:
                continue
            grouped[message_size].append((actual_bytes, elapsed))
    return grouped


def build_plots(input_path, output_dir):
    grouped = read_results(input_path)
    if not grouped:
        print(f"Нет корректных результатов в {input_path}; графики не построены.")
        return False

    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    sizes = sorted(grouped)
    average_times = []
    throughputs = []
    for size in sizes:
        measurements = grouped[size]
        average_time = sum(elapsed for _, elapsed in measurements) / len(measurements)
        average_bytes = sum(actual_bytes for actual_bytes, _ in measurements) / len(measurements)
        average_times.append(average_time)
        throughputs.append(average_bytes / average_time)

    output_dir.mkdir(parents=True, exist_ok=True)

    latency_path = output_dir / "latency.png"
    plt.figure(figsize=(8, 5))
    plt.plot(sizes, average_times, marker="o")
    plt.title("Зависимость времени TCP-обмена от размера сообщения")
    plt.xlabel("Размер сообщения, байт")
    plt.ylabel("Среднее время обмена, с")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(latency_path, dpi=150)
    plt.close()

    throughput_path = output_dir / "throughput.png"
    plt.figure(figsize=(8, 5))
    plt.plot(sizes, throughputs, marker="o")
    plt.title("Зависимость пропускной способности от размера сообщения")
    plt.xlabel("Размер сообщения, байт")
    plt.ylabel("Пропускная способность, байт/с")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    plt.savefig(throughput_path, dpi=150)
    plt.close()

    print(f"График задержки сохранён: {latency_path}")
    print(f"График пропускной способности сохранён: {throughput_path}")
    return True


def parse_arguments():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Построение графиков TCP-экспериментов")
    parser.add_argument("--input", type=Path, default=root / "data" / "experiments.csv")
    parser.add_argument("--output-dir", type=Path, default=root / "data")
    return parser.parse_args()


if __name__ == "__main__":
    arguments = parse_arguments()
    if not arguments.input.is_file():
        print(f"Файл результатов не найден: {arguments.input}")
        raise SystemExit(1)
    try:
        build_plots(arguments.input, arguments.output_dir)
    except (OSError, ValueError, ImportError) as error:
        print(f"Ошибка построения графиков: {error}")
        raise SystemExit(1)
