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
    <div className="panel-card chart-panel-container" data-testid="chart-panel">
      <div className="panel-header">
        <div>
          <div className="panel-title-row">
            <h2 className="panel-title">Real-Time Telemetry</h2>
            <span className="panel-scope-badge num-tabular">Last {displayHistory.length} epochs</span>
          </div>
          <span className="panel-desc">Memory allocation and command throughput (dual-axis)</span>
        </div>
        <div className="chart-legend">
          <span className="legend-item">
            <span className="legend-dot dot-memory" aria-hidden="true"></span>
            <span className="legend-label">Memory:</span>
            <b className="legend-value num-tabular">{formatBytes(chartData.latestMem)}</b>
            <span className="chart-legend-badge num-tabular">(peak: {formatBytes(chartData.peakMem)})</span>
          </span>
          <span className="legend-item">
            <span className="legend-dot dot-ops" aria-hidden="true"></span>
            <span className="legend-label">Throughput:</span>
            <b className="legend-value num-tabular">{chartData.latestOps} ops/s</b>
            <span className="chart-legend-badge num-tabular">(peak: {chartData.peakOps}/s)</span>
          </span>
        </div>
      </div>

      <div className="chart-wrapper">
        {hoveredItem && (
          <div className="chart-tooltip">
            <span className="tooltip-epoch num-tabular">Epoch #{hoverIndex + 1}</span>
            <span className="tooltip-mem num-tabular">Mem: <b>{formatBytes(hoveredItem.used_memory || 0)}</b></span>
            <span className="tooltip-ops num-tabular">Ops: <b>{hoveredItem.ops_per_sec || 0}/s</b></span>
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
              <stop offset="0%" stopColor="#d64028" stopOpacity="0.22" />
              <stop offset="100%" stopColor="#d64028" stopOpacity="0.01" />
            </linearGradient>
          </defs>

          {/* Grid lines */}
          <line
            x1={padding.left}
            y1={padding.top}
            x2={width - padding.right}
            y2={padding.top}
            stroke="#22242c"
            strokeDasharray="2 3"
          />
          <line
            x1={padding.left}
            y1={padding.top + (height - padding.top - padding.bottom) / 2}
            x2={width - padding.right}
            y2={padding.top + (height - padding.top - padding.bottom) / 2}
            stroke="#22242c"
            strokeDasharray="2 3"
          />
          <line
            x1={padding.left}
            y1={height - padding.bottom}
            x2={width - padding.right}
            y2={height - padding.bottom}
            stroke="#2d3039"
          />

          {/* Left Y Axis (Memory) */}
          <text x={padding.left - 8} y={padding.top + 4} fill="#e18576" fontSize="10" fontFamily="var(--font-mono)" textAnchor="end">
            {formatBytes(chartData.maxMem)}
          </text>
          <text x={padding.left - 8} y={height - padding.bottom} fill="#828594" fontSize="10" fontFamily="var(--font-mono)" textAnchor="end">
            0B
          </text>

          {/* Right Y Axis (Ops/sec) */}
          <text x={width - padding.right + 6} y={padding.top + 4} fill="#58a6ff" fontSize="10" fontFamily="var(--font-mono)" textAnchor="start">
            {chartData.maxOps}/s
          </text>
          <text x={width - padding.right + 6} y={height - padding.bottom} fill="#828594" fontSize="10" fontFamily="var(--font-mono)" textAnchor="start">
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
              stroke="#d64028"
              strokeWidth="2"
              strokeLinecap="round"
              strokeLinejoin="round"
            />
          )}

          {/* Ops/Sec Polyline */}
          {chartData.opsPoints && (
            <polyline
              points={chartData.opsPoints}
              fill="none"
              stroke="#58a6ff"
              strokeWidth="1.8"
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
                stroke="#4a4d5a"
                strokeWidth="1"
                strokeDasharray="2 2"
              />
              <circle
                cx={chartData.memCoords[hoverIndex][0]}
                cy={chartData.memCoords[hoverIndex][1]}
                r="4"
                fill="#d64028"
                stroke="#18191f"
                strokeWidth="1.5"
              />
              {chartData.opsCoords[hoverIndex] && (
                <circle
                  cx={chartData.opsCoords[hoverIndex][0]}
                  cy={chartData.opsCoords[hoverIndex][1]}
                  r="3.5"
                  fill="#58a6ff"
                  stroke="#18191f"
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
                r="3.5"
                fill="#d64028"
                stroke="#18191f"
                strokeWidth="1.5"
              />
              {chartData.opsCoords.length > 0 && (
                <circle
                  cx={chartData.opsCoords[chartData.opsCoords.length - 1][0]}
                  cy={chartData.opsCoords[chartData.opsCoords.length - 1][1]}
                  r="3"
                  fill="#58a6ff"
                  stroke="#18191f"
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
