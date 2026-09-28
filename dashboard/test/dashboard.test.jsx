import React from 'react';
import { describe, it, expect, vi } from 'vitest';
import { render, screen, fireEvent } from '@testing-library/react';

import { Header } from '../src/components/Header';
import { MetricCards } from '../src/components/MetricCards';
import { LruVisualizer } from '../src/components/LruVisualizer';
import { EventFeed } from '../src/components/EventFeed';
import { CommandConsole } from '../src/components/CommandConsole';

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
});
