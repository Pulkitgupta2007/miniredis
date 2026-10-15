import React, { useState, useEffect, useRef } from 'react';

function TrafficPlaygroundComponent({ onExecute, online, metrics }) {
  // Config state
  const [maxMemoryVal, setMaxMemoryVal] = useState('0');
  const [maxMemoryUnit, setMaxMemoryUnit] = useState('KB');
  const [policy, setPolicy] = useState('noeviction');
  const [configFeedback, setConfigFeedback] = useState(null);
  const [isApplyingConfig, setIsApplyingConfig] = useState(false);
  const isInputFocusedRef = useRef(false);

  // Admin actions state
  const [adminActionLoading, setAdminActionLoading] = useState(null);

  // Sync initial values with server metrics
  useEffect(() => {
    if (metrics.maxmemory_policy) {
      setPolicy(metrics.maxmemory_policy);
    }
    if (!isInputFocusedRef.current && metrics.maxmemory !== undefined) {
      const bytes = metrics.maxmemory;
      if (bytes === 0) {
        setMaxMemoryVal('0');
      } else if (bytes >= 1048576 && bytes % 1048576 === 0) {
        setMaxMemoryVal((bytes / 1048576).toString());
        setMaxMemoryUnit('MB');
      } else if (bytes >= 1024) {
        setMaxMemoryVal((bytes / 1024).toString());
        setMaxMemoryUnit('KB');
      } else {
        setMaxMemoryVal(bytes.toString());
        setMaxMemoryUnit('Bytes');
      }
    }
  }, [metrics.maxmemory, metrics.maxmemory_policy]);

  const handleApplyConfig = async () => {
    if (!online || isApplyingConfig) return;
    setIsApplyingConfig(true);
    setConfigFeedback(null);

    try {
      let bytes = parseInt(maxMemoryVal, 10);
      if (isNaN(bytes) || bytes < 0) bytes = 0;
      if (maxMemoryUnit === 'KB') bytes *= 1024;
      if (maxMemoryUnit === 'MB') bytes *= 1024 * 1024;

      await onExecute(`CONFIG SET maxmemory ${bytes}`);
      await onExecute(`CONFIG SET maxmemory-policy ${policy}`);

      const formatted = bytes === 0 ? 'Unlimited' : `${maxMemoryVal} ${maxMemoryUnit}`;
      setConfigFeedback({ type: 'success', message: `Saved: maxmemory=${formatted}, policy=${policy}` });
    } catch (err) {
      setConfigFeedback({ type: 'error', message: err.message || 'Failed to update config' });
    } finally {
      setIsApplyingConfig(false);
      setTimeout(() => setConfigFeedback(null), 4000);
    }
  };

  // Administrative Operations
  const handleFlushAll = async () => {
    if (!online || adminActionLoading) return;
    setAdminActionLoading('flush');
    try {
      await onExecute('FLUSHALL');
      setConfigFeedback({ type: 'success', message: 'FLUSHALL executed: all keys cleared' });
    } catch (err) {
      setConfigFeedback({ type: 'error', message: err.message || 'FLUSHALL failed' });
    } finally {
      setAdminActionLoading(null);
      setTimeout(() => setConfigFeedback(null), 3000);
    }
  };

  const handleBgRewriteAof = async () => {
    if (!online || adminActionLoading) return;
    setAdminActionLoading('aof');
    try {
      const res = await onExecute('BGREWRITEAOF');
      setConfigFeedback({ type: 'success', message: typeof res === 'string' ? res : 'BGREWRITEAOF started' });
    } catch (err) {
      setConfigFeedback({ type: 'error', message: err.message || 'BGREWRITEAOF failed' });
    } finally {
      setAdminActionLoading(null);
      setTimeout(() => setConfigFeedback(null), 3000);
    }
  };

  return (
    <div className="traffic-playground-container" data-testid="traffic-playground">
      {/* Cache & Memory Config Panel */}
      <div className="playground-card config-panel">
        <div className="playground-header">
          <div className="playground-title-row">
            <div>
              <h3 className="playground-title">Cache & Eviction Policy</h3>
              <p className="playground-subtitle">Configure memory limits and eviction algorithms in real-time</p>
            </div>
          </div>
          <span className="current-policy-text">
            Active: {metrics.maxmemory_policy || 'noeviction'}
          </span>
        </div>

        <div className="config-form">
          <div className="form-group">
            <label className="form-label">
              <span>Max Memory:</span>
              <span className="label-helper">
                {metrics.maxmemory ? `${(metrics.maxmemory / 1024).toFixed(1)} KB currently allocated` : 'Currently Unlimited'}
              </span>
            </label>

            <div className="input-with-unit">
              <input
                type="number"
                min="0"
                value={maxMemoryVal}
                onChange={(e) => setMaxMemoryVal(e.target.value)}
                onFocus={() => { isInputFocusedRef.current = true; }}
                onBlur={() => { isInputFocusedRef.current = false; }}
                disabled={!online || isApplyingConfig}
                className="config-input"
                placeholder="0 = unlimited"
              />
              <select
                value={maxMemoryUnit}
                onChange={(e) => setMaxMemoryUnit(e.target.value)}
                disabled={!online || isApplyingConfig}
                className="config-select unit-select"
              >
                <option value="KB">KB</option>
                <option value="MB">MB</option>
                <option value="Bytes">Bytes</option>
              </select>
            </div>
          </div>

          <div className="form-group">
            <label className="form-label">Eviction Policy:</label>
            <div className="policy-pill-group">
              <label className={`policy-pill ${policy === 'noeviction' ? 'active' : ''}`}>
                <input
                  type="radio"
                  name="policy"
                  value="noeviction"
                  checked={policy === 'noeviction'}
                  onChange={(e) => setPolicy(e.target.value)}
                  disabled={!online}
                />
                <span className="pill-title">noeviction</span>
                <span className="pill-desc">Return -OOM on write</span>
              </label>

              <label className={`policy-pill ${policy === 'allkeys-lru' ? 'active' : ''}`}>
                <input
                  type="radio"
                  name="policy"
                  value="allkeys-lru"
                  checked={policy === 'allkeys-lru'}
                  onChange={(e) => setPolicy(e.target.value)}
                  disabled={!online}
                />
                <span className="pill-title">allkeys-lru</span>
                <span className="pill-desc">Exact O(1) LRU list</span>
              </label>

              <label className={`policy-pill ${policy === 'allkeys-lfu' ? 'active' : ''}`}>
                <input
                  type="radio"
                  name="policy"
                  value="allkeys-lfu"
                  checked={policy === 'allkeys-lfu'}
                  onChange={(e) => setPolicy(e.target.value)}
                  disabled={!online}
                />
                <span className="pill-title">allkeys-lfu</span>
                <span className="pill-desc">O(1) Frequency buckets</span>
              </label>
            </div>
          </div>

          <div className="form-footer">
            <button
              type="button"
              onClick={handleApplyConfig}
              disabled={!online || isApplyingConfig}
              className="apply-btn"
            >
              {isApplyingConfig ? 'Applying...' : 'Apply CONFIG SET'}
            </button>

            {configFeedback && (
              <span className={`feedback-badge ${configFeedback.type}`}>
                {configFeedback.message}
              </span>
            )}
          </div>
        </div>
      </div>

      {/* Database Maintenance & Management Panel */}
      <div className="playground-card traffic-panel">
        <div className="playground-header">
          <div className="playground-title-row">
            <div>
              <h3 className="playground-title">Database Maintenance &amp; Actions</h3>
              <p className="playground-subtitle">Execute administrative commands and manage engine keyspace</p>
            </div>
          </div>
        </div>

        <div className="sim-buttons-grid">
          <button
            type="button"
            onClick={handleFlushAll}
            disabled={!online || Boolean(adminActionLoading)}
            className="sim-btn flush-btn"
          >
            <div className="sim-btn-header">
              <span className="sim-btn-name">
                {adminActionLoading === 'flush' ? 'Flushing Keys...' : 'FLUSHALL Keys'}
              </span>
            </div>
            <span className="sim-btn-desc">Clears all keys in the database to cleanly reset the store.</span>
          </button>

          <button
            type="button"
            onClick={handleBgRewriteAof}
            disabled={!online || Boolean(adminActionLoading)}
            className="sim-btn flush-btn"
          >
            <div className="sim-btn-header">
              <span className="sim-btn-name">
                {adminActionLoading === 'aof' ? 'Rewriting AOF...' : 'BGREWRITEAOF'}
              </span>
            </div>
            <span className="sim-btn-desc">Compacts the Append-Only File into a minimal representation.</span>
          </button>
        </div>
      </div>
    </div>
  );
}

export const TrafficPlayground = React.memo(TrafficPlaygroundComponent);
