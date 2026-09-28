import React, { useMemo } from 'react';

export function MemoryOpsChart({ history = [] }) {
  // history is array of { timestamp, used_memory, ops_per_sec }
  const width = 600;
  const height = 180;
  const padding = { top: 20, right: 30, bottom: 25, left: 55 };

  const chartData = useMemo(() => {
    if (!history || history.length === 0) {
      return { memPoints: '', opsPoints: '', memArea: '', maxMem: 1024, maxOps: 10, latestMem: 0, latestOps: 0 };
    }

    const maxMemRaw = Math.max(...history.map(d => d.used_memory || 0), 256);
    const maxMem = Math.ceil(maxMemRaw * 1.15); // Add headroom

    const maxOpsRaw = Math.max(...history.map(d => d.ops_per_sec || 0), 5);
    const maxOps = Math.ceil(maxOpsRaw * 1.25);

    const innerW = width - padding.left - padding.right;
    const innerH = height - padding.top - padding.bottom;

    const n = Math.max(history.length - 1, 1);
    const getX = (idx) => padding.left + (idx / n) * innerW;
    const getYMem = (val) => padding.top + innerH - (val / maxMem) * innerH;
    const getYOps = (val) => padding.top + innerH - (val / maxOps) * innerH;

    const memCoords = history.map((d, i) => [getX(i), getYMem(d.used_memory || 0)]);
    const opsCoords = history.map((d, i) => [getX(i), getYOps(d.ops_per_sec || 0)]);

    const memPoints = memCoords.map(p => `${p[0].toFixed(1)},${p[1].toFixed(1)}`).join(' ');
    const opsPoints = opsCoords.map(p => `${p[0].toFixed(1)},${p[1].toFixed(1)}`).join(' ');

    const baselineY = padding.top + innerH;
    const firstX = memCoords[0][0];
    const lastX = memCoords[memCoords.length - 1][0];
    const memArea = `${memPoints} ${lastX},${baselineY} ${firstX},${baselineY}`;

    const latest = history[history.length - 1] || {};

    return {
      memPoints,
      opsPoints,
      memArea,
      maxMem,
      maxOps,
      latestMem: latest.used_memory || 0,
      latestOps: latest.ops_per_sec || 0
    };
  }, [history]);

  const formatBytes = (bytes) => {
    if (bytes < 1024) return `${bytes}B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(0)}KB`;
    return `${(bytes / (1024 * 1024)).toFixed(1)}MB`;
  };

  return (
    <div className="panel-card" data-testid="chart-panel">
      <div className="panel-header">
        <div>
          <h2 className="panel-title">Real-Time Telemetry (Memory &amp; Ops/Sec)</h2>
          <span className="panel-desc">Last 30 polling epochs</span>
        </div>
        <div className="chart-legend">
          <span className="legend-item">
            <span className="legend-dot dot-memory"></span>
            Memory ({formatBytes(chartData.latestMem)})
          </span>
          <span className="legend-item">
            <span className="legend-dot dot-ops"></span>
            Throughput ({chartData.latestOps} ops/s)
          </span>
        </div>
      </div>

      <div className="chart-wrapper">
        <svg viewBox={`0 0 ${width} ${height}`} className="telemetry-svg">
          <defs>
            <linearGradient id="memGradient" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#8b5cf6" stopOpacity="0.45" />
              <stop offset="100%" stopColor="#8b5cf6" stopOpacity="0.02" />
            </linearGradient>
          </defs>

          {/* Grid lines */}
          <line
            x1={padding.left}
            y1={padding.top}
            x2={width - padding.right}
            y2={padding.top}
            stroke="#262626"
            strokeDasharray="3 3"
          />
          <line
            x1={padding.left}
            y1={padding.top + (height - padding.top - padding.bottom) / 2}
            x2={width - padding.right}
            y2={padding.top + (height - padding.top - padding.bottom) / 2}
            stroke="#262626"
            strokeDasharray="3 3"
          />
          <line
            x1={padding.left}
            y1={height - padding.bottom}
            x2={width - padding.right}
            y2={height - padding.bottom}
            stroke="#333333"
          />

          {/* Left Y Axis (Memory) */}
          <text x={padding.left - 8} y={padding.top + 4} fill="#a78bfa" fontSize="10" textAnchor="end">
            {formatBytes(chartData.maxMem)}
          </text>
          <text x={padding.left - 8} y={height - padding.bottom} fill="#71717a" fontSize="10" textAnchor="end">
            0B
          </text>

          {/* Right Y Axis (Ops/sec) */}
          <text x={width - padding.right + 8} y={padding.top + 4} fill="#06b6d4" fontSize="10" textAnchor="start">
            {chartData.maxOps}/s
          </text>
          <text x={width - padding.right + 8} y={height - padding.bottom} fill="#71717a" fontSize="10" textAnchor="start">
            0/s
          </text>

          {/* Memory Fill Area & Polyline */}
          {chartData.memArea && (
            <polygon points={chartData.memArea} fill="url(#memGradient)" />
          )}
          {chartData.memPoints && (
            <polyline
              points={chartData.memPoints}
              fill="none"
              stroke="#8b5cf6"
              strokeWidth="2.5"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
          )}

          {/* Ops/Sec Polyline */}
          {chartData.opsPoints && (
            <polyline
              points={chartData.opsPoints}
              fill="none"
              stroke="#06b6d4"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
              strokeDasharray="4 2"
            />
          )}
        </svg>
      </div>
    </div>
  );
}
