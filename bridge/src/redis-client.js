// redis-client.js: Minimal, robust TCP RESP client for MiniRedis
const net = require('net');
const EventEmitter = require('events');
const { encodeRespCommand, parseRespFrame, parseInfo, parseEvents } = require('./parser');

class RedisClient extends EventEmitter {
  constructor(options = {}) {
    super();
    this.host = options.host || '127.0.0.1';
    this.port = options.port || 6379;
    this.reconnectInterval = options.reconnectInterval || 1500;
    this.autoReconnect = options.autoReconnect !== false;

    this.socket = null;
    this.isConnected = false;
    this.connecting = false;
    this.closed = false;

    this.buffer = Buffer.alloc(0);
    this.queue = []; // [{ resolve, reject, command }]
    this.reconnectTimer = null;
  }

  connect() {
    if (this.closed || this.connecting || this.isConnected) return;
    this.connecting = true;

    const socket = new net.Socket();
    this.socket = socket;

    socket.connect(this.port, this.host, () => {
      this.connecting = false;
      this.isConnected = true;
      this.buffer = Buffer.alloc(0);
      this.emit('connect');
    });

    socket.on('data', (chunk) => {
      this.buffer = Buffer.concat([this.buffer, chunk]);
      this._processBuffer();
    });

    socket.on('error', (err) => {
      // Handled via close/disconnect
      this.emit('socket_error', err.message);
    });

    socket.on('close', () => {
      const wasConnected = this.isConnected;
      this.isConnected = false;
      this.connecting = false;
      this.socket = null;

      // Reject all pending in-flight commands
      while (this.queue.length > 0) {
        const item = this.queue.shift();
        item.reject(new Error('Connection closed'));
      }

      if (wasConnected) {
        this.emit('disconnect');
      }

      if (this.autoReconnect && !this.closed) {
        this._scheduleReconnect();
      }
    });
  }

  _scheduleReconnect() {
    if (this.reconnectTimer || this.closed) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.closed && !this.isConnected) {
        this.connect();
      }
    }, this.reconnectInterval);
  }

  _processBuffer() {
    let offset = 0;
    while (offset < this.buffer.length) {
      const frame = parseRespFrame(this.buffer, offset);
      if (!frame.complete) {
        break;
      }
      offset = frame.nextOffset;

      if (this.queue.length > 0) {
        const { resolve, reject } = this.queue.shift();
        if (frame.isError) {
          reject(new Error(frame.error));
        } else {
          resolve(frame.value);
        }
      }
    }

    if (offset > 0) {
      this.buffer = this.buffer.slice(offset);
    }
  }

  /**
   * Splits command string respecting quoted arguments.
   * e.g. 'SET "hello world" 123' -> ['SET', 'hello world', '123']
   */
  static splitArgs(input) {
    if (Array.isArray(input)) return input;
    if (typeof input !== 'string') return [String(input)];

    const args = [];
    let current = '';
    let inQuotes = false;
    let quoteChar = '';
    let escaped = false;

    for (let i = 0; i < input.length; i++) {
      const char = input[i];

      if (escaped) {
        current += char;
        escaped = false;
        continue;
      }

      if (char === '\\') {
        escaped = true;
        continue;
      }

      if (inQuotes) {
        if (char === quoteChar) {
          inQuotes = false;
        } else {
          current += char;
        }
      } else {
        if (char === '"' || char === "'") {
          inQuotes = true;
          quoteChar = char;
        } else if (/\s/.test(char)) {
          if (current.length > 0) {
            args.push(current);
            current = '';
          }
        } else {
          current += char;
        }
      }
    }

    if (current.length > 0) {
      args.push(current);
    }
    return args;
  }

  sendCommand(commandOrArgs) {
    return new Promise((resolve, reject) => {
      if (!this.isConnected || !this.socket) {
        return reject(new Error('Server offline'));
      }

      const args = RedisClient.splitArgs(commandOrArgs);
      if (args.length === 0) {
        return reject(new Error('Empty command'));
      }

      const payload = encodeRespCommand(args);
      this.queue.push({ resolve, reject, command: args });
      this.socket.write(payload);
    });
  }

  async getInfo() {
    const raw = await this.sendCommand(['INFO']);
    return parseInfo(raw);
  }

  async getLruKeys() {
    try {
      const raw = await this.sendCommand(['LRUKEYS']);
      return Array.isArray(raw) ? raw : [];
    } catch {
      return [];
    }
  }

  async getLfuKeys() {
    try {
      const raw = await this.sendCommand(['LFUKEYS']);
      return Array.isArray(raw) ? raw : [];
    } catch {
      return [];
    }
  }

  async getEvents(limit = 20) {
    try {
      const raw = await this.sendCommand(['EVENTS', String(limit)]);
      return parseEvents(raw);
    } catch {
      return [];
    }
  }

  close() {
    this.closed = true;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
    this.isConnected = false;
  }
}

module.exports = RedisClient;
