// server.js: HTTP & WebSocket Bridge connecting Dashboard to MiniRedis
const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer, WebSocket } = require('ws');
const RedisClient = require('./redis-client');

function createBridgeServer(options = {}) {
  const redisPort = options.redisPort || parseInt(process.env.REDIS_PORT || '6379', 10);
  const redisHost = options.redisHost || process.env.REDIS_HOST || '127.0.0.1';
  const port = options.port !== undefined ? options.port : parseInt(process.env.BRIDGE_PORT || '4000', 10);
  const pollIntervalMs = options.pollInterval || 1000;

  const redis = new RedisClient({
    port: redisPort,
    host: redisHost,
    reconnectInterval: 1500,
    autoReconnect: true
  });

  let state = {
    online: false,
    timestamp: Date.now(),
    metrics: {},
    lru_keys: [],
    lfu_keys: [],
    events: []
  };

  // HTTP server
  const server = http.createServer(async (req, res) => {
    // Add CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
      res.writeHead(204);
      return res.end();
    }

    const parsedUrl = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

    if (req.method === 'GET' && parsedUrl.pathname === '/api/status') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      return res.end(JSON.stringify({
        online: redis.isConnected,
        uptime_seconds: process.uptime(),
        connected_ws_clients: wss.clients.size,
        metrics: state.metrics
      }));
    }

    if (req.method === 'GET' && parsedUrl.pathname === '/api/keys') {
      try {
        const rawKeys = await redis.sendCommand('KEYS *');
        const keyList = Array.isArray(rawKeys) ? rawKeys : [];
        const targetKeys = keyList.slice(0, 100);
        const details = await Promise.all(
          targetKeys.map(async (k) => {
            try {
              const [val, ttl, mem] = await Promise.all([
                redis.sendCommand(`GET ${k}`),
                redis.sendCommand(`TTL ${k}`),
                redis.sendCommand(`MEMORY USAGE ${k}`)
              ]);
              return {
                key: k,
                value: typeof val === 'string' ? val : (val === null ? null : String(val)),
                ttl: typeof ttl === 'number' ? ttl : parseInt(ttl, 10) || -1,
                memory: typeof mem === 'number' ? mem : parseInt(mem, 10) || 0
              };
            } catch {
              return { key: k, value: null, ttl: -2, memory: 0 };
            }
          })
        );
        res.writeHead(200, { 'Content-Type': 'application/json' });
        return res.end(JSON.stringify({ success: true, keys: details, total: keyList.length }));
      } catch (err) {
        res.writeHead(500, { 'Content-Type': 'application/json' });
        return res.end(JSON.stringify({ success: false, error: err.message }));
      }
    }

    if (req.method === 'POST' && parsedUrl.pathname === '/api/command') {
      let body = '';
      req.on('data', chunk => { body += chunk; });
      req.on('end', async () => {
        try {
          const payload = JSON.parse(body || '{}');
          if (!payload.command) {
            res.writeHead(400, { 'Content-Type': 'application/json' });
            return res.end(JSON.stringify({ success: false, error: 'Missing command' }));
          }

          const result = await redis.sendCommand(payload.command);
          // Trigger immediate refresh after mutation
          triggerPoll().catch(() => {});

          res.writeHead(200, { 'Content-Type': 'application/json' });
          return res.end(JSON.stringify({ success: true, result }));
        } catch (err) {
          res.writeHead(500, { 'Content-Type': 'application/json' });
          return res.end(JSON.stringify({ success: false, error: err.message }));
        }
      });
      return;
    }

    // Serve static dashboard dist if exists
    const distPath = path.resolve(__dirname, '../../dashboard/dist');
    if (fs.existsSync(distPath)) {
      let filePath = path.join(distPath, parsedUrl.pathname === '/' ? 'index.html' : parsedUrl.pathname);
      if (!fs.existsSync(filePath)) {
        filePath = path.join(distPath, 'index.html');
      }
      const ext = path.extname(filePath);
      const mimeTypes = {
        '.html': 'text/html',
        '.js': 'application/javascript',
        '.css': 'text/css',
        '.json': 'application/json',
        '.svg': 'image/svg+xml'
      };
      res.writeHead(200, { 'Content-Type': mimeTypes[ext] || 'text/plain' });
      return fs.createReadStream(filePath).pipe(res);
    }

    res.writeHead(404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ error: 'Not Found' }));
  });

  // WebSocket Server
  const wss = new WebSocketServer({ server });

  function broadcast(data) {
    const message = JSON.stringify(data);
    for (const client of wss.clients) {
      if (client.readyState === WebSocket.OPEN) {
        client.send(message);
      }
    }
  }

  async function triggerPoll() {
    if (!redis.isConnected) {
      state.online = false;
      broadcast({
        type: 'TELEMETRY_UPDATE',
        online: false,
        timestamp: Date.now()
      });
      return;
    }

    try {
      const [metrics, lru_keys, lfu_keys, events] = await Promise.all([
        redis.getInfo(),
        redis.getLruKeys(),
        redis.getLfuKeys(),
        redis.getEvents(30)
      ]);

      state = {
        online: true,
        timestamp: Date.now(),
        metrics: metrics || {},
        lru_keys: lru_keys || [],
        lfu_keys: lfu_keys || [],
        events: events || []
      };

      broadcast({
        type: 'TELEMETRY_UPDATE',
        ...state
      });
    } catch (err) {
      // If poll query failed, server might have just dropped
      state.online = false;
      broadcast({
        type: 'TELEMETRY_UPDATE',
        online: false,
        timestamp: Date.now(),
        error: err.message
      });
    }
  }

  let pollTimer = null;
  function startPolling() {
    if (pollTimer) return;
    pollTimer = setInterval(triggerPoll, pollIntervalMs);
  }

  function stopPolling() {
    if (pollTimer) {
      clearInterval(pollTimer);
      pollTimer = null;
    }
  }

  redis.on('connect', () => {
    state.online = true;
    triggerPoll().catch(() => {});
  });

  redis.on('disconnect', () => {
    state.online = false;
    broadcast({
      type: 'TELEMETRY_UPDATE',
      online: false,
      timestamp: Date.now()
    });
  });

  wss.on('connection', (ws) => {
    // Send current state immediately on connection
    ws.send(JSON.stringify({
      type: 'INITIAL_STATE',
      ...state,
      online: redis.isConnected
    }));

    ws.on('message', async (raw) => {
      try {
        const msg = JSON.parse(raw.toString());
        if (msg.type === 'COMMAND' || msg.type === 'EXECUTE') {
          const cmd = msg.command;
          try {
            const result = await redis.sendCommand(cmd);
            ws.send(JSON.stringify({
              type: 'COMMAND_REPLY',
              id: msg.id,
              success: true,
              result
            }));
            // Update telemetry immediately on write/mutation
            triggerPoll().catch(() => {});
          } catch (err) {
            ws.send(JSON.stringify({
              type: 'COMMAND_REPLY',
              id: msg.id,
              success: false,
              error: err.message
            }));
          }
        }
      } catch (err) {
        ws.send(JSON.stringify({
          type: 'ERROR',
          error: 'Invalid JSON payload'
        }));
      }
    });
  });

  return {
    server,
    wss,
    redis,
    start() {
      return new Promise((resolve) => {
        redis.connect();
        startPolling();
        server.listen(port, () => {
          resolve({ port: server.address().port });
        });
      });
    },
    async close() {
      stopPolling();
      redis.close();
      for (const client of wss.clients) {
        client.terminate();
      }
      await new Promise(resolve => wss.close(resolve));
      await new Promise(resolve => server.close(resolve));
    }
  };
}

if (require.main === module) {
  const bridge = createBridgeServer();
  bridge.start().then(({ port }) => {
    console.log(`[MiniRedis Bridge] Running on http://127.0.0.1:${port} (WebSocket ready)`);
    console.log(`[MiniRedis Bridge] Connecting to MiniRedis on ${bridge.redis.host}:${bridge.redis.port}...`);
  });

  process.on('SIGINT', async () => {
    console.log('\n[MiniRedis Bridge] Shutting down...');
    await bridge.close();
    process.exit(0);
  });
}

module.exports = { createBridgeServer };
