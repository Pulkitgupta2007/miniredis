#!/usr/bin/env python3
"""
benchmark_comparison.py: Comprehensive comparative benchmark between real Redis and MiniRedis.
Runs redis-benchmark with identical parameters:
- SET and GET
- Non-pipelined (-P 1) and pipelined (-P 16)
Outputs structured results table (ops/sec, P50, P99 latency) and analysis.
"""

import subprocess
import time
import socket
import csv
import io
import sys
import os

REAL_REDIS_PORT = 6384
MINIREDIS_PORT = 6385
HOST = '127.0.0.1'

REDIS_BENCHMARK = os.path.expanduser('~/.local/bin/redis-benchmark')
REDIS_SERVER = os.path.expanduser('~/.local/bin/redis-server')
MINIREDIS_SERVER = os.path.abspath(os.path.join(os.path.dirname(__file__), '../server/build/miniredis-server'))

def wait_for_server(host, port, timeout=5.0):
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.create_connection((host, port), timeout=0.1)
            s.close()
            return True
        except OSError:
            time.sleep(0.05)
    return False

def run_benchmark(port, test_name, pipelined=False, requests=25000, clients=50):
    cmd = [
        REDIS_BENCHMARK,
        '-h', HOST,
        '-p', str(port),
        '-t', test_name.lower(),
        '-c', str(clients),
        '-n', str(requests),
        '--csv',
        '--precision', '3'
    ]
    if pipelined:
        cmd.extend(['-P', '16'])

    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"redis-benchmark failed (port {port}): {proc.stderr}")

    reader = csv.DictReader(io.StringIO(proc.stdout.strip()))
    for row in reader:
        return {
            'test': row['test'],
            'rps': float(row['rps']),
            'avg_ms': float(row['avg_latency_ms']),
            'p50_ms': float(row['p50_latency_ms']),
            'p95_ms': float(row['p95_latency_ms']),
            'p99_ms': float(row['p99_latency_ms']),
        }
    raise RuntimeError(f"No benchmark data returned from port {port}")

