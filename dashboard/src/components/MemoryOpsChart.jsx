import React, { useMemo, useState, useRef } from 'react';

export function MemoryOpsChart({ history = [] }) {
  // history is array of { timestamp, used_memory, ops_per_sec }
  const width = 600;
  const height = 180;
  const padding = { top: 22, right: 45, bottom: 25, left: 55 };
  const svgRef = useRef(null);
  const [hoverIndex, setHoverIndex] = useState(null);

  const displayHistory = useMemo(() => {
    if (!history || history.length === 0) return [];
    if (history.length === 1) return [history[0], history[0]];
    return history;
  }, [history]);

  const chartData = useMemo(() => {
    if (!displayHistory || displayHistory.length === 0) {
      return {
        memPoints: '',
        opsPoints: '',
        memArea: '',
        maxMem: 1024,
        maxOps: 10,
        peakMem: 0,
        peakOps: 0,
        latestMem: 0,
        latestOps: 0,
        memCoords: [],
        opsCoords: []
      };
    }

    const peakMem = Math.max(...displayHistory.map((d) => d.used_memory || 0), 0);
    const maxMem = Math.max(Math.ceil(peakMem * 1.15), 1024); // Add 15% headroom, min 1KB

    const peakOps = Math.max(...displayHistory.map((d) => d.ops_per_sec || 0), 0);
    const maxOps = Math.max(Math.ceil(peakOps * 1.25), 10); // Add 25% headroom, min 10 ops/s

    const innerW = width - padding.left - padding.right;
    const innerH = height - padding.top - padding.bottom;
    const baselineY = padding.top + innerH;

    const n = Math.max(displayHistory.length - 1, 1);
    const getX = (idx) => padding.left + (idx / n) * innerW;
    const getYMem = (val) => {
      const v = Math.max(0, val || 0);
      const ratio = maxMem > 0 ? v / maxMem : 0;
      return Math.max(padding.top, Math.min(baselineY, baselineY - ratio * innerH));
    };
    const getYOps = (val) => {
      const v = Math.max(0, val || 0);
      const ratio = maxOps > 0 ? v / maxOps : 0;
      return Math.max(padding.top, Math.min(baselineY, baselineY - ratio * innerH));
    };

    const memCoords = displayHistory.map((d, i) => [getX(i), getYMem(d.used_memory || 0)]);
    const opsCoords = displayHistory.map((d, i) => [getX(i), getYOps(d.ops_per_sec || 0)]);

    const memPoints = memCoords.map((p) => `${p[0].toFixed(1)},${p[1].toFixed(1)}`).join(' ');
    const opsPoints = opsCoords.map((p) => `${p[0].toFixed(1)},${p[1].toFixed(1)}`).join(' ');

    const firstX = memCoords[0][0];
    const lastX = memCoords[memCoords.length - 1][0];
    const memArea = `${memPoints} ${lastX},${baselineY} ${firstX},${baselineY}`;

    const latest = displayHistory[displayHistory.length - 1] || {};

    return {
      memPoints,
      opsPoints,
      memArea,
      maxMem,
      maxOps,
      peakMem,
      peakOps,
      latestMem: latest.used_memory || 0,
      latestOps: latest.ops_per_sec || 0,
      memCoords,
      opsCoords
    };
  }, [displayHistory]);

  const formatBytes = (bytes) => {
    if (!bytes || bytes <= 0) return '0B';
    if (bytes < 1024) return `${bytes}B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)}KB`;
    return `${(bytes / (1024 * 1024)).toFixed(2)}MB`;
  };

  const handleMouseMove = (e) => {
    if (!svgRef.current || displayHistory.length === 0) return;
    const rect = svgRef.current.getBoundingClientRect();
    if (rect.width <= 0) return;
    const clientX = e.clientX - rect.left;
    const svgX = (clientX / rect.width) * width;
    const innerW = width - padding.left - padding.right;
    const relX = Math.max(0, Math.min(innerW, svgX - padding.left));
    const idx = Math.round((relX / innerW) * (displayHistory.length - 1));
    setHoverIndex(idx);
  };

  const handleMouseLeave = () => {
    setHoverIndex(null);
  };

  const hoveredItem = hoverIndex !== null && displayHistory[hoverIndex] ? displayHistory[hoverIndex] : null;

  return (
    <div className="panel-card" data-testid="chart-panel">
      <div className="panel-header">
        <div>
          <h2 className="panel-title">Real-Time Telemetry (Memory &amp; Ops/Sec)</h2>
          <span className="panel-desc">Adaptive time-series window (Last {displayHistory.length} epochs)</span>
        </div>
        <div className="chart-legend">
          <span className="legend-item">
            <span className="legend-dot dot-memory"></span>
            Memory: <b>{formatBytes(chartData.latestMem)}</b>
            <span className="chart-legend-badge">(peak: {formatBytes(chartData.peakMem)})</span>
          </span>
          <span className="legend-item">
            <span className="legend-dot dot-ops"></span>
            Throughput: <b>{chartData.latestOps} ops/s</b>
            <span className="chart-legend-badge">(peak: {chartData.peakOps}/s)</span>
          </span>
        </div>
      </div>

      <div className="chart-wrapper">
        {hoveredItem && (
          <div className="chart-tooltip">
            <span>Epoch #{hoverIndex + 1}:</span>
            <span style={{ color: '#c084fc' }}>Mem: <b>{formatBytes(hoveredItem.used_memory || 0)}</b></span>
            <span style={{ color: '#22d3ee' }}>Ops: <b>{hoveredItem.ops_per_sec || 0}/s</b></span>
          </div>
        )}

        <svg
          ref={svgRef}
          viewBox={`0 0 ${width} ${height}`}
          className="telemetry-svg"
          preserveAspectRatio="none"
          onMouseMove={handleMouseMove}
          onMouseLeave={handleMouseLeave}
        >
          <defs>
            <linearGradient id="memGradient" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#d97706" stopOpacity="0.25" />
              <stop offset="100%" stopColor="#d97706" stopOpacity="0.02" />
            </linearGradient>
          </defs>

          {/* Grid lines */}
          <line
            x1={padding.left}
            y1={padding.top}
            x2={width - padding.right}
            y2={padding.top}
            stroke="#e5dcbe"
            strokeDasharray="3 3"
          />
          <line
            x1={padding.left}
            y1={padding.top + (height - padding.top - padding.bottom) / 2}
            x2={width - padding.right}
            y2={padding.top + (height - padding.top - padding.bottom) / 2}
            stroke="#e5dcbe"
            strokeDasharray="3 3"
          />
          <line
            x1={padding.left}
            y1={height - padding.bottom}
            x2={width - padding.right}
            y2={height - padding.bottom}
            stroke="#cfc39f"
          />

          {/* Left Y Axis (Memory) */}
          <text x={padding.left - 8} y={padding.top + 4} fill="#b45309" fontSize="10" textAnchor="end">
            {formatBytes(chartData.maxMem)}
          </text>
          <text x={padding.left - 8} y={height - padding.bottom} fill="#6b5f49" fontSize="10" textAnchor="end">
            0B
          </text>

          {/* Right Y Axis (Ops/sec) */}
          <text x={width - padding.right + 6} y={padding.top + 4} fill="#0284c7" fontSize="10" textAnchor="start">
            {chartData.maxOps}/s
          </text>
          <text x={width - padding.right + 6} y={height - padding.bottom} fill="#6b5f49" fontSize="10" textAnchor="start">
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
              stroke="#d97706"
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
              stroke="#0284c7"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
              strokeDasharray="4 2"
            />
          )}

          {/* Hover Crosshair & Indicators */}
          {hoverIndex !== null && chartData.memCoords[hoverIndex] && (
            <g>
              <line
                x1={chartData.memCoords[hoverIndex][0]}
                y1={padding.top}
                x2={chartData.memCoords[hoverIndex][0]}
                y2={height - padding.bottom}
                stroke="#b8ab8e"
                strokeWidth="1"
                strokeDasharray="2 2"
              />
              <circle
                cx={chartData.memCoords[hoverIndex][0]}
                cy={chartData.memCoords[hoverIndex][1]}
                r="4.5"
                fill="#d97706"
                stroke="#fffef9"
                strokeWidth="1.5"
              />
              {chartData.opsCoords[hoverIndex] && (
                <circle
                  cx={chartData.opsCoords[hoverIndex][0]}
                  cy={chartData.opsCoords[hoverIndex][1]}
                  r="4"
                  fill="#0284c7"
                  stroke="#fffef9"
                  strokeWidth="1.5"
                />
              )}
            </g>
          )}

          {/* Live Head Indicators */}
          {hoverIndex === null && chartData.memCoords.length > 0 && (
            <g>
              <circle
                cx={chartData.memCoords[chartData.memCoords.length - 1][0]}
                cy={chartData.memCoords[chartData.memCoords.length - 1][1]}
                r="4"
                fill="#d97706"
                stroke="#fffef9"
                strokeWidth="1.5"
              />
              {chartData.opsCoords.length > 0 && (
                <circle
                  cx={chartData.opsCoords[chartData.opsCoords.length - 1][0]}
                  cy={chartData.opsCoords[chartData.opsCoords.length - 1][1]}
                  r="3.5"
                  fill="#0284c7"
                  stroke="#fffef9"
                  strokeWidth="1.5"
                />
              )}
            </g>
          )}
        </svg>
      </div>
    </div>
  );
}

export default MemoryOpsChart;
