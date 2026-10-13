import React from 'react';
import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent, within } from '@testing-library/react';

import { Header } from '../src/components/Header';
import { MetricCards } from '../src/components/MetricCards';
import { MemoryOpsChart } from '../src/components/MemoryOpsChart';
import { LruVisualizer } from '../src/components/LruVisualizer';
import { EventFeed } from '../src/components/EventFeed';
import { CommandConsole } from '../src/components/CommandConsole';
import { TrafficPlayground } from '../src/components/TrafficPlayground';
import { KeyspaceExplorer } from '../src/components/KeyspaceExplorer';

describe('Header Component', () => {
  it('renders online state with uptime and clients', () => {
    render(<Header online={true} uptimeSeconds={125} connectedClients={3} />);

    const badge = screen.getByTestId('status-badge');
    expect(badge.textContent).toContain('MINIREDIS ONLINE');
    expect(screen.getByText('Uptime:')).toBeDefined();
    expect(screen.getByText('2m 5s')).toBeDefined();
    expect(screen.getByText('3')).toBeDefined();
  });

  it('renders offline state with warning badge', () => {
    render(<Header online={false} uptimeSeconds={0} connectedClients={0} />);

    const badge = screen.getByTestId('status-badge');
    expect(badge.textContent).toContain('SERVER OFFLINE');
    expect(screen.getByText(/Reconnecting to TCP bridge/i)).toBeDefined();
  });
});

describe('MetricCards Component', () => {
  it('renders ops/sec, hit ratio, memory allocation, and keyspace counters', () => {
    const metrics = {
      used_memory: 2048,
      maxmemory: 8192,
      maxmemory_policy: 'allkeys-lru',
      total_keys: 15,
      keyspace_hits: 80,
      keyspace_misses: 20,
      hit_ratio: 80.0,
      evicted_keys: 4,
      expired_keys: 2,
      ops_per_sec: 42,
      total_commands_processed: 500
    };

    render(<MetricCards metrics={metrics} />);

    expect(screen.getByTestId('metric-ops').textContent).toContain('42');
    expect(screen.getByTestId('metric-hit-ratio').textContent).toContain('80.0%');
    expect(screen.getByTestId('metric-memory').textContent).toContain('2.0 KB');
    expect(screen.getByTestId('metric-memory').textContent).toContain('(25%)');
    expect(screen.getByTestId('metric-keys').textContent).toContain('15');
    expect(screen.getByText('Policy: allkeys-lru')).toBeDefined();
  });
});

describe('MemoryOpsChart Component', () => {
  it('renders gracefully when history is empty', () => {
    render(<MemoryOpsChart history={[]} />);
    expect(screen.getByTestId('chart-panel')).toBeDefined();
    expect(screen.getByText(/Real-Time Telemetry/i)).toBeDefined();
    expect(screen.getByText(/Last 0 epochs/i)).toBeDefined();
  });

  it('renders memory and ops metrics with peak values and SVG polylines', () => {
    const history = [
      { timestamp: 1000, used_memory: 1024, ops_per_sec: 10 },
      { timestamp: 2000, used_memory: 4096, ops_per_sec: 50 },
      { timestamp: 3000, used_memory: 2048, ops_per_sec: 25 }
    ];

    render(<MemoryOpsChart history={history} />);
    expect(screen.getByTestId('chart-panel')).toBeDefined();
    expect(screen.getByText(/Last 3 epochs/i)).toBeDefined();
    // Latest values
    expect(screen.getByText('2.0KB')).toBeDefined();
    expect(screen.getByText('25 ops/s')).toBeDefined();
    // Peak badges
    expect(screen.getByText('(peak: 4.0KB)')).toBeDefined();
    expect(screen.getByText('(peak: 50/s)')).toBeDefined();
  });

  it('renders reliably under simulated eviction storm workload', () => {
    // Generate 40 data points representing an eviction storm burst
    const stormHistory = Array.from({ length: 40 }, (_, i) => ({
      timestamp: 10000 + i * 20,
      used_memory: 51200 - (i % 5) * 1024,
      ops_per_sec: 200 + i * 50
    }));

    render(<MemoryOpsChart history={stormHistory} />);
    expect(screen.getByTestId('chart-panel')).toBeDefined();
    expect(screen.getByText(/Last 40 epochs/i)).toBeDefined();
    expect(screen.getByText(/peak: 50.0KB/i)).toBeDefined();
  });
});

