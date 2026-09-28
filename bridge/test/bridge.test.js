// bridge.test.js: Comprehensive unit tests for MiniRedis Bridge & RedisClient
const { test, describe, before, after } = require('node:test');
const assert = require('node:assert');
const net = require('node:net');
const http = require('node:http');
const { WebSocket } = require('ws');

const { parseInfo, parseEvents, encodeRespCommand, parseRespFrame } = require('../src/parser');
const RedisClient = require('../src/redis-client');
const { createBridgeServer } = require('../src/server');

describe('Parser Utilities', () => {
  test('parseInfo parses key-value pairs and sections correctly', () => {
    const raw = `# Server\r\nredis_version:miniredis-1.0.0\r\nuptime_in_seconds:42\r\n\r\n# Stats\r\nhit_ratio:87.50\r\nkeyspace_hits:14\r\nkeyspace_misses:2\r\n`;
    const info = parseInfo(raw);

    assert.strictEqual(info.redis_version, 'miniredis-1.0.0');
    assert.strictEqual(info.uptime_in_seconds, 42);
    assert.strictEqual(info.hit_ratio, 87.5);
    assert.strictEqual(info.keyspace_hits, 14);
    assert.strictEqual(info.keyspace_misses, 2);
  });

  test('parseEvents parses formatted event strings', () => {
    const rawEvents = [
      '1700000000000:EVICTION:user:1:freed=64',
      '1700000000100:EXPIRATION:session_token:active_cycle'
    ];
    const events = parseEvents(rawEvents);
    assert.strictEqual(events.length, 2);
    assert.strictEqual(events[0].type, 'EVICTION');
    assert.strictEqual(events[0].key, 'user');
    assert.strictEqual(events[0].detail, '1:freed=64');
    assert.strictEqual(events[1].type, 'EXPIRATION');
    assert.strictEqual(events[1].key, 'session_token');
    assert.strictEqual(events[1].detail, 'active_cycle');
  });

  test('encodeRespCommand formats arguments into RESP2 Array', () => {
    const resp = encodeRespCommand(['SET', 'mykey', 'myvalue']);
    assert.strictEqual(resp, '*3\r\n$3\r\nSET\r\n$5\r\nmykey\r\n$7\r\nmyvalue\r\n');
  });

  test('parseRespFrame handles simple string, bulk string, integer, array, and errors', () => {
    const sStr = parseRespFrame(Buffer.from('+OK\r\n'));
    assert.strictEqual(sStr.value, 'OK');

    const err = parseRespFrame(Buffer.from('-ERR unknown\r\n'));
    assert.strictEqual(err.isError, true);
    assert.strictEqual(err.error, 'ERR unknown');

    const num = parseRespFrame(Buffer.from(':42\r\n'));
    assert.strictEqual(num.value, 42);

    const bulk = parseRespFrame(Buffer.from('$5\r\nhello\r\n'));
    assert.strictEqual(bulk.value, 'hello');

    const nullBulk = parseRespFrame(Buffer.from('$-1\r\n'));
    assert.strictEqual(nullBulk.value, null);

    const arr = parseRespFrame(Buffer.from('*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n'));
    assert.deepStrictEqual(arr.value, ['foo', 'bar']);

    const incomplete = parseRespFrame(Buffer.from('$10\r\npartial'));
    assert.strictEqual(incomplete.complete, false);
  });
});

describe('RedisClient with Mock TCP Server', () => {
  let mockServer;
  let mockPort;
  let client;

  before(async () => {
    await new Promise((resolve) => {
      mockServer = net.createServer((socket) => {
        socket.on('data', (data) => {
          const str = data.toString();
          if (str.includes('PING')) {
            socket.write('+PONG\r\n');
          } else if (str.includes('INFO')) {
            const body = '# Stats\r\nused_memory:1024\r\nops_per_sec:10\r\n';
            socket.write(`$${body.length}\r\n${body}\r\n`);
          } else if (str.includes('ERRTEST')) {
            socket.write('-ERR syntax error\r\n');
          } else {
            socket.write('+OK\r\n');
          }
        });
      });
      mockServer.listen(0, '127.0.0.1', () => {
        mockPort = mockServer.address().port;
        resolve();
      });
    });
  });

  after(async () => {
    if (client) client.close();
    await new Promise((resolve) => mockServer.close(resolve));
  });

  test('connects and receives command response', async () => {
    client = new RedisClient({ host: '127.0.0.1', port: mockPort, autoReconnect: false });
    client.connect();

    await new Promise((resolve) => client.once('connect', resolve));
    assert.strictEqual(client.isConnected, true);

    const pingRes = await client.sendCommand(['PING']);
    assert.strictEqual(pingRes, 'PONG');

    const info = await client.getInfo();
    assert.strictEqual(info.used_memory, 1024);
    assert.strictEqual(info.ops_per_sec, 10);
  });

  test('handles RESP error replies as rejected promises', async () => {
    await assert.rejects(
      async () => {
        await client.sendCommand(['ERRTEST']);
      },
      { message: 'ERR syntax error' }
    );
  });

  test('rejects commands when disconnected', async () => {
    client.close();
    assert.strictEqual(client.isConnected, false);
    await assert.rejects(
      async () => {
        await client.sendCommand(['PING']);
      },
      { message: 'Server offline' }
    );
  });
});

