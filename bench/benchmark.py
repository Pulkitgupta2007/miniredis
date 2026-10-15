#!/usr/bin/env python3
"""
MiniRedis Performance & Concurrency Benchmark Suite
Simulates production-grade multi-client workloads, TCP pipelining,
and reports throughput (ops/sec) and latency percentiles (P50, P95, P99).
"""

import argparse
import socket
import time
import threading
import statistics

def build_resp_cmd(*args):
    out = [f"*{len(args)}\r\n".encode()]
    for a in args:
        val = str(a).encode()
        out.append(f"${len(val)}\r\n".encode())
        out.append(val + b"\r\n")
    return b"".join(out)

def count_resp_frames(buf):
    idx = 0
    count = 0
    n = len(buf)
    while idx < n:
        prefix = chr(buf[idx])
        crlf = buf.find(b"\r\n", idx)
        if crlf == -1:
            break
        if prefix in ('+', '-', ':'):
            idx = crlf + 2
            count += 1
        elif prefix == '$':
            len_str = buf[idx + 1:crlf].decode('ascii', errors='ignore')
            try:
                bulk_len = int(len_str)
            except ValueError:
                idx = crlf + 2
                count += 1
                continue
            if bulk_len == -1:
                idx = crlf + 2
                count += 1
            else:
                end_idx = crlf + 2 + bulk_len + 2
                if end_idx <= n:
                    idx = end_idx
                    count += 1
                else:
                    break
        else:
            idx = crlf + 2
            count += 1
    return count, idx

def worker_thread(host, port, req_per_worker, pipeline_depth, data_size, test_type, latencies, errors):
    payload = "X" * data_size
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.connect((host, port))
    except Exception as e:
        errors.append(str(e))
        return

    remaining = req_per_worker
    key_idx = 0

    while remaining > 0:
        batch_size = min(remaining, pipeline_depth)
        pipeline_buf = bytearray()

        for _ in range(batch_size):
            key = f"bench:key:{threading.get_ident()}:{key_idx}"
            key_idx += 1
            if test_type == "SET":
                pipeline_buf.extend(build_resp_cmd("SET", key, payload))
            else:
                pipeline_buf.extend(build_resp_cmd("GET", key))

        t0 = time.perf_counter()
        try:
            s.sendall(pipeline_buf)
            received = 0
            read_buf = bytearray()
            while received < batch_size:
                chunk = s.recv(16384)
                if not chunk:
                    break
                read_buf.extend(chunk)
                cnt, consumed = count_resp_frames(read_buf)
                if cnt > 0:
                    received += cnt
                    del read_buf[:consumed]

            t1 = time.perf_counter()
            latency_ms = (t1 - t0) * 1000 / batch_size
            for _ in range(batch_size):
                latencies.append(latency_ms)
        except Exception as e:
            errors.append(str(e))
            break

        remaining -= batch_size

    try:
        s.close()
    except Exception:
        pass

def run_benchmark(host, port, num_clients, total_requests, pipeline_depth, data_size, test_type):
    print(f"\n========================================================")
    print(f" MiniRedis Benchmark: {test_type} ({total_requests:,} requests, {num_clients} clients, pipeline={pipeline_depth})")
    print(f"========================================================")

    req_per_worker = total_requests // num_clients
    latencies = []
    errors = []
    threads = []

    start_time = time.perf_counter()

    for _ in range(num_clients):
        t = threading.Thread(
            target=worker_thread,
            args=(host, port, req_per_worker, pipeline_depth, data_size, test_type, latencies, errors)
        )
        threads.append(t)
        t.start()

    for t in threads:
        t.join()

    total_time = time.perf_counter() - start_time
    total_completed = len(latencies)

    if total_completed == 0:
        print(f"Benchmark failed: All requests errored ({len(errors)} errors)")
        return

    ops_per_sec = total_completed / total_time
    latencies.sort()

    p50 = statistics.median(latencies)
    p90 = latencies[int(len(latencies) * 0.90)]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]
    p999 = latencies[int(len(latencies) * 0.999)]
    max_lat = latencies[-1]

    print(f"Throughput:    {ops_per_sec:,.2f} ops/sec")
    print(f"Total Time:    {total_time:.3f} seconds")
    print(f"Completed:     {total_completed:,} requests ({len(errors)} errors)")
    print(f"Latency P50:   {p50:.3f} ms")
    print(f"Latency P90:   {p90:.3f} ms")
    print(f"Latency P95:   {p95:.3f} ms")
    print(f"Latency P99:   {p99:.3f} ms")
    print(f"Latency P99.9: {p999:.3f} ms")
    print(f"Latency Max:   {max_lat:.3f} ms")

def main():
    parser = argparse.ArgumentParser(description="MiniRedis Performance Benchmark Suite")
    parser.add_argument("--host", default="127.0.0.1", help="MiniRedis server host")
    parser.add_argument("--port", type=int, default=6379, help="MiniRedis server port")
    parser.add_argument("-c", "--clients", type=int, default=10, help="Number of concurrent clients")
    parser.add_argument("-n", "--requests", type=int, default=20000, help="Total number of requests")
    parser.add_argument("-p", "--pipeline", type=int, default=16, help="Pipelining depth")
    parser.add_argument("-d", "--datasize", type=int, default=32, help="Payload data size in bytes")

    args = parser.parse_args()

    # Pre-test connection
    try:
        test_sock = socket.socket()
        test_sock.connect((args.host, args.port))
        test_sock.sendall(b"PING\r\n")
        resp = test_sock.recv(1024)
        test_sock.close()
        if b"PONG" not in resp:
            print(f"Error: Server responded with unexpected payload: {resp}")
            return
    except Exception as e:
        print(f"Error: Unable to connect to MiniRedis on {args.host}:{args.port} - {e}")
        return

    run_benchmark(args.host, args.port, args.clients, args.requests, args.pipeline, args.datasize, "SET")
    run_benchmark(args.host, args.port, args.clients, args.requests, args.pipeline, args.datasize, "GET")

if __name__ == "__main__":
    main()
