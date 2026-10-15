/**
 * Formats a raw byte count into a human-readable string (B, KB, MB).
 * @param {number} bytes
 * @param {boolean} [space=true] - Whether to include a space before the unit.
 * @returns {string}
 */
export function formatBytes(bytes, space = true) {
  if (!bytes && bytes !== 0) return space ? '0 B' : '0B';
  const s = space ? ' ' : '';
  if (bytes < 1024) return `${bytes}${s}B`;
  if (bytes < 1048576) return `${(bytes / 1024).toFixed(1)}${s}KB`;
  return `${(bytes / 1048576).toFixed(2)}${s}MB`;
}
