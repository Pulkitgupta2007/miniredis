import { useState, useEffect, useRef } from 'react';

export function TrafficPlayground({ onExecute, online, metrics }) {
  // Config state
  const [maxMemoryVal, setMaxMemoryVal] = useState('0');
  const [maxMemoryUnit, setMaxMemoryUnit] = useState('KB');
  const [policy, setPolicy] = useState('noeviction');
  const [configFeedback, setConfigFeedback] = useState(null);
  const [isApplyingConfig, setIsApplyingConfig] = useState(false);

  // Traffic generator state
  const [activeSimulation, setActiveSimulation] = useState(null);
  const [simProgress, setSimProgress] = useState(0);
  const [simStatus, setSimStatus] = useState('');
  const abortSimRef = useRef(false);

  // Sync initial values with server metrics
  useEffect(() => {
    if (metrics.maxmemory_policy) {
      setPolicy(metrics.maxmemory_policy);
    }
    if (metrics.maxmemory !== undefined) {
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

  const setPresetMemory = (val, unit) => {
    setMaxMemoryVal(val);
    setMaxMemoryUnit(unit);
  };

  // Simulations
  const runBurstTraffic = async () => {
    if (!online || activeSimulation) return;
    setActiveSimulation('burst');
    abortSimRef.current = false;
    setSimProgress(0);
    setSimStatus('Bursting 100 rapid SET operations...');

    const total = 100;
    try {
      for (let i = 1; i <= total; i++) {
        if (abortSimRef.current) {
          setSimStatus('Burst cancelled.');
          break;
        }
        await onExecute(`SET burst:key:${i} "val_${Date.now()}_${i}" EX 180`);
        if (i % 5 === 0 || i === total) {
          setSimProgress(Math.round((i / total) * 100));
        }
      }
      if (!abortSimRef.current) {
        setSimStatus('Burst complete! 100 keys created with 180s TTL.');
      }
    } catch (err) {
      setSimStatus(`Error: ${err.message}`);
    } finally {
      setTimeout(() => {
        setActiveSimulation(null);
        setSimProgress(0);
      }, 2500);
    }
  };

  const runEvictionStorm = async () => {
    if (!online || activeSimulation) return;
    setActiveSimulation('storm');
    abortSimRef.current = false;
    setSimProgress(0);
    setSimStatus('Configuring maxmemory to 50KB with allkeys-lru...');

    try {
      // Set maxmemory to 50 KB with LRU
      await onExecute('CONFIG SET maxmemory 51200');
      await onExecute('CONFIG SET maxmemory-policy allkeys-lru');
      setMaxMemoryVal('50');
      setMaxMemoryUnit('KB');
      setPolicy('allkeys-lru');

      setSimStatus('Injecting heavy payloads to force continuous eviction...');
      const payload = 'X'.repeat(1024); // 1 KB payload
      const total = 40;

      for (let i = 1; i <= total; i++) {
        if (abortSimRef.current) {
          setSimStatus('Eviction storm cancelled.');
          break;
        }
        await onExecute(['SET', `storm:payload:${i}`, payload]);
        if (i % 2 === 0 || i === total) {
          setSimProgress(Math.round((i / total) * 100));
        }
      }

      if (!abortSimRef.current) {
        setSimStatus('Eviction storm complete! Check Evictions count & Event Feed.');
      }
    } catch (err) {
      setSimStatus(`Error: ${err.message}`);
    } finally {
      setTimeout(() => {
        setActiveSimulation(null);
        setSimProgress(0);
      }, 2500);
    }
  };

  const handleFlushAll = async () => {
    if (!online || activeSimulation) return;
    if (window.confirm('Are you sure you want to FLUSHALL and clear all keys in MiniRedis?')) {
      try {
        await onExecute('FLUSHALL');
        setSimStatus('FLUSHALL executed: All keys purged.');
        setTimeout(() => setSimStatus(''), 2500);
      } catch (err) {
        setSimStatus(`Error: ${err.message}`);
      }
    }
  };

  const handleStopSimulation = () => {
    abortSimRef.current = true;
  };

  return (
    <div className="traffic-playground-container" data-testid="traffic-playground">
      {/* Cache & Memory Config Panel */}
      <div className="playground-card config-panel">
        <div className="playground-header">
          <div className="playground-title-row">
            <span className="playground-icon">⚙️</span>
            <div>
              <h3 className="playground-title">Cache & Eviction Policy</h3>
              <p className="playground-subtitle">Configure memory limits and eviction algorithms in real-time</p>
            </div>
          </div>
          <span className="current-badge">
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

            <div className="presets-row">
              <span className="preset-label">Presets:</span>
              <button
                type="button"
                onClick={() => setPresetMemory('0', 'KB')}
                className="preset-btn"
                disabled={!online}
              >
                Unlimited
              </button>
              <button
                type="button"
                onClick={() => setPresetMemory('50', 'KB')}
                className="preset-btn"
                disabled={!online}
              >
                50 KB
              </button>
              <button
                type="button"
                onClick={() => setPresetMemory('256', 'KB')}
                className="preset-btn"
                disabled={!online}
              >
                256 KB
              </button>
              <button
                type="button"
                onClick={() => setPresetMemory('1', 'MB')}
                className="preset-btn"
                disabled={!online}
              >
                1 MB
              </button>
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

      {/* Traffic & Chaos Generator Panel */}
      <div className="playground-card traffic-panel">
        <div className="playground-header">
          <div className="playground-title-row">
            <span className="playground-icon">🚦</span>
            <div>
              <h3 className="playground-title">Traffic & Chaos Playground</h3>
              <p className="playground-subtitle">Simulate real-world client traffic, cache misses, and eviction storms</p>
            </div>
          </div>
          {activeSimulation && (
            <button
              type="button"
              onClick={handleStopSimulation}
              className="stop-sim-btn"
            >
              Stop
            </button>
          )}
        </div>

        {activeSimulation && (
          <div className="sim-progress-box">
            <div className="progress-info">
              <span className="sim-status-text">{simStatus}</span>
              <span className="progress-percent">{simProgress}%</span>
            </div>
            <div className="progress-track">
              <div
                className="progress-fill"
                style={{ width: `${simProgress}%` }}
              />
            </div>
          </div>
        )}

        {!activeSimulation && simStatus && (
          <div className="sim-result-banner">{simStatus}</div>
        )}

        <div className="sim-buttons-grid">
          <button
            type="button"
            onClick={runBurstTraffic}
            disabled={!online || Boolean(activeSimulation)}
            className="sim-btn burst-btn"
          >
            <div className="sim-btn-header">
              <span className="sim-btn-name">Burst 100 Keys</span>
            </div>
            <span className="sim-btn-desc">100 rapid SET operations with TTL. Spikes ops/sec and memory.</span>
          </button>

          <button
            type="button"
            onClick={runEvictionStorm}
            disabled={!online || Boolean(activeSimulation)}
            className="sim-btn storm-btn"
          >
            <div className="sim-btn-header">
              <span className="sim-btn-name">Eviction Storm</span>
            </div>
            <span className="sim-btn-desc">Enforces 50KB LRU limit and writes 40 heavy keys to force evictions.</span>
          </button>

          <button
            type="button"
            onClick={handleFlushAll}
            disabled={!online || Boolean(activeSimulation)}
            className="sim-btn flush-btn"
          >
            <div className="sim-btn-header">
              <span className="sim-btn-name">FLUSHALL Keys</span>
            </div>
            <span className="sim-btn-desc">Clears all keys in the database to cleanly reset the store.</span>
          </button>
        </div>
      </div>
    </div>
  );
}

export default TrafficPlayground;
