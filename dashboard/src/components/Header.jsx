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
          <div className="logo-icon">
            <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <path d="M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5" />
            </svg>
          </div>
          <div>
            <h1 className="header-title">MiniRedis Live Monitor</h1>
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
            🏛️ Systems Architecture
          </button>

          <div
            data-testid="status-badge"
            className={`status-badge ${online ? 'status-online' : 'status-offline'}`}
          >
            <span className="status-dot"></span>
            <span>{online ? 'MINIREDIS ONLINE' : 'SERVER OFFLINE'}</span>
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
