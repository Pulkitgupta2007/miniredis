import React, { useState } from 'react';
import { ArchitectureGuideModal } from './ArchitectureGuideModal';

export function Header({ online, uptimeSeconds, connectedClients, lastUpdated }) {
  const [showGuide, setShowGuide] = useState(false);

  const formatUptime = (sec) => {
    if (!sec && sec !== 0) return '0s';
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = Math.floor(sec % 60);
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m ${s}s`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  };

  return (
    <>
      <header className="header-container" data-testid="header">
        <div className="header-brand">
          <div className="logo-icon" aria-hidden="true">
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2.2" strokeLinecap="round" strokeLinejoin="round">
              <ellipse cx="12" cy="5" rx="9" ry="3"></ellipse>
              <path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"></path>
              <path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"></path>
            </svg>
          </div>
          <div className="header-titles">
            <div className="header-title-row">
              <h1 className="header-title">MiniRedis Console</h1>
              <span className="instance-pill">engine: v1.0 • port 6379</span>
            </div>
            <span className="header-subtitle">In-Memory Engine &amp; Event Loop Telemetry</span>
          </div>
        </div>

        <div className="header-status-group">
          <button
            type="button"
            className="guide-btn"
            onClick={() => setShowGuide(true)}
            title="Read Systems Architecture Guide"
          >
            <svg width="13" height="13" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
              <rect x="2" y="2" width="20" height="8" rx="2" ry="2"></rect>
              <rect x="2" y="14" width="20" height="8" rx="2" ry="2"></rect>
              <line x1="6" y1="6" x2="6.01" y2="6"></line>
              <line x1="6" y1="18" x2="6.01" y2="18"></line>
            </svg>
            <span>Systems Architecture</span>
          </button>

          <div
            data-testid="status-badge"
            className={`status-badge ${online ? 'status-online' : 'status-offline'}`}
          >
            <span className="status-dot"></span>
            <span className="status-text">{online ? 'MINIREDIS ONLINE' : 'SERVER OFFLINE'}</span>
          </div>

          {online ? (
            <div className="header-meta">
              <span className="meta-pill">Uptime: <strong>{formatUptime(uptimeSeconds)}</strong></span>
              <span className="meta-pill">Clients: <strong>{connectedClients || 1}</strong></span>
            </div>
          ) : (
            <div className="header-meta">
              <span className="meta-pill meta-warning">Reconnecting to TCP bridge...</span>
            </div>
          )}
        </div>
      </header>

      <ArchitectureGuideModal
        isOpen={showGuide}
        onClose={() => setShowGuide(false)}
      />
    </>
  );
}

export default Header;
