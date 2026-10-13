import React from 'react';

export function MetricCards({ metrics = {} }) {
  const formatBytes = (bytes) => {
    if (!bytes && bytes !== 0) return '0 B';
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
  };

  const usedMem = metrics.used_memory || 0;
  const maxMem = metrics.maxmemory || 0;
  const memPct = maxMem > 0 ? Math.min(100, Math.round((usedMem / maxMem) * 100)) : null;

  const hits = metrics.keyspace_hits || 0;
  const misses = metrics.keyspace_misses || 0;
  const hitRatio = metrics.hit_ratio !== undefined ? Number(metrics.hit_ratio).toFixed(1) : '100.0';
  const ops = metrics.ops_per_sec || 0;
  const totalKeys = metrics.total_keys || 0;
  const evictedKeys = metrics.evicted_keys || 0;
  const expiredKeys = metrics.expired_keys || 0;
  const policy = metrics.maxmemory_policy || 'noeviction';

  return (
    <div className="metrics-grid" data-testid="metrics-grid">
      {/* 1. Ops Per Sec */}
      <div className="metric-card">
        <div className="metric-header">
          <span className="metric-title">Throughput</span>
          <span className="metric-badge">1s window</span>
        </div>
        <div className="metric-value" data-testid="metric-ops">
          <span className="num-tabular">{ops}</span> <span className="metric-unit">ops/sec</span>
        </div>
        <div className="metric-footer">
          <span className="metric-footer-label">Total processed:</span>
          <span className="metric-footer-val num-tabular">{metrics.total_commands_processed || 0}</span>
        </div>
      </div>

      {/* 2. Hit / Miss Ratio */}
      <div className="metric-card">
        <div className="metric-header">
          <span className="metric-title">Hit Ratio</span>
          <span className={`metric-badge ${Number(hitRatio) >= 80 ? 'badge-ok' : 'badge-warn'}`}>
            {hits + misses > 0 ? `${hits} hits / ${misses} misses` : 'No lookups yet'}
          </span>
        </div>
        <div className="metric-value" data-testid="metric-hit-ratio">
          <span className="num-tabular">{hitRatio}</span><span className="metric-unit">%</span>
        </div>
        <div className="metric-footer">
          <div className="ratio-bar-bg" role="progressbar" aria-valuenow={Number(hitRatio)} aria-valuemin="0" aria-valuemax="100">
            <div
              className="ratio-bar-fill"
              style={{ width: `${Math.max(0, Math.min(100, Number(hitRatio)))}%` }}
            ></div>
          </div>
        </div>
      </div>

      {/* 3. Memory Usage */}
      <div className="metric-card">
        <div className="metric-header">
          <span className="metric-title">Memory Allocation</span>
          <span className="metric-badge">
            {maxMem > 0 ? `Cap: ${formatBytes(maxMem)}` : 'Unlimited'}
          </span>
        </div>
        <div className="metric-value" data-testid="metric-memory">
          <span className="num-tabular">{formatBytes(usedMem)}</span>
          {memPct !== null && <span className="metric-sub-pct num-tabular">({memPct}%)</span>}
        </div>
        <div className="metric-footer">
          {maxMem > 0 ? (
            <div className="ratio-bar-bg" role="progressbar" aria-valuenow={memPct} aria-valuemin="0" aria-valuemax="100">
              <div
                className={`ratio-bar-fill ${memPct > 85 ? 'fill-danger' : 'fill-accent'}`}
                style={{ width: `${memPct}%` }}
              ></div>
            </div>
          ) : (
            <span className="metric-subtext">Dynamic heap (64B overhead/key)</span>
          )}
        </div>
      </div>

      {/* 4. Keyspace & Evictions */}
      <div className="metric-card">
        <div className="metric-header">
          <span className="metric-title">Keyspace Lifecycle</span>
          <span className="metric-badge">Policy: {policy}</span>
        </div>
        <div className="metric-value" data-testid="metric-keys">
          <span className="num-tabular">{totalKeys}</span> <span className="metric-unit">keys</span>
        </div>
        <div className="metric-footer split-stats">
          <span>Evicted: <strong className="text-warn num-tabular">{evictedKeys}</strong></span>
          <span>Expired: <strong className="text-lavender num-tabular">{expiredKeys}</strong></span>
        </div>
      </div>
    </div>
  );
}
