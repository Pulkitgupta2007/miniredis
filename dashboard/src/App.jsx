import { useState, useEffect, useRef, useCallback } from 'react';
import { Header } from './components/Header';
import { MetricCards } from './components/MetricCards';
import { MemoryOpsChart } from './components/MemoryOpsChart';
import { LruVisualizer } from './components/LruVisualizer';
import { EventFeed } from './components/EventFeed';
import { CommandConsole } from './components/CommandConsole';
import { TrafficPlayground } from './components/TrafficPlayground';
import { KeyspaceExplorer } from './components/KeyspaceExplorer';
import './App.css';

export function App() {
  const [online, setOnline] = useState(false);
  const [metrics, setMetrics] = useState({});
  const [lruKeys, setLruKeys] = useState([]);
  const [lfuKeys, setLfuKeys] = useState([]);
  const [events, setEvents] = useState([]);
  const [history, setHistory] = useState([]);
  const [prefillCommand, setPrefillCommand] = useState('');

  const wsRef = useRef(null);
  const pendingRequestsRef = useRef(new Map());

  // WebSocket URL resolution
  const getWsUrl = () => {
    if (typeof window === 'undefined') return 'ws://127.0.0.1:4000';
    const loc = window.location;
    // If running under Vite dev server (port 3000/5173), default bridge is on 4000
    if (loc.port === '3000' || loc.port === '5173') {
      return `ws://${loc.hostname}:4000`;
    }
    const proto = loc.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${proto}//${loc.host}`;
  };

  const connectWebSocket = useCallback(() => {
    const url = getWsUrl();
    try {
      const ws = new WebSocket(url);
      wsRef.current = ws;

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);

          if (msg.type === 'INITIAL_STATE' || msg.type === 'TELEMETRY_UPDATE') {
            setOnline(Boolean(msg.online));
            if (msg.metrics) setMetrics(msg.metrics);
            if (msg.lru_keys) setLruKeys(msg.lru_keys);
            if (msg.lfu_keys) setLfuKeys(msg.lfu_keys);
            if (msg.events) setEvents(msg.events);

            if (msg.online && msg.metrics) {
              setHistory((prev) => {
                const now = msg.timestamp || Date.now();
                const newPoint = {
                  timestamp: now,
                  used_memory: msg.metrics.used_memory || 0,
                  ops_per_sec: msg.metrics.ops_per_sec || 0
                };
                if (prev.length === 0) return [newPoint];
                const last = prev[prev.length - 1];
                // If updates arrive rapidly in a burst (< 300ms), update the latest point with the peak
                // so burst mutations do not displace the entire historical window
                if (now - last.timestamp < 300) {
                  const updated = [...prev];
                  updated[updated.length - 1] = {
                    timestamp: now,
                    used_memory: Math.max(last.used_memory, newPoint.used_memory),
                    ops_per_sec: Math.max(last.ops_per_sec, newPoint.ops_per_sec)
                  };
                  return updated;
                }
                const updated = [...prev, newPoint];
                return updated.slice(-30); // keep last 30 data points
              });
            }
          } else if (msg.type === 'COMMAND_REPLY') {
            const resolver = pendingRequestsRef.current.get(msg.id);
            if (resolver) {
              pendingRequestsRef.current.delete(msg.id);
              if (msg.success) {
                resolver.resolve(msg.result);
              } else {
                resolver.reject(new Error(msg.error || 'Command failed'));
              }
            }
          }
        } catch {
          // Ignore malformed payloads
        }
      };

      ws.onclose = () => {
        setOnline(false);
        wsRef.current = null;
        setTimeout(connectWebSocket, 1500); // Auto reconnect
      };

      ws.onerror = () => {
        setOnline(false);
      };
    } catch {
      setOnline(false);
      setTimeout(connectWebSocket, 2000);
    }
  }, []);

  useEffect(() => {
    connectWebSocket();
    return () => {
      if (wsRef.current) {
        wsRef.current.close();
      }
    };
  }, [connectWebSocket]);

  const executeCommand = async (commandStr) => {
    const id = `req_${Date.now()}_${Math.random().toString(36).substr(2, 6)}`;
    const ws = wsRef.current;

    if (ws && ws.readyState === WebSocket.OPEN) {
      return new Promise((resolve, reject) => {
        pendingRequestsRef.current.set(id, { resolve, reject });
        ws.send(JSON.stringify({ type: 'COMMAND', id, command: commandStr }));

        // Timeout safety
        setTimeout(() => {
          if (pendingRequestsRef.current.has(id)) {
            pendingRequestsRef.current.delete(id);
            reject(new Error('Command timeout'));
          }
        }, 5000);
      });
    }

    // Fallback to HTTP POST proxy if WebSocket is reconnecting
    const host = typeof window !== 'undefined' && (window.location.port === '3000' || window.location.port === '5173')
      ? `http://${window.location.hostname}:4000`
      : '';
    const res = await fetch(`${host}/api/command`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ command: commandStr })
    });
    const data = await res.json();
    if (!data.success) {
      throw new Error(data.error || 'Command error');
    }
    return data.result;
  };

  const handleSelectKey = (key) => {
    setPrefillCommand(`GET ${key}`);
  };

  return (
    <div className="dashboard-root">
      {!online && (
        <div className="offline-banner" data-testid="offline-banner">
          <span className="banner-icon">⚠️</span>
          <span>MiniRedis Server Disconnected — Attempting auto-reconnection to TCP bridge...</span>
        </div>
      )}

      <div className="dashboard-container">
        <Header
          online={online}
          uptimeSeconds={metrics.uptime_in_seconds}
          connectedClients={metrics.connected_clients}
        />

        <MetricCards metrics={metrics} />

        <div className="telemetry-row">
          <div className="telemetry-main">
            <MemoryOpsChart history={history} />
            <LruVisualizer keys={lruKeys} lfuKeys={lfuKeys} onSelectKey={handleSelectKey} />
          </div>

          <div className="telemetry-sidebar">
            <EventFeed events={events} />
          </div>
        </div>

        <KeyspaceExplorer
          onExecute={executeCommand}
          online={online}
          onSelectKey={handleSelectKey}
        />

        <TrafficPlayground
          onExecute={executeCommand}
          online={online}
          metrics={metrics}
        />

        <CommandConsole
          onExecute={executeCommand}
          online={online}
          prefillCommand={prefillCommand}
        />
      </div>
    </div>
  );
}

export default App;