describe('LruVisualizer Component', () => {
  it('renders empty state when no keys exist', () => {
    render(<LruVisualizer keys={[]} />);
    expect(screen.getByTestId('lru-empty')).toBeDefined();
    expect(screen.getByText(/No active keys in storage/i)).toBeDefined();
  });

  it('renders keys in exact MRU to LRU order with tags and triggers click inspection', () => {
    const onSelect = vi.fn();
    const keys = ['user:3', 'user:2', 'user:1'];

    render(<LruVisualizer keys={keys} onSelectKey={onSelect} />);

    expect(screen.getByText('MRU (Head)')).toBeDefined();
    expect(screen.getByText('LRU (Victim)')).toBeDefined();

    const node = screen.getByTestId('lru-node-user:3');
    expect(node).toBeDefined();
    fireEvent.click(node);
    expect(onSelect).toHaveBeenCalledWith('user:3');
  });

  it('switches to LFU mode and displays frequency buckets with min_freq', () => {
    const onSelect = vi.fn();
    const lfuKeys = ['k1:1', 'k2:1', 'k3:3'];

    render(<LruVisualizer keys={['k3', 'k2', 'k1']} lfuKeys={lfuKeys} onSelectKey={onSelect} />);

    // Click LFU Buckets toggle
    const lfuBtn = screen.getByText('📊 LFU Buckets');
    fireEvent.click(lfuBtn);

    expect(screen.getByTestId('lfu-buckets')).toBeDefined();
    expect(screen.getByText('Freq = 1')).toBeDefined();
    expect(screen.getByText('Freq = 3')).toBeDefined();
    expect(screen.getByText('min_freq 🎯')).toBeDefined();

    // Click a key chip in LFU mode
    const keyChip = screen.getByText('k1');
    fireEvent.click(keyChip);
    expect(onSelect).toHaveBeenCalledWith('k1');
  });
});

describe('EventFeed Component', () => {
  it('renders eviction and expiration events and supports filtering', () => {
    const events = [
      { timestamp: 1700000000000, type: 'EVICTION', key: 'victim_k1', detail: 'freed=128' },
      { timestamp: 1700000000100, type: 'EXPIRATION', key: 'token_k2', detail: 'ttl_expired' }
    ];

    render(<EventFeed events={events} />);

    expect(screen.getByText('victim_k1')).toBeDefined();
    expect(screen.getByText('token_k2')).toBeDefined();

    // Filter to only Evictions
    const evictionsBtn = screen.getByText('Evictions');
    fireEvent.click(evictionsBtn);

    expect(screen.getByText('victim_k1')).toBeDefined();
    expect(screen.queryByText('token_k2')).toBeNull();
  });
});

