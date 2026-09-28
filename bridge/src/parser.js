// parser.js: RESP2 parsing and telemetry parsing helpers

/**
 * Parses Redis INFO response string into a structured JavaScript object.
 * @param {string} infoStr 
 * @returns {Record<string, string|number>}
 */
function parseInfo(infoStr) {
  if (!infoStr || typeof infoStr !== 'string') return {};
  const lines = infoStr.split(/\r?\n/);
  const result = {};

  for (const line of lines) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) continue;
    const colonIdx = trimmed.indexOf(':');
    if (colonIdx === -1) continue;
    const key = trimmed.slice(0, colonIdx).trim();
    const val = trimmed.slice(colonIdx + 1).trim();

    // Preserve string for version or policy, parse numeric values
    if (!isNaN(val) && val !== '' && !val.includes('.')) {
      result[key] = parseInt(val, 10);
    } else if (!isNaN(val) && val !== '' && val.includes('.')) {
      result[key] = parseFloat(val);
    } else {
      result[key] = val;
    }
  }
  return result;
}

/**
 * Parses events strings returned by MiniRedis EVENTS command into structured objects.
 * Format: "<timestamp_ms>:<TYPE>:<key>:<detail>"
 * @param {string[]} eventLines 
 * @returns {Array<{timestamp: number, type: string, key: string, detail: string}>}
 */
function parseEvents(eventLines) {
  if (!Array.isArray(eventLines)) return [];
  return eventLines.map((line) => {
    if (typeof line !== 'string') return line;
    const firstColon = line.indexOf(':');
    if (firstColon === -1) return { timestamp: Date.now(), type: 'UNKNOWN', key: '', detail: line };

    const secondColon = line.indexOf(':', firstColon + 1);
    if (secondColon === -1) return { timestamp: Number(line.slice(0, firstColon)) || Date.now(), type: line.slice(firstColon + 1), key: '', detail: '' };

    const thirdColon = line.indexOf(':', secondColon + 1);
    if (thirdColon === -1) {
      return {
        timestamp: Number(line.slice(0, firstColon)) || Date.now(),
        type: line.slice(firstColon + 1, secondColon),
        key: line.slice(secondColon + 1),
        detail: ''
      };
    }

    return {
      timestamp: Number(line.slice(0, firstColon)) || Date.now(),
      type: line.slice(firstColon + 1, secondColon),
      key: line.slice(secondColon + 1, thirdColon),
      detail: line.slice(thirdColon + 1)
    };
  });
}

/**
 * Encodes command arguments into a RESP2 Array frame (*<n>\r\n$<len>\r\n...).
 * @param {string[]} args 
 * @returns {string}
 */
function encodeRespCommand(args) {
  if (!Array.isArray(args) || args.length === 0) return '';
  let out = `*${args.length}\r\n`;
  for (const arg of args) {
    const str = String(arg);
    const byteLen = Buffer.byteLength(str, 'utf8');
    out += `$${byteLen}\r\n${str}\r\n`;
  }
  return out;
}

/**
 * Parses a single RESP2 frame from a Buffer starting at `offset`.
 * Returns { value, nextOffset, complete: true } or { complete: false }
 * @param {Buffer} buf 
 * @param {number} offset 
 */
function parseRespFrame(buf, offset = 0) {
  while (offset < buf.length && (buf[offset] === 13 || buf[offset] === 10)) {
    offset++;
  }
  if (offset >= buf.length) return { complete: false };

  const type = String.fromCharCode(buf[offset]);
  const crlf = buf.indexOf('\r\n', offset);
  if (crlf === -1) return { complete: false };

  if (type === '+' || type === '-' || type === ':') {
    const line = buf.toString('utf8', offset + 1, crlf);
    const nextOffset = crlf + 2;
    if (type === '+') return { value: line, nextOffset, complete: true };
    if (type === '-') return { error: line, isError: true, nextOffset, complete: true };
    return { value: parseInt(line, 10), nextOffset, complete: true };
  }

  if (type === '$') {
    const lenStr = buf.toString('utf8', offset + 1, crlf);
    const len = parseInt(lenStr, 10);
    if (len === -1) {
      return { value: null, nextOffset: crlf + 2, complete: true };
    }
    const dataStart = crlf + 2;
    const dataEnd = dataStart + len;
    if (buf.length < dataEnd + 2) return { complete: false }; // wait for data + \r\n
    const value = buf.toString('utf8', dataStart, dataEnd);
    return { value, nextOffset: dataEnd + 2, complete: true };
  }

  if (type === '*') {
    const countStr = buf.toString('utf8', offset + 1, crlf);
    const count = parseInt(countStr, 10);
    if (count === -1) {
      return { value: null, nextOffset: crlf + 2, complete: true };
    }
    const items = [];
    let cur = crlf + 2;
    for (let i = 0; i < count; ++i) {
      const res = parseRespFrame(buf, cur);
      if (!res.complete) return { complete: false };
      items.push(res.isError ? `(error) ${res.error}` : res.value);
      cur = res.nextOffset;
    }
    return { value: items, nextOffset: cur, complete: true };
  }

  // Fallback for unexpected format: consume up to crlf
  const line = buf.toString('utf8', offset, crlf);
  return { value: line, nextOffset: crlf + 2, complete: true };
}

module.exports = {
  parseInfo,
  parseEvents,
  encodeRespCommand,
  parseRespFrame
};
