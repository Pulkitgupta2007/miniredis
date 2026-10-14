import { useState, useMemo, Fragment } from 'react';

export function LruVisualizer({ keys = [], lfuKeys = [], onSelectKey }) {
  const [viewMode, setViewMode] = useState('lru'); // 'lru' | 'lfu'

  // Parse LFU keys formatted as "key:freq"
  const lfuBuckets = useMemo(() => {
    const buckets = {};
    for (const item of lfuKeys) {
      if (typeof item !== 'string') continue;
      const lastColon = item.lastIndexOf(':');
      if (lastColon === -1) {
        const freq = 1;
        if (!buckets[freq]) buckets[freq] = [];
        buckets[freq].push(item);
      } else {
        const key = item.slice(0, lastColon);
        const freq = parseInt(item.slice(lastColon + 1), 10) || 1;
        if (!buckets[freq]) buckets[freq] = [];
        buckets[freq].push(key);
      }
    }
    return buckets;
  }, [lfuKeys]);

  const sortedFrequencies = Object.keys(lfuBuckets)
    .map(Number)
    .sort((a, b) => a - b);

  const minFreq = sortedFrequencies.length > 0 ? sortedFrequencies[0] : 0;
  const totalLfuKeys = Object.values(lfuBuckets).reduce((acc, arr) => acc + arr.length, 0);

  return (
    <div className="panel-card" data-testid="lru-panel">
      <div className="panel-header">
        <div>
          <div className="visualizer-title-row">
            <h2 className="panel-title">
              {viewMode === 'lru' ? 'LRU Doubly Linked List Order' : 'LFU Frequency Buckets (O(1) Buckets)'}
            </h2>
            <div className="mode-toggle-pills">
              <button
                type="button"
                className={`mode-pill ${viewMode === 'lru' ? 'active' : ''}`}
                onClick={() => setViewMode('lru')}
              >
                LRU List
              </button>
              <button
                type="button"
                className={`mode-pill ${viewMode === 'lfu' ? 'active' : ''}`}
                onClick={() => setViewMode('lfu')}
              >
                LFU Buckets
              </button>
            </div>
          </div>
          <span className="panel-desc">
            {viewMode === 'lru'
              ? 'Head (MRU) → Tail (LRU victim). Re-ordered in O(1) on every GET/SET.'
              : 'Keys grouped by access count. min_freq pointer points to lowest bucket; LRU tie-breaking within buckets.'}
          </span>
        </div>
        <span className="panel-subtext">
          {viewMode === 'lru'
            ? `${keys.length} tracked ${keys.length === 1 ? 'key' : 'keys'}`
            : `${totalLfuKeys} tracked ${totalLfuKeys === 1 ? 'key' : 'keys'}`}
        </span>
      </div>

      {viewMode === 'lru' ? (
        /* LRU View */
        keys.length === 0 ? (
          <div className="empty-state" data-testid="lru-empty">
            <div className="empty-icon">
              <svg width="32" height="32" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.5">
                <path d="M4 6h16M4 12h16M4 18h16" />
              </svg>
            </div>
            <p className="empty-text">No active keys in storage.</p>
            <span className="empty-subtext">Use the command console below to insert keys (e.g. <code>SET user:1 John</code>).</span>
          </div>
        ) : (
          <div className="lru-track" data-testid="lru-track">
            <div className="lru-nodes-scroll">
              {keys.map((key, index) => {
                const isMru = index === 0;
                const isLru = index === keys.length - 1 && keys.length > 1;

                return (
                  <Fragment key={key}>
                    <div
                      className={`lru-node ${isMru ? 'node-mru' : ''} ${isLru ? 'node-lru' : ''}`}
                      onClick={() => onSelectKey && onSelectKey(key)}
                      title={`Click to inspect key "${key}"`}
                      data-testid={`lru-node-${key}`}
                    >
                      <div className="node-badge">
                        {isMru ? (
                          <span className="badge-tag tag-green">MRU (Head)</span>
                        ) : isLru ? (
                          <span className="badge-tag tag-amber">LRU (Victim)</span>
                        ) : (
                          <span className="badge-tag tag-muted">#{index + 1}</span>
                        )}
                      </div>
                      <div className="node-key">{key}</div>
                      <div className="node-action">Click to inspect</div>
                    </div>

                    {index < keys.length - 1 && (
                      <div className="lru-connector">
                        <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                          <path d="M5 12h14M12 5l7 7-7 7" />
                        </svg>
                      </div>
                    )}
                  </Fragment>
                );
              })}
            </div>
          </div>
        )
      ) : (
        /* LFU View */
        sortedFrequencies.length === 0 ? (
          <div className="empty-state" data-testid="lfu-empty">
            <div className="empty-icon">📊</div>
            <p className="empty-text">No keys tracked in LFU engine.</p>
            <span className="empty-subtext">Add keys or run simulations to see frequency distribution.</span>
          </div>
        ) : (
          <div className="lfu-buckets-grid" data-testid="lfu-buckets">
            {sortedFrequencies.map((freq) => {
              const bucketKeys = lfuBuckets[freq] || [];
              const isMinFreq = freq === minFreq;

              return (
                <div
                  key={freq}
                  className={`lfu-bucket-column ${isMinFreq ? 'is-min-freq' : ''}`}
                  data-testid={`lfu-bucket-${freq}`}
                >
                  <div className="bucket-header">
                    <div className="bucket-freq-badge">
                      <span className="freq-count">Freq = {freq}</span>
                      {isMinFreq && (
                        <span className="min-freq-tag">min_freq 🎯</span>
                      )}
                    </div>
                    <span className="bucket-count">{bucketKeys.length} {bucketKeys.length === 1 ? 'key' : 'keys'}</span>
                  </div>

                  <div className="bucket-keys-list">
                    {bucketKeys.map((k, idx) => {
                      const isVictimCandidate = isMinFreq && idx === bucketKeys.length - 1;

                      return (
                        <div
                          key={k}
                          className={`lfu-key-chip ${isVictimCandidate ? 'lfu-victim' : ''}`}
                          onClick={() => onSelectKey && onSelectKey(k)}
                          title={`Key: ${k} (Frequency: ${freq})${isVictimCandidate ? ' - Next LFU Victim' : ''}`}
                        >
                          <span className="chip-key-name">{k}</span>
                          {isVictimCandidate ? (
                            <span className="chip-badge victim">Next Victim</span>
                          ) : (
                            <span className="chip-badge">{freq}×</span>
                          )}
                        </div>
                      );
                    })}
                  </div>
                </div>
              );
            })}
          </div>
        )
      )}
    </div>
  );
}

export default LruVisualizer;
