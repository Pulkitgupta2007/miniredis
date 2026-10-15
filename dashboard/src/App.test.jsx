import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { Header } from './components/Header';
import { MetricCards } from './components/MetricCards';

describe('MiniRedis Dashboard Sanity Tests', () => {
  it('renders Header with title and status', () => {
    render(<Header online={true} uptimeSeconds={3600} connectedClients={2} />);
    expect(screen.getByText(/MiniRedis Live Monitor/i)).toBeDefined();
    expect(screen.getByText(/Uptime:/i)).toBeDefined();
    expect(screen.getByText(/Clients:/i)).toBeDefined();
  });

  it('renders MetricCards with accurate values', () => {
    const mockMetrics = {
      used_memory: 10240,
      maxmemory: 51200,
      ops_per_sec: 125,
      total_keys: 42,
      evicted_keys: 5,
      expired_keys: 12,
      maxmemory_policy: 'allkeys-lru'
    };
    render(<MetricCards metrics={mockMetrics} />);
    expect(screen.getByTestId('metric-ops').textContent).toContain('125');
    expect(screen.getByTestId('metric-keys').textContent).toContain('42');
    expect(screen.getByText(/allkeys-lru/i)).toBeDefined();
  });
});
