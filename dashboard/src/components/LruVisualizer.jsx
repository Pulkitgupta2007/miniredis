import React from 'react';

export function LruVisualizer({ keys = [], onSelectKey }) {
  return (
    <div className="panel-card" data-testid="lru-panel">
      <div className="panel-header">
        <div>
          <h2 className="panel-title">LRU Doubly Linked List Order</h2>
          <span className="panel-desc">
            Head (MRU) &rarr; Tail (LRU victim). Re-ordered in O(1) on every GET/SET.
          </span>
        </div>
        <span className="metric-tag tag-indigo">
          {keys.length} tracked {keys.length === 1 ? 'key' : 'keys'}
        </span>
      </div>

      {keys.length === 0 ? (
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
                <React.Fragment key={key}>
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
                </React.Fragment>
              );
            })}
          </div>
        </div>
      )}
    </div>
  );
}
