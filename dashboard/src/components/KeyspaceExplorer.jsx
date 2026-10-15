import React, { useState, useEffect, useCallback } from 'react';
import { formatBytes } from '../utils/format';

function KeyspaceExplorerComponent({ onExecute, online, onSelectKey }) {
  const [keys, setKeys] = useState([]);
  const [loading, setLoading] = useState(false);
  const [searchQuery, setSearchQuery] = useState('');
  const [activeModal, setActiveModal] = useState(null); // 'create' | 'inspect'
  const [inspectKey, setInspectKey] = useState(null);
  const [inspectValue, setInspectValue] = useState('');
  const [inspectTtl, setInspectTtl] = useState('');
  const [inspectMemory, setInspectMemory] = useState(0);

  // New Key Form State
  const [newKey, setNewKey] = useState('');
  const [newValue, setNewValue] = useState('');
  const [newExpiryType, setNewExpiryType] = useState('NONE'); // NONE | EX | PX
  const [newExpiryVal, setNewExpiryVal] = useState('60');
  const [newCondition, setNewCondition] = useState('NONE'); // NONE | NX | XX
  const [modalFeedback, setModalFeedback] = useState(null);

  const fetchKeys = useCallback(async () => {
    if (!online) return;
    setLoading(true);
    try {
      const scanRes = await onExecute('SCAN 0 COUNT 50');
      const keyList = Array.isArray(scanRes) && Array.isArray(scanRes[1])
        ? scanRes[1]
        : (Array.isArray(scanRes) ? scanRes : []);
      const targetKeys = keyList.slice(0, 50);

      const items = await Promise.all(
        targetKeys.map(async (k) => {
          try {
            const [val, ttl, mem] = await Promise.all([
              onExecute(`GET ${k}`).catch(() => null),
              onExecute(`TTL ${k}`).catch(() => -1),
              onExecute(`MEMORY USAGE ${k}`).catch(() => 0)
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
      setKeys(items);
    } catch {
      // Ignore fetch errors
    } finally {
      setLoading(false);
    }
  }, [online, onExecute]);

  // Initial fetch on mount and periodic 5-second sync
  useEffect(() => {
    fetchKeys();
    const interval = setInterval(fetchKeys, 4000);
    return () => clearInterval(interval);
  }, [fetchKeys]);

  // Local 1-second countdown ticker for TTL bars
  useEffect(() => {
    const timer = setInterval(() => {
      setKeys((prev) =>
        prev.map((item) => {
          if (item.ttl > 0) {
            const nextTtl = item.ttl - 1;
            return { ...item, ttl: nextTtl <= 0 ? -2 : nextTtl };
          }
          return item;
        })
      );
    }, 1000);
    return () => clearInterval(timer);
  }, []);

  const handleOpenInspect = (item) => {
    setInspectKey(item.key);
    setInspectValue(item.value !== null ? item.value : '');
    setInspectTtl(item.ttl >= 0 ? item.ttl.toString() : '');
    setInspectMemory(item.memory || 0);
    setModalFeedback(null);
    setActiveModal('inspect');
  };

  const handleSaveInspect = async () => {
    if (!inspectKey) return;
    try {
      await onExecute(['SET', inspectKey, inspectValue]);
      if (inspectTtl !== '') {
        const ttlNum = parseInt(inspectTtl, 10);
        if (!isNaN(ttlNum) && ttlNum > 0) {
          await onExecute(['EXPIRE', inspectKey, String(ttlNum)]);
        } else if (ttlNum <= 0) {
          await onExecute(['PERSIST', inspectKey]);
        }
      }
      setModalFeedback({ type: 'success', message: 'Key updated successfully!' });
      setTimeout(() => {
        setActiveModal(null);
        fetchKeys();
      }, 1000);
    } catch (err) {
      setModalFeedback({ type: 'error', message: err.message || 'Update failed' });
    }
  };

  const handleDeleteKey = async (keyToDelete) => {
    if (!window.confirm(`Delete key "${keyToDelete}"?`)) return;
    try {
      await onExecute(['DEL', keyToDelete]);
      setKeys((prev) => prev.filter((k) => k.key !== keyToDelete));
      if (activeModal === 'inspect' && inspectKey === keyToDelete) {
        setActiveModal(null);
      }
    } catch (err) {
      alert(`Delete error: ${err.message}`);
    }
  };

  const handleCreateKey = async (e) => {
    e.preventDefault();
    if (!newKey.trim()) {
      setModalFeedback({ type: 'error', message: 'Key name is required' });
      return;
    }

    const cmdArgs = ['SET', newKey.trim(), newValue];
    if (newExpiryType === 'EX') {
      const s = parseInt(newExpiryVal, 10) || 60;
      cmdArgs.push('EX', String(s));
    } else if (newExpiryType === 'PX') {
      const ms = parseInt(newExpiryVal, 10) || 60000;
      cmdArgs.push('PX', String(ms));
    }

    if (newCondition === 'NX') cmdArgs.push('NX');
    if (newCondition === 'XX') cmdArgs.push('XX');

    try {
      await onExecute(cmdArgs);
      setModalFeedback({ type: 'success', message: `Created key "${newKey}"!` });
      setTimeout(() => {
        setActiveModal(null);
        setNewKey('');
        setNewValue('');
        fetchKeys();
      }, 1000);
    } catch (err) {
      setModalFeedback({ type: 'error', message: err.message || 'Create failed' });
    }
  };

  const filteredKeys = keys.filter((k) =>
    k.key.toLowerCase().includes(searchQuery.toLowerCase())
  );

  return (
    <div className="keyspace-explorer-container" data-testid="keyspace-explorer">
      {/* Explorer Header */}
      <div className="explorer-header">
        <div className="explorer-title-area">
          <div>
            <h3 className="explorer-title">Keyspace Explorer & Data Browser</h3>
            <p className="explorer-subtitle">Inspect, edit, query, and monitor keys with live TTL countdowns</p>
          </div>
        </div>

        <div className="explorer-actions">
          <div className="search-box">
            <span className="search-icon">🔍</span>
            <input
              type="text"
              placeholder="Search keys..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="search-input"
            />
            {searchQuery && (
              <button
                type="button"
                onClick={() => setSearchQuery('')}
                className="search-clear-btn"
              >
                ×
              </button>
            )}
          </div>

          <button
            type="button"
            onClick={fetchKeys}
            disabled={!online || loading}
            className="refresh-keys-btn"
            title="Refresh keyspace"
          >
            <span className={`refresh-icon ${loading ? 'spinning' : ''}`}>↻</span>
            <span>Refresh</span>
          </button>

          <button
            type="button"
            onClick={() => {
              setModalFeedback(null);
              setActiveModal('create');
            }}
            disabled={!online}
            className="new-key-btn"
          >
            + Add Key
          </button>
        </div>
      </div>

      {/* Keys Table */}
      <div className="keys-table-wrapper">
        <table className="keys-table">
          <thead>
            <tr>
              <th>Key Name</th>
              <th>Value Preview</th>
              <th>Memory Size</th>
              <th>TTL Remaining</th>
              <th>Actions</th>
            </tr>
          </thead>
          <tbody>
            {filteredKeys.length === 0 ? (
              <tr>
                <td colSpan="5" className="empty-keys-cell">
                  {loading ? 'Loading keyspace data...' : (
                    searchQuery ? `No keys matching "${searchQuery}"` : 'No keys in database. Click "+ Add Key" or run Traffic Burst!'
                  )}
                </td>
              </tr>
            ) : (
              filteredKeys.map((item) => {
                const isPersistent = item.ttl === -1;
                const isExpired = item.ttl <= -2;

                return (
                  <tr key={item.key} className="key-row">
                    <td className="key-name-cell">
                      <span
                        className="key-link"
                        onClick={() => onSelectKey ? onSelectKey(item.key) : handleOpenInspect(item)}
                        title="Click to inspect in console"
                      >
                        {item.key}
                      </span>
                    </td>
                    <td className="key-val-cell">
                      <code className="val-snippet">
                        {item.value !== null ? (item.value.length > 36 ? `${item.value.slice(0, 36)}...` : item.value) : 'nil'}
                      </code>
                    </td>
                    <td className="key-mem-cell">
                      <span className="mem-badge">{formatBytes(item.memory)}</span>
                    </td>
                    <td className="key-ttl-cell">
                      {isPersistent ? (
                        <span className="ttl-badge persistent">Persistent (∞)</span>
                      ) : isExpired ? (
                        <span className="ttl-badge expired">Expired</span>
                      ) : (
                        <span className="ttl-text">
                          {item.ttl}s
                        </span>
                      )}
                    </td>
                    <td className="key-actions-cell">
                      <button
                        type="button"
                        onClick={() => handleOpenInspect(item)}
                        className="action-btn inspect-btn"
                        title="Inspect & Edit"
                      >
                        Edit
                      </button>
                      <button
                        type="button"
                        onClick={() => handleDeleteKey(item.key)}
                        className="action-btn delete-btn"
                        title="Delete key"
                      >
                        🗑️
                      </button>
                    </td>
                  </tr>
                );
              })
            )}
          </tbody>
        </table>
      </div>

      {/* Create Key Modal */}
      {activeModal === 'create' && (
        <div className="modal-overlay" onClick={() => setActiveModal(null)}>
          <div className="modal-content" onClick={(e) => e.stopPropagation()}>
            <div className="modal-header">
              <h4>Create New Key</h4>
              <button type="button" onClick={() => setActiveModal(null)} className="modal-close-btn">×</button>
            </div>

            <form onSubmit={handleCreateKey} className="modal-form">
              <div className="modal-form-group">
                <label>Key Name:</label>
                <input
                  type="text"
                  placeholder="e.g. user:profile:100"
                  value={newKey}
                  onChange={(e) => setNewKey(e.target.value)}
                  className="modal-input"
                  required
                />
              </div>

              <div className="modal-form-group">
                <label>Value (Text or JSON):</label>
                <textarea
                  rows="4"
                  placeholder='e.g. {"name": "Alice", "role": "admin"}'
                  value={newValue}
                  onChange={(e) => setNewValue(e.target.value)}
                  className="modal-textarea"
                />
              </div>

              <div className="modal-grid-2">
                <div className="modal-form-group">
                  <label>TTL Expiration:</label>
                  <select
                    value={newExpiryType}
                    onChange={(e) => setNewExpiryType(e.target.value)}
                    className="modal-select"
                  >
                    <option value="NONE">No Expiration (Persistent)</option>
                    <option value="EX">EX (Seconds)</option>
                    <option value="PX">PX (Milliseconds)</option>
                  </select>
                </div>

                {newExpiryType !== 'NONE' && (
                  <div className="modal-form-group">
                    <label>Duration ({newExpiryType === 'EX' ? 'Seconds' : 'ms'}):</label>
                    <input
                      type="number"
                      min="1"
                      value={newExpiryVal}
                      onChange={(e) => setNewExpiryVal(e.target.value)}
                      className="modal-input"
                    />
                  </div>
                )}
              </div>

              <div className="modal-form-group">
                <label>Condition:</label>
                <select
                  value={newCondition}
                  onChange={(e) => setNewCondition(e.target.value)}
                  className="modal-select"
                >
                  <option value="NONE">None (Always overwrite)</option>
                  <option value="NX">NX (Only if key does NOT exist)</option>
                  <option value="XX">XX (Only if key ALREADY exists)</option>
                </select>
              </div>

              {modalFeedback && (
                <div className={`modal-feedback ${modalFeedback.type}`}>
                  {modalFeedback.message}
                </div>
              )}

              <div className="modal-actions">
                <button
                  type="button"
                  onClick={() => setActiveModal(null)}
                  className="modal-cancel-btn"
                >
                  Cancel
                </button>
                <button
                  type="submit"
                  className="modal-submit-btn"
                >
                  Set Key (SET)
                </button>
              </div>
            </form>
          </div>
        </div>
      )}

      {/* Inspect & Edit Modal */}
      {activeModal === 'inspect' && (
        <div className="modal-overlay" onClick={() => setActiveModal(null)}>
          <div className="modal-content" onClick={(e) => e.stopPropagation()}>
            <div className="modal-header">
              <h4>Inspect & Edit: <code>{inspectKey}</code></h4>
              <button type="button" onClick={() => setActiveModal(null)} className="modal-close-btn">×</button>
            </div>

            <div className="modal-meta-row">
              <span className="meta-pill">Memory: <b>{formatBytes(inspectMemory)}</b></span>
              <span className="meta-pill">Length: <b>{inspectValue.length} chars</b></span>
            </div>

            <div className="modal-form">
              <div className="modal-form-group">
                <label>Key Value:</label>
                <textarea
                  rows="6"
                  value={inspectValue}
                  onChange={(e) => setInspectValue(e.target.value)}
                  className="modal-textarea"
                />
              </div>

              <div className="modal-form-group">
                <label>TTL Remaining (seconds, leave empty for no change, 0 to persist):</label>
                <input
                  type="number"
                  placeholder="TTL in seconds (0 = PERSIST)"
                  value={inspectTtl}
                  onChange={(e) => setInspectTtl(e.target.value)}
                  className="modal-input"
                />
              </div>

              {modalFeedback && (
                <div className={`modal-feedback ${modalFeedback.type}`}>
                  {modalFeedback.message}
                </div>
              )}

              <div className="modal-actions space-between">
                <button
                  type="button"
                  onClick={() => handleDeleteKey(inspectKey)}
                  className="modal-danger-btn"
                >
                  Delete Key
                </button>

                <div className="modal-action-buttons">
                  <button
                    type="button"
                    onClick={() => setActiveModal(null)}
                    className="modal-cancel-btn"
                  >
                    Close
                  </button>
                  <button
                    type="button"
                    onClick={handleSaveInspect}
                    className="modal-submit-btn"
                  >
                    Save Changes
                  </button>
                </div>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

export const KeyspaceExplorer = React.memo(KeyspaceExplorerComponent);