describe('CommandConsole Component', () => {
  it('disables input when server is offline', () => {
    render(<CommandConsole online={false} onExecute={vi.fn()} />);
    const input = screen.getByTestId('console-input');
    expect(input.hasAttribute('disabled')).toBe(true);
  });

  it('executes command and displays result in console log', async () => {
    const onExecute = vi.fn().mockResolvedValue('PONG');
    render(<CommandConsole online={true} onExecute={onExecute} />);

    const input = screen.getByTestId('console-input');
    fireEvent.change(input, { target: { value: 'PING' } });
    fireEvent.submit(screen.getByTestId('console-form'));

    expect(onExecute).toHaveBeenCalledWith('PING');
    // Result should appear in console
    const result = await screen.findByText('PONG');
    expect(result).toBeDefined();
  });

  it('prefills input when prefillCommand prop changes', () => {
    const { rerender } = render(<CommandConsole online={true} onExecute={vi.fn()} prefillCommand="" />);
    rerender(<CommandConsole online={true} onExecute={vi.fn()} prefillCommand="GET session:99" />);

    const input = screen.getByTestId('console-input');
    expect(input.value).toBe('GET session:99');
  });

  it('displays autocomplete suggestions when typing command prefix', () => {
    render(<CommandConsole online={true} onExecute={vi.fn()} />);

    const input = screen.getByTestId('console-input');
    fireEvent.change(input, { target: { value: 'SE' } });

    const popup = screen.getByTestId('autocomplete-popup');
    expect(popup).toBeDefined();
    const setOption = within(popup).getByText('SET');
    expect(setOption).toBeDefined();

    // Clicking autocomplete suggestion fills input
    fireEvent.click(setOption);
    expect(input.value).toBe('SET ');
  });

  it('renders syntax helper banner for recognized command', () => {
    render(<CommandConsole online={true} onExecute={vi.fn()} />);

    const input = screen.getByTestId('console-input');
    fireEvent.change(input, { target: { value: 'EXPIRE user:1' } });

    expect(screen.getByText('Syntax:')).toBeDefined();
    expect(screen.getByText('EXPIRE <key> <seconds>')).toBeDefined();
  });
});

describe('TrafficPlayground Component', () => {
  const defaultMetrics = {
    maxmemory: 0,
    maxmemory_policy: 'noeviction',
    used_memory: 1024
  };

  it('renders memory and policy configuration controls', () => {
    render(
      <TrafficPlayground
        onExecute={vi.fn()}
        online={true}
        metrics={defaultMetrics}
      />
    );

    expect(screen.getByTestId('traffic-playground')).toBeDefined();
    expect(screen.getByText('Cache & Eviction Policy')).toBeDefined();
    expect(screen.getByText('Traffic & Chaos Playground')).toBeDefined();
    expect(screen.getByText('Burst 100 Keys')).toBeDefined();
    expect(screen.getByText('80/20 Zipfian Reads')).toBeDefined();
    expect(screen.getByText('Eviction Storm')).toBeDefined();
  });

  it('updates input when clicking preset memory buttons', () => {
    render(
      <TrafficPlayground
        onExecute={vi.fn()}
        online={true}
        metrics={defaultMetrics}
      />
    );

    const preset256 = screen.getByText('256 KB');
    fireEvent.click(preset256);

    const input = screen.getByPlaceholderText('0 = unlimited');
    expect(input.value).toBe('256');
  });

  it('executes CONFIG SET when clicking apply button', async () => {
    const onExecute = vi.fn().mockResolvedValue('OK');
    render(
      <TrafficPlayground
        onExecute={onExecute}
        online={true}
        metrics={defaultMetrics}
      />
    );

    // Select 50 KB
    fireEvent.click(screen.getByText('50 KB'));
    // Select allkeys-lru
    const lruPill = screen.getByLabelText(/allkeys-lru/i);
    fireEvent.click(lruPill);

    const applyBtn = screen.getByText('Apply CONFIG SET');
    fireEvent.click(applyBtn);

    const feedback = await screen.findByText(/Saved: maxmemory=50 KB, policy=allkeys-lru/i);
    expect(feedback).toBeDefined();

    expect(onExecute).toHaveBeenCalledWith('CONFIG SET maxmemory 51200');
    expect(onExecute).toHaveBeenCalledWith('CONFIG SET maxmemory-policy allkeys-lru');
  });

  it('triggers Burst 100 Keys simulation on click', async () => {
    const onExecute = vi.fn().mockResolvedValue('OK');
    render(
      <TrafficPlayground
        onExecute={onExecute}
        online={true}
        metrics={defaultMetrics}
      />
    );

    const burstBtn = screen.getByText('Burst 100 Keys');
    fireEvent.click(burstBtn);

    expect(screen.getByText(/Bursting 100 rapid SET operations/i)).toBeDefined();
    expect(onExecute).toHaveBeenCalled();
  });
});