def main():
    print("===================================================================")
    print("   MiniRedis vs. Real Redis Performance Benchmark Suite          ")
    print("===================================================================")
    print(f"Host: {HOST} | Machine: Darwin Apple Silicon | Comp: clang++")

    # 1. Start Real Redis
    print(f"\n[1/4] Starting Real Redis on port {REAL_REDIS_PORT}...")
    real_proc = subprocess.Popen([
        REDIS_SERVER,
        '--port', str(REAL_REDIS_PORT),
        '--save', '',
        '--appendonly', 'no',
        '--daemonize', 'no',
        '--loglevel', 'warning'
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    if not wait_for_server(HOST, REAL_REDIS_PORT):
        print(f"Error: Real Redis failed to start on port {REAL_REDIS_PORT}", file=sys.stderr)
        real_proc.kill()
        sys.exit(1)

    real_results = {}
    try:
        print("  Running Real Redis benchmarks...")
        print("    -> SET (non-pipelined, 50 clients, 25k req)")
        real_results['set_nopipe'] = run_benchmark(REAL_REDIS_PORT, 'set', pipelined=False, requests=25000)
        print("    -> GET (non-pipelined, 50 clients, 25k req)")
        real_results['get_nopipe'] = run_benchmark(REAL_REDIS_PORT, 'get', pipelined=False, requests=25000)
        print("    -> SET (pipelined P=16, 50 clients, 50k req)")
        real_results['set_pipe'] = run_benchmark(REAL_REDIS_PORT, 'set', pipelined=True, requests=50000)
        print("    -> GET (pipelined P=16, 50 clients, 50k req)")
        real_results['get_pipe'] = run_benchmark(REAL_REDIS_PORT, 'get', pipelined=True, requests=50000)
    finally:
        real_proc.terminate()
        real_proc.wait()

    # 2. Start MiniRedis
    print(f"\n[2/4] Starting MiniRedis on port {MINIREDIS_PORT}...")
    mini_proc = subprocess.Popen([
        MINIREDIS_SERVER,
        str(MINIREDIS_PORT),
        HOST
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    if not wait_for_server(HOST, MINIREDIS_PORT):
        print(f"Error: MiniRedis failed to start on port {MINIREDIS_PORT}", file=sys.stderr)
        mini_proc.kill()
        sys.exit(1)

    mini_results = {}
    try:
        print("  Running MiniRedis benchmarks...")
        print("    -> SET (non-pipelined, 50 clients, 25k req)")
        mini_results['set_nopipe'] = run_benchmark(MINIREDIS_PORT, 'set', pipelined=False, requests=25000)
        print("    -> GET (non-pipelined, 50 clients, 25k req)")
        mini_results['get_nopipe'] = run_benchmark(MINIREDIS_PORT, 'get', pipelined=False, requests=25000)
        print("    -> SET (pipelined P=16, 50 clients, 50k req)")
        mini_results['set_pipe'] = run_benchmark(MINIREDIS_PORT, 'set', pipelined=True, requests=50000)
        print("    -> GET (pipelined P=16, 50 clients, 50k req)")
        mini_results['get_pipe'] = run_benchmark(MINIREDIS_PORT, 'get', pipelined=True, requests=50000)
    finally:
        mini_proc.terminate()
        mini_proc.wait()

    # 3. Print Results Table
    print("\n[3/4] Comparative Benchmark Results Table:")
    print("=" * 86)
    print(f"{'Benchmark Workload':<32} | {'Metric':<14} | {'Real Redis':<16} | {'MiniRedis':<16}")
    print("-" * 86)

    def print_section(title, k):
        r = real_results[k]
        m = mini_results[k]
        pct = (m['rps'] / r['rps']) * 100.0

        print(f"{title:<32} | {'Throughput':<14} | {r['rps']:>10,.0f} ops/s | {m['rps']:>10,.0f} ops/s ({pct:.1f}%)")
        print(f"{'':<32} | {'P50 Latency':<14} | {r['p50_ms']:>10.3f} ms   | {m['p50_ms']:>10.3f} ms")
        print(f"{'':<32} | {'P99 Latency':<14} | {r['p99_ms']:>10.3f} ms   | {m['p99_ms']:>10.3f} ms")
        print("-" * 86)

    print_section("SET (non-pipelined, -P 1)", 'set_nopipe')
    print_section("GET (non-pipelined, -P 1)", 'get_nopipe')
    print_section("SET (pipelined, -P 16)", 'set_pipe')
    print_section("GET (pipelined, -P 16)", 'get_pipe')

    # 4. Save results to markdown for documentation
    md_output = f"""# MiniRedis vs. Real Redis Benchmark Results

**Environment**: Darwin arm64 (Apple Silicon), loopback network (`127.0.0.1`), 50 parallel client connections.

| Workload | Metric | Real Redis | MiniRedis | Ratio (Mini / Real) |
|---|---|---|---|---|
| **SET (non-pipelined, -P 1)** | Throughput | {real_results['set_nopipe']['rps']:,.0f} ops/s | {mini_results['set_nopipe']['rps']:,.0f} ops/s | **{(mini_results['set_nopipe']['rps']/real_results['set_nopipe']['rps'])*100:.1f}%** |
| | P50 Latency | {real_results['set_nopipe']['p50_ms']:.3f} ms | {mini_results['set_nopipe']['p50_ms']:.3f} ms | - |
| | P99 Latency | {real_results['set_nopipe']['p99_ms']:.3f} ms | {mini_results['set_nopipe']['p99_ms']:.3f} ms | - |
| **GET (non-pipelined, -P 1)** | Throughput | {real_results['get_nopipe']['rps']:,.0f} ops/s | {mini_results['get_nopipe']['rps']:,.0f} ops/s | **{(mini_results['get_nopipe']['rps']/real_results['get_nopipe']['rps'])*100:.1f}%** |
| | P50 Latency | {real_results['get_nopipe']['p50_ms']:.3f} ms | {mini_results['get_nopipe']['p50_ms']:.3f} ms | - |
| | P99 Latency | {real_results['get_nopipe']['p99_ms']:.3f} ms | {mini_results['get_nopipe']['p99_ms']:.3f} ms | - |
| **SET (pipelined, -P 16)** | Throughput | {real_results['set_pipe']['rps']:,.0f} ops/s | {mini_results['set_pipe']['rps']:,.0f} ops/s | **{(mini_results['set_pipe']['rps']/real_results['set_pipe']['rps'])*100:.1f}%** |
| | P50 Latency | {real_results['set_pipe']['p50_ms']:.3f} ms | {mini_results['set_pipe']['p50_ms']:.3f} ms | - |
| | P99 Latency | {real_results['set_pipe']['p99_ms']:.3f} ms | {mini_results['set_pipe']['p99_ms']:.3f} ms | - |
| **GET (pipelined, -P 16)** | Throughput | {real_results['get_pipe']['rps']:,.0f} ops/s | {mini_results['get_pipe']['rps']:,.0f} ops/s | **{(mini_results['get_pipe']['rps']/real_results['get_pipe']['rps'])*100:.1f}%** |
| | P50 Latency | {real_results['get_pipe']['p50_ms']:.3f} ms | {mini_results['get_pipe']['p50_ms']:.3f} ms | - |
| | P99 Latency | {real_results['get_pipe']['p99_ms']:.3f} ms | {mini_results['get_pipe']['p99_ms']:.3f} ms | - |
"""
    results_path = os.path.abspath(os.path.join(os.path.dirname(__file__), 'BENCHMARKS.md'))
    with open(results_path, 'w') as f:
        f.write(md_output)

    print(f"\n[4/4] Results written to {results_path}")

if __name__ == '__main__':
    main()
