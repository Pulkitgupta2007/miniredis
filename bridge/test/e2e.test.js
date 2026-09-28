// e2e.test.js: End-to-end test connecting real C++ miniredis-server, Node bridge, and WebSocket client
const { test, describe, before, after } = require('node:test');
const assert = require('node:assert');
const { spawn } = require('node:child_process');
const path = require('node:path');
const { WebSocket } = require('ws');
const { createBridgeServer } = require('../src/server');

describe('End-to-End: MiniRedis Server + Bridge + WebSocket', () => {
  let serverProcess;
  let bridge;
  let bridgePort;
  const testRedisPort = 6392;

  before(async () => {
    // 1. Spawn real C++ miniredis-server executable
    const serverBin = path.resolve(__dirname, '../../server/build/miniredis-server');
    serverProcess = spawn(serverBin, [String(testRedisPort), '127.0.0.1'], {
      stdio: ['ignore', 'pipe', 'pipe']
    });

    // Wait for server to bind port
    await new Promise((resolve, reject) => {
      serverProcess.stdout.on('data', (d) => {
        if (d.toString().includes('MiniRedis server listening')) {
          resolve();
        }
      });
      serverProcess.on('error', reject);
      setTimeout(resolve, 300); // fallback
    });

    // 2. Start bridge server connected to real C++ server
    bridge = createBridgeServer({
      redisPort: testRedisPort,
      port: 0,
      pollInterval: 150
    });
    const info = await bridge.start();
    bridgePort = info.port;

    if (!bridge.redis.isConnected) {
      await new Promise((resolve) => bridge.redis.once('connect', resolve));
    }
  });

  after(async () => {
    if (bridge) await bridge.close();
    if (serverProcess) {
      serverProcess.kill('SIGINT');
      await new Promise((resolve) => serverProcess.on('exit', resolve));
    }
  });

  test('Real C++ server, Node.js bridge, and WebSocket proxy work end-to-end', async () => {
    const ws = new WebSocket(`ws://127.0.0.1:${bridgePort}`);
    await new Promise((resolve) => ws.on('open', resolve));

    // Helper to send command over WebSocket and wait for reply
    const sendCommand = (cmd, id) => {
      return new Promise((resolve) => {
        const handler = (raw) => {
          const msg = JSON.parse(raw.toString());
          if (msg.type === 'COMMAND_REPLY' && msg.id === id) {
            ws.off('message', handler);
            resolve(msg);
          }
        };
        ws.on('message', handler);
        ws.send(JSON.stringify({ type: 'COMMAND', id, command: cmd }));
      });
    };

    // 1. Send SET commands through the WebSocket proxy
    const r1 = await sendCommand('SET alpha 100', 'c1');
    assert.strictEqual(r1.success, true);
    assert.strictEqual(r1.result, 'OK');

    const r2 = await sendCommand('SET beta 200', 'c2');
    assert.strictEqual(r2.success, true);
    assert.strictEqual(r2.result, 'OK');

    // 2. GET alpha so alpha becomes MRU (head)
    const r3 = await sendCommand('GET alpha', 'c3');
    assert.strictEqual(r3.success, true);
    assert.strictEqual(r3.result, '100');

    // 3. Await telemetry update and verify LRU order is ['alpha', 'beta']
    const telemetry = await new Promise((resolve) => {
      const handler = (raw) => {
        const msg = JSON.parse(raw.toString());
        if (msg.type === 'TELEMETRY_UPDATE' && msg.online && msg.lru_keys?.length >= 2) {
          ws.off('message', handler);
          resolve(msg);
        }
      };
      ws.on('message', handler);
    });

    assert.strictEqual(telemetry.online, true);
    assert.deepStrictEqual(telemetry.lru_keys, ['alpha', 'beta']);
    assert.ok(telemetry.metrics.used_memory > 0);
    assert.ok(telemetry.metrics.total_commands_processed >= 3);

    ws.close();
  });
});
