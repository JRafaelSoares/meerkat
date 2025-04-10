import sys
import csv
from process_logs import BenchmarkResult, process_client_logs_parallel, process_IA_microbench_logs
from meerkat_benchmarks import ParametersAndResult
from pathlib import Path

def usage():
    print("python update_result.py OUTPUT_FILENAME warmup duration INPUT_FOLDER")

def main():
    if len(sys.argv) < 4:
        usage()

    out = sys.argv[1]
    warmup = int(sys.argv[2])
    duration = int(sys.argv[3])
    in_folder = sys.argv[4]

    with open(out, 'w') as f:
        csv_writer = csv.writer(f)
        csv_writer.writerow(ParametersAndResult._fields)

        for path in Path(in_folder).glob("*/client.log"):
            try:
                result = process_client_logs_parallel(path, warmup, duration)
                if result:
                    print(result)
                    csv_writer.writerow(ParametersAndResult(*(parameters + result)))
                    f.flush()
            finally:
                continue

if __name__ == '__main__':
    main()
