# MiniRedis RESP2 Protocol Specification

MiniRedis implements the Redis Serialization Protocol version 2 (RESP2). The protocol is binary-safe, human-readable, and designed for high throughput over stream-oriented transport protocols like TCP.

---

## 1. Frame Types

| First Byte | Type | Format | Example |
|---|---|---|---|
| `+` | Simple String | `+<string>\r\n` | `+OK\r\n` |
| `-` | Error | `-<error message>\r\n` | `-ERR unknown command 'FOO'\r\n` |
| `:` | Integer | `:<signed 64-bit int>\r\n` | `:42\r\n` |
| `$` | Bulk String | `$<byte length>\r\n<data>\r\n` | `$5\r\nhello\r\n` |
| `$` | Null Bulk String | `$-1\r\n` | Key does not exist |
| `*` | Array | `*<element count>\r\n<elem1>...` | `*2\r\n$3\r\nGET\r\n$1\r\nk\r\n` |
| `*` | Null Array | `*-1\r\n` | Null response |

---

## 2. Command Streaming & Framing Details

### Partial Reads & Stream Chunking
TCP guarantees in-order byte delivery, but does not preserve application message boundaries. A single command frame may arrive across multiple `read()` system calls (e.g. `*2\r\n$3\r` followed by `\nGET\r\n...`).
- The parser inspects the buffered bytes without advancing the consumed index until an entire frame or command array is complete.
- Incomplete frames yield `ParseStatus::Incomplete`, instructing the server event loop to retain the buffer and poll for subsequent socket read events.

### Pipelining
Clients frequently transmit multiple requests in rapid succession without waiting for intermediate replies.
- The server buffers all incoming data and processes commands in a tight loop:
  ```cpp
  while (!in_buf.empty()) {
      auto res = RespParser::parse_command(in_buf);
      if (res.status == ParseStatus::Incomplete) break;
      in_buf.erase(0, res.bytes_consumed);
      out_buf += CommandDispatcher::execute(res.args, store).response;
  }
  ```
- All serialized responses are appended to the client's output buffer and written back in a single batch, minimizing system call overhead.

### Inline Commands
When a connection sends text commands not prefixed by RESP control characters (`*`, `$`, etc.), MiniRedis parses the line as an inline command:
- Reads until `\n` (stripping optional trailing `\r`).
- Splits tokens on whitespace (` ` and `\t`).
- Treats the tokens as an Array of Bulk Strings.
- Enables debugging via `nc`, `telnet`, or raw socket scripts.

---

## 3. Error Reply Standards

MiniRedis adheres to strict Redis error formatting standards:
- **Unknown Command**:  
  `-ERR unknown command '<CMD>', with args beginning with: '<ARG1>' '<ARG2>' \r\n`
- **Wrong Number of Arguments**:  
  `-ERR wrong number of arguments for '<cmd>' command\r\n`
- **Syntax Error**:  
  `-ERR syntax error\r\n`
- **Invalid Integer / TTL**:  
  `-ERR value is not an integer or out of range\r\n`
- **Protocol Framing Error**:  
  `-ERR Protocol error: <details>\r\n`
