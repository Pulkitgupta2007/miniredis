#!/usr/bin/env python3
"""
Integration test for MiniRedis server running against the real redis-cli binary
and raw TCP socket streams (for fragmented, pipelined, and inline checks).
"""

import os
import signal
import socket
import subprocess
import sys
import time

SERVER_BIN = os.path.abspath(os.path.join(os.path.dirname(__file__), "../build/miniredis-server"))
REDIS_CLI_BIN = os.path.expanduser("~/.local/bin/redis-cli")
PORT = 6389
HOST = "127.0.0.1"

def run_cli(*args):
    cmd = [REDIS_CLI_BIN, "-h", HOST, "-p", str(PORT)] + list(args)
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5)
    return proc.stdout.strip(), proc.stderr.strip(), proc.returncode

def wait_for_server(host, port, timeout=3.0):
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.create_connection((host, port), timeout=0.1)
            s.close()
            return True
        except OSError:
            time.sleep(0.05)
    return False

def test_raw_socket_pipeline():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    
    # Send 4 pipelined commands in a single TCP packet
    payload = b"*1\r\n$8\r\nFLUSHALL\r\n*1\r\n$4\r\nPING\r\n*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n*1\r\n$6\r\nDBSIZE\r\n"
    s.sendall(payload)
    
    expected = b"+OK\r\n+PONG\r\n$5\r\nhello\r\n:0\r\n"
    response = b""
    while len(response) < len(expected):
        chunk = s.recv(1024)
        if not chunk:
            break
        response += chunk
    s.close()
    
    assert response == expected, f"Pipeline failed: expected {expected!r}, got {response!r}"
    print("  [PASS] Pipelined commands via raw TCP socket")

def test_raw_socket_inline():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    
    # Send inline command (no RESP prefix)
    s.sendall(b"PING\r\n")
    res = s.recv(1024)
    assert res == b"+PONG\r\n", f"Inline PING failed: got {res!r}"
    
    s.sendall(b"SET inline_key inline_val\r\n")
    res = s.recv(1024)
    assert res == b"+OK\r\n", f"Inline SET failed: got {res!r}"
    
    s.sendall(b"GET inline_key\r\n")
    res = s.recv(1024)
    assert res == b"$10\r\ninline_val\r\n", f"Inline GET failed: got {res!r}"
    s.close()
    print("  [PASS] Inline commands via raw TCP socket")

