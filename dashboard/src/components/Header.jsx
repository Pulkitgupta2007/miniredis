export function Header({ online, uptimeSeconds, connectedClients }) {
  const formatUptime = (sec) => {
    if (!sec && sec !== 0) return '0s';
    const d = Math.floor(sec / 86400);
    const h = Math.floor((sec % 86400) / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = Math.floor(sec % 60);
    if (d > 0) return `${d}d ${h}m`;
    if (h > 0) return `${h}m ${s}s`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  };

  return (
    <header className="header-container" data-testid="header">
      <div className="header-brand">
        <div>
          <h1 className="header-title">MiniRedis Live Monitor</h1>
          <span className="header-subtitle">In-Memory Engine &amp; Event Loop Telemetry</span>
        </div>
      </div>

      <div className="header-status-group">
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
  );
}

export default Header;
