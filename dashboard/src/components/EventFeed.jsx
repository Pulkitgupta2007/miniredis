import React, { useState } from 'react';

export function EventFeed({ events = [] }) {
  const [filter, setFilter] = useState('ALL');

  const filteredEvents = events.filter((ev) => {
    if (filter === 'ALL') return true;
    return ev.type === filter;
  });

  const formatTime = (ts) => {
    if (!ts) return '';
    const date = new Date(ts);
    return date.toTimeString().split(' ')[0] + '.' + String(date.getMilliseconds()).padStart(3, '0');
  };

  return (
    <div className="panel-card" data-testid="event-feed-panel">
      <div className="panel-header">
        <div>
          <h2 className="panel-title">Eviction &amp; Expiry Feed</h2>
          <span className="panel-desc">Real-time telemetry stream from ring buffer</span>
        </div>
        <div className="filter-pill-group">
          <button
            className={`filter-btn ${filter === 'ALL' ? 'active' : ''}`}
            onClick={() => setFilter('ALL')}
          >
            All ({events.length})
          </button>
          <button
            className={`filter-btn ${filter === 'EVICTION' ? 'active' : ''}`}
            onClick={() => setFilter('EVICTION')}
          >
            Evictions
          </button>
          <button
            className={`filter-btn ${filter === 'EXPIRATION' ? 'active' : ''}`}
            onClick={() => setFilter('EXPIRATION')}
          >
            Expirations
          </button>
        </div>
      </div>

      <div className="event-list" data-testid="event-list">
        {filteredEvents.length === 0 ? (
          <div className="empty-state">
            <p className="empty-text">No lifecycle events recorded yet.</p>
            <span className="empty-subtext">Keys evicted via maxmemory or expired via TTL will appear here live.</span>
          </div>
        ) : (
          filteredEvents.map((ev, i) => {
            const isEviction = ev.type === 'EVICTION';
            return (
              <div key={i} className="event-row" data-testid="event-row">
                <span className="event-time">{formatTime(ev.timestamp)}</span>
                <span className={`event-badge ${isEviction ? 'badge-eviction' : 'badge-expiration'}`}>
                  {ev.type}
                </span>
                <span className="event-key">{ev.key}</span>
                <span className="event-detail">{ev.detail}</span>
              </div>
            );
          })
        )}
      </div>
    </div>
  );
}