def main():
    print(f"=== Starting MiniRedis Integration Test on port {PORT} ===")
    if not os.path.exists(SERVER_BIN):
        print(f"Error: Server binary not found at {SERVER_BIN}", file=sys.stderr)
        sys.exit(1)
        
    if not os.path.exists(REDIS_CLI_BIN):
        print(f"Error: redis-cli not found at {REDIS_CLI_BIN}", file=sys.stderr)
        sys.exit(1)

    # Launch server
    server_proc = subprocess.Popen([SERVER_BIN, str(PORT), HOST],
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE,
                                   text=True)

    # Allow server to bind and listen
    if not wait_for_server(HOST, PORT):
        print(f"Error: Server failed to start on {HOST}:{PORT}", file=sys.stderr)
        server_proc.kill()
        sys.exit(1)

    try:
        # 1. PING
        out, _, _ = run_cli("PING")
        assert out == "PONG", f"PING failed: {out}"
        print("  [PASS] redis-cli PING -> PONG")

        # 2. SET and GET
        out, _, _ = run_cli("SET", "alpha", "bravo")
        assert out == "OK", f"SET failed: {out}"
        out, _, _ = run_cli("GET", "alpha")
        assert out == "bravo", f"GET failed: {out}"
        print("  [PASS] redis-cli SET / GET")

        # 3. Overwrite & Binary-safe characters
        out, _, _ = run_cli("SET", "special_key", "value with spaces and \t tabs")
        assert out == "OK", f"SET special failed: {out}"
        out, _, _ = run_cli("GET", "special_key")
        assert out == "value with spaces and \t tabs", f"GET special failed: {out}"
        print("  [PASS] redis-cli values with special whitespace")

        # 4. EXISTS & DEL
        out, _, _ = run_cli("EXISTS", "alpha")
        assert out in ("1", "(integer) 1"), f"EXISTS failed: {out}"
        out, _, _ = run_cli("DEL", "alpha")
        assert out in ("1", "(integer) 1"), f"DEL failed: {out}"
        out, _, _ = run_cli("GET", "alpha")
        assert out in ("", "(nil)", "nil"), f"GET after DEL should be nil, got: {out}"
        print("  [PASS] redis-cli EXISTS / DEL")

        # 5. DBSIZE & FLUSHALL
        run_cli("SET", "k1", "v1")
        run_cli("SET", "k2", "v2")
        out, _, _ = run_cli("DBSIZE")
        assert out in ("2", "3", "(integer) 2", "(integer) 3"), f"DBSIZE failed: {out}"
        out, _, _ = run_cli("FLUSHALL")
        assert out == "OK", f"FLUSHALL failed: {out}"
        out, _, _ = run_cli("DBSIZE")
        assert out in ("0", "(integer) 0"), f"DBSIZE after FLUSHALL failed: {out}"
        print("  [PASS] redis-cli DBSIZE / FLUSHALL")

        # 6. TTL, EXPIRE, PTTL
        run_cli("SET", "temp", "val")
        out, _, _ = run_cli("EXPIRE", "temp", "10")
        assert out in ("1", "(integer) 1"), f"EXPIRE failed: {out}"
        out, _, _ = run_cli("TTL", "temp")
        val = int(out.replace("(integer)", "").strip())
        assert val > 0, f"TTL should be positive, got: {out}"
        print("  [PASS] redis-cli EXPIRE / TTL")

        # 7. Error handling: Unknown command
        out, err, _ = run_cli("FOOBAR")
        assert "ERR unknown command 'FOOBAR'" in out or "ERR unknown command 'FOOBAR'" in err, f"Unknown command error missing: {out} {err}"
        print("  [PASS] Proper error reply: -ERR unknown command")

        # 8. Error handling: Wrong number of arguments
        out, err, _ = run_cli("GET")
        assert "ERR wrong number of arguments for 'get' command" in out or "ERR wrong number of arguments for 'get' command" in err, f"Wrong arg error missing: {out} {err}"
        print("  [PASS] Proper error reply: -ERR wrong number of arguments")

        # 9. CONFIG GET / SET and MEMORY USAGE
        out, _, _ = run_cli("CONFIG", "SET", "maxmemory", "1048576")
        assert out == "OK", f"CONFIG SET maxmemory failed: {out}"
        out, _, _ = run_cli("CONFIG", "GET", "maxmemory")
        assert "1048576" in out, f"CONFIG GET maxmemory failed: {out}"

        out, _, _ = run_cli("CONFIG", "SET", "maxmemory-policy", "allkeys-lru")
        assert out == "OK", f"CONFIG SET maxmemory-policy failed: {out}"
        out, _, _ = run_cli("CONFIG", "GET", "maxmemory-policy")
        assert "allkeys-lru" in out, f"CONFIG GET maxmemory-policy failed: {out}"

        run_cli("SET", "memkey", "samplevalue")
        out, _, _ = run_cli("MEMORY", "USAGE", "memkey")
        assert int(out.replace("(integer)", "").strip()) > 0, f"MEMORY USAGE failed: {out}"
        print("  [PASS] redis-cli CONFIG SET/GET and MEMORY USAGE")

        # 10. Memory limit and noeviction OOM
        run_cli("CONFIG", "SET", "maxmemory-policy", "noeviction")
        out, _, _ = run_cli("MEMORY", "USAGE", "memkey")
        mem_bytes = int(out.replace("(integer)", "").strip())
        run_cli("CONFIG", "SET", "maxmemory", str(mem_bytes))
        out, err, _ = run_cli("SET", "memkey2", "overflow")
        assert "OOM command not allowed" in out or "OOM command not allowed" in err, f"OOM error expected, got: {out} {err}"
        # Reset memory limit to unlimited
        run_cli("CONFIG", "SET", "maxmemory", "0")
        print("  [PASS] Proper OOM reply under noeviction policy")

        # 11. INFO command with all observability fields
        info_out, _, _ = run_cli("INFO")
        for expected_field in [
            "used_memory:", "total_keys:", "keyspace_hits:", "keyspace_misses:",
            "hit_ratio:", "evicted_keys:", "expired_keys:", "connected_clients:",
            "total_commands_processed:", "ops_per_sec:", "uptime_in_seconds:"
        ]:
            assert expected_field in info_out, f"Missing field in INFO output: {expected_field}\nOutput:\n{info_out}"
        print("  [PASS] redis-cli INFO contains all required observability fields")

        # 11. Pipelined commands via raw TCP socket
        test_raw_socket_pipeline()

        # 12. Inline commands via raw TCP socket
        test_raw_socket_inline()

        # 13. Graceful shutdown on SIGINT
        print("  Testing graceful shutdown on SIGINT...")
        server_proc.send_signal(signal.SIGINT)
        server_proc.wait(timeout=3)
        assert server_proc.returncode == 0, f"Server did not exit with code 0 on SIGINT: {server_proc.returncode}"
        print("  [PASS] Graceful shutdown on SIGINT (exit code 0)")

    finally:
        if server_proc.poll() is None:
            server_proc.kill()
            server_proc.wait()

    print("\n>>> ALL INTEGRATION TESTS PASSED SUCCESSFULLY! <<<\n")

if __name__ == "__main__":
    main()
