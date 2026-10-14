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
          <span className="metric-subtext">1s sliding window</span>
        </div>
        <div className="metric-value" data-testid="metric-ops">
          {ops} <span className="metric-unit">ops/sec</span>
        </div>
        <div className="metric-footer">
          Total Processed: <strong>{metrics.total_commands_processed || 0}</strong>
        </div>
      </div>

      {/* 3. Memory Usage */}
      <div className="metric-card">
        <div className="metric-header">
          <span className="metric-title">Memory Allocation</span>
          <span className="metric-subtext">
            {maxMem > 0 ? `Cap: ${formatBytes(maxMem)}` : 'Unlimited'}
          </span>
        </div>
        <div className="metric-value" data-testid="metric-memory">
          {formatBytes(usedMem)}
          {memPct !== null && <span className="metric-sub-pct">({memPct}%)</span>}
        </div>
        <div className="metric-footer">
          {maxMem > 0 ? (
            <div className="ratio-bar-bg">
              <div
                className={`ratio-bar-fill ${memPct > 85 ? 'fill-danger' : 'fill-purple'}`}
                style={{ width: `${memPct}%` }}
              ></div>
            </div>
          ) : (
            <span>Dynamic heap accounting (64B overhead/key)</span>
          )}
        </div>
      </div>

      {/* 4. Keyspace & Evictions */}
      <div className="metric-card">
        <div className="metric-header">
          <span className="metric-title">Keyspace Lifecycle</span>
          <span className="metric-subtext">Policy: {policy}</span>
        </div>
        <div className="metric-value" data-testid="metric-keys">
          {totalKeys} <span className="metric-unit">keys</span>
        </div>
        <div className="metric-footer split-stats">
          <span>Evicted: <strong className="text-amber">{evictedKeys}</strong></span>
          <span>Expired: <strong className="text-purple">{expiredKeys}</strong></span>
        </div>
      </div>
    </div>
  );
}