describe('KeyspaceExplorer Component', () => {
  it('renders table headers and add key button', () => {
    render(
      <KeyspaceExplorer
        onExecute={vi.fn().mockResolvedValue([])}
        online={true}
      />
    );

    expect(screen.getByTestId('keyspace-explorer')).toBeDefined();
    expect(screen.getByText('Keyspace Explorer & Data Browser')).toBeDefined();
    expect(screen.getByText('+ Add Key')).toBeDefined();
    expect(screen.getByPlaceholderText('Search keys...')).toBeDefined();
  });

  it('renders keys with TTL and memory when fetched', async () => {
    // Mock onExecute responses for KEYS, GET, TTL, MEMORY USAGE
    const onExecute = vi.fn().mockImplementation((cmd) => {
      if (cmd === 'KEYS *') return Promise.resolve(['sess:u1', 'cache:k2']);
      if (cmd.startsWith('GET sess:u1')) return Promise.resolve('{"role":"admin"}');
      if (cmd.startsWith('GET cache:k2')) return Promise.resolve('page_html');
      if (cmd.startsWith('TTL sess:u1')) return Promise.resolve(120);
      if (cmd.startsWith('TTL cache:k2')) return Promise.resolve(-1);
      if (cmd.startsWith('MEMORY USAGE')) return Promise.resolve(128);
      return Promise.resolve('OK');
    });

    // Mock global fetch to fail so it uses onExecute fallback
    const origFetch = global.fetch;
    global.fetch = vi.fn().mockRejectedValue(new Error('Network error'));

    try {
      render(
        <KeyspaceExplorer
          onExecute={onExecute}
          online={true}
        />
      );

      const keyEl = await screen.findByText('sess:u1');
      expect(keyEl).toBeDefined();
      expect(screen.getByText('cache:k2')).toBeDefined();
      expect(screen.getByText('Persistent (∞)')).toBeDefined();
      expect(screen.getByText(/120s/)).toBeDefined();
    } finally {
      global.fetch = origFetch;
    }
  });

  it('opens Add Key modal and executes SET command', async () => {
    const onExecute = vi.fn().mockResolvedValue('OK');
    render(
      <KeyspaceExplorer
        onExecute={onExecute}
        online={true}
      />
    );

    fireEvent.click(screen.getByText('+ Add Key'));
    expect(screen.getByText('Create New Key')).toBeDefined();

    const keyInput = screen.getByPlaceholderText('e.g. user:profile:100');
    fireEvent.change(keyInput, { target: { value: 'user:new:99' } });

    const valInput = screen.getByPlaceholderText(/e\.g\. \{"name": "Alice"/);
    fireEvent.change(valInput, { target: { value: 'SecretValue' } });

    const submitBtn = screen.getByText('Set Key (SET)');
    fireEvent.click(submitBtn);

    expect(onExecute).toHaveBeenCalledWith(['SET', 'user:new:99', 'SecretValue']);
    const feedback = await screen.findByText(/Created key "user:new:99"/i);
    expect(feedback).toBeDefined();
  });

  it('safely creates key with JSON value containing quotes without syntax error', async () => {
    const onExecute = vi.fn().mockResolvedValue('OK');
    render(
      <KeyspaceExplorer
        onExecute={onExecute}
        online={true}
      />
    );

    fireEvent.click(screen.getByText('+ Add Key'));
    const keyInput = screen.getByPlaceholderText('e.g. user:profile:100');
    fireEvent.change(keyInput, { target: { value: 'session:json:1' } });

    const valInput = screen.getByPlaceholderText(/e\.g\. \{"name": "Alice"/);
    fireEvent.change(valInput, { target: { value: '{"name": "Alice", "role": "admin"}' } });

    const submitBtn = screen.getByText('Set Key (SET)');
    fireEvent.click(submitBtn);

    expect(onExecute).toHaveBeenCalledWith(['SET', 'session:json:1', '{"name": "Alice", "role": "admin"}']);
    const feedback = await screen.findByText(/Created key "session:json:1"/i);
    expect(feedback).toBeDefined();
  });
});