describe('Bridge Server & WebSocket Protocol', () => {
  let mockServer;
  let mockPort;
  let bridge;
  let bridgePort;

  before(async () => {
    // 1. Mock MiniRedis TCP Server
    await new Promise((resolve) => {
      mockServer = net.createServer((socket) => {
        let inBuf = Buffer.alloc(0);
        socket.on('data', (chunk) => {
          inBuf = Buffer.concat([inBuf, chunk]);
          let offset = 0;
          while (offset < inBuf.length) {
            const frame = parseRespFrame(inBuf, offset);
            if (!frame.complete) break;
            offset = frame.nextOffset;

            let cmd = '';
            if (Array.isArray(frame.value) && frame.value.length > 0) {
              cmd = String(frame.value[0]).toUpperCase();
            } else if (frame.value) {
              cmd = String(frame.value).toUpperCase();
            }

            if (cmd === 'INFO') {
              const body = 'used_memory:2048\r\ntotal_keys:5\r\nhit_ratio:100.0\r\n';
              socket.write(`$${body.length}\r\n${body}\r\n`);
            } else if (cmd === 'LRUKEYS') {
              socket.write('*2\r\n$2\r\nk1\r\n$2\r\nk2\r\n');
            } else if (cmd === 'EVENTS') {
              const evStr = '1700000000000:EVICTION:old_key:freed';
              socket.write(`*1\r\n$${Buffer.byteLength(evStr)}\r\n${evStr}\r\n`);
            } else if (cmd === 'SET') {
              socket.write('+OK\r\n');
            } else {
              socket.write('+OK\r\n');
            }
          }
          if (offset > 0) {
            inBuf = inBuf.slice(offset);
          }
        });
      });
      mockServer.listen(0, '127.0.0.1', () => {
        mockPort = mockServer.address().port;
        resolve();
      });
    });

    // 2. Start Bridge Server connected to mock TCP server
    bridge = createBridgeServer({
      redisPort: mockPort,
      port: 0,
      pollInterval: 100
    });
    const info = await bridge.start();
    bridgePort = info.port;
  });

  after(async () => {
    await bridge.close();
    await new Promise((resolve) => mockServer.close(resolve));
  });

  test('WebSocket receives INITIAL_STATE and TELEMETRY_UPDATE', async () => {
    const ws = new WebSocket(`ws://127.0.0.1:${bridgePort}`);

    const messages = [];
    await new Promise((resolve, reject) => {
      ws.on('open', () => {});
      ws.on('message', (raw) => {
        const msg = JSON.parse(raw.toString());
        messages.push(msg);
        if (messages.length >= 2) {
          ws.close();
          resolve();
        }
      });
      ws.on('error', reject);
    });

    assert.strictEqual(messages[0].type, 'INITIAL_STATE');
    const update = messages.find((m) => m.type === 'TELEMETRY_UPDATE');
    assert.ok(update, 'Expected at least one TELEMETRY_UPDATE message');
    assert.strictEqual(update.online, true);
    assert.strictEqual(update.metrics.used_memory, 2048);
    assert.deepStrictEqual(update.lru_keys, ['k1', 'k2']);
  });

  test('Proxies command through WebSocket and receives COMMAND_REPLY', async () => {
    const ws = new WebSocket(`ws://127.0.0.1:${bridgePort}`);
    await new Promise((resolve) => ws.on('open', resolve));

    const replyPromise = new Promise((resolve) => {
      ws.on('message', (raw) => {
        const msg = JSON.parse(raw.toString());
        if (msg.type === 'COMMAND_REPLY' && msg.id === 'req-42') {
          resolve(msg);
        }
      });
    });

    ws.send(JSON.stringify({
      type: 'COMMAND',
      id: 'req-42',
      command: 'SET hello world'
    }));

    const reply = await replyPromise;
    assert.strictEqual(reply.success, true);
    assert.strictEqual(reply.result, 'OK');
    ws.close();
  });

  test('HTTP POST /api/command executes command successfully', async () => {
    const postData = JSON.stringify({ command: 'SET mykey 123' });
    const response = await new Promise((resolve, reject) => {
      const req = http.request(
        {
          host: '127.0.0.1',
          port: bridgePort,
          path: '/api/command',
          method: 'POST',
          headers: {
            'Content-Type': 'application/json',
            'Content-Length': Buffer.byteLength(postData)
          }
        },
        (res) => {
          let body = '';
          res.on('data', (chunk) => (body += chunk));
          res.on('end', () => resolve(JSON.parse(body)));
        }
      );
      req.on('error', reject);
      req.write(postData);
      req.end();
    });

    assert.strictEqual(response.success, true);
    assert.strictEqual(response.result, 'OK');
  });

  test('HTTP GET /api/status returns online state', async () => {
    const status = await new Promise((resolve, reject) => {
      http.get(`http://127.0.0.1:${bridgePort}/api/status`, (res) => {
        let body = '';
        res.on('data', (chunk) => (body += chunk));
        res.on('end', () => resolve(JSON.parse(body)));
      }).on('error', reject);
    });

    assert.strictEqual(status.online, true);
    assert.ok(typeof status.uptime_seconds === 'number');
  });

  test('Broadcasts online: false to WebSocket clients on server disconnect', async () => {
    const ws = new WebSocket(`ws://127.0.0.1:${bridgePort}`);
    await new Promise((resolve) => ws.on('open', resolve));

    const offlinePromise = new Promise((resolve) => {
      ws.on('message', (raw) => {
        const msg = JSON.parse(raw.toString());
        if (msg.type === 'TELEMETRY_UPDATE' && msg.online === false) {
          resolve(msg);
        }
      });
    });

    // Destroy Redis client socket to simulate MiniRedis crash / disconnect
    if (bridge.redis.socket) {
      bridge.redis.socket.destroy();
    }

    const offlineMsg = await offlinePromise;
    assert.strictEqual(offlineMsg.online, false);
    ws.close();
  });
});
