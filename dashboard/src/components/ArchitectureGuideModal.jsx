import React from 'react';

export function ArchitectureGuideModal({ isOpen, onClose }) {
  if (!isOpen) return null;

  return (
    <div className="modal-overlay" onClick={onClose} data-testid="architecture-modal">
      <div className="modal-content architecture-modal-content" onClick={(e) => e.stopPropagation()}>
        <div className="modal-header">
          <div className="modal-title-with-icon">
            <div>
              <h4>MiniRedis Systems Architecture & Core Algorithms</h4>
              <p className="modal-subtitle">Direct insights into the modern C++17 implementation</p>
            </div>
          </div>
          <button type="button" onClick={onClose} className="modal-close-btn" aria-label="Close guide">×</button>
        </div>

        <div className="architecture-sections-scroll">
          {/* Section 1 */}
          <div className="arch-card">
            <div className="arch-card-header">
              <span className="arch-step-badge">01</span>
              <h5>Single-Threaded Reactor Event Loop (epoll / kqueue)</h5>
            </div>
            <p className="arch-card-body">
              MiniRedis operates on a non-blocking single-threaded event loop using <code>epoll</code> on Linux and <code>kqueue</code> on macOS.
              Because in-memory hash lookups execute in ~100 nanoseconds, multithreading would introduce lock contention, cache-line bouncing, and thread context switches that exceed command execution time.
            </p>
            <div className="arch-tech-pill">Unix Self-Pipe Trick for non-reentrant safe signal teardown (SIGINT)</div>
          </div>

          {/* Section 2 */}
          <div className="arch-card">
            <div className="arch-card-header">
              <span className="arch-step-badge">02</span>
              <h5>Streaming Zero-Copy RESP2 Parser &amp; Security Boundaries</h5>
            </div>
            <p className="arch-card-body">
              Incoming TCP byte streams are tokenized using <code>std::string_view</code> references without copying string buffers until final command creation.
              To guard against adversarial memory bloat and integer overflow vulnerabilities:
            </p>
            <ul className="arch-list">
              <li>Bulk strings capped at 512 MB (rejects negative lengths).</li>
              <li>Array allocations clamped to <code>min(count, 1024)</code> upfront to prevent speculative heap exhaustion.</li>
              <li>Recursion depth bounded at 64 to protect against stack-overflow exploits.</li>
            </ul>
          </div>

          {/* Section 3 */}
          <div className="arch-card">
            <div className="arch-card-header">
              <span className="arch-step-badge">03</span>
              <h5>True O(1) Expiration via Contiguous Swap-and-Pop</h5>
            </div>
            <p className="arch-card-body">
              Standard hash maps cannot be sampled uniformly in O(1) time because empty buckets cause sampling misses.
              MiniRedis pairs <code>unordered_map&lt;key, time_point&gt;</code> with a contiguous <code>vector&lt;string&gt; expiry_keys_</code> and a reverse index map <code>expiry_index_</code>:
            </p>
            <ul className="arch-list">
              <li><strong>Sampling</strong>: Uniform random index selection from <code>[0, size-1]</code> in O(1).</li>
              <li><strong>Deletion</strong>: Target key is swapped with the back element of the vector and popped in O(1).</li>
              <li><strong>Active Cycle</strong>: Samples 20 keys periodically; if &gt;25% are expired, repeats immediately (capped at 25ms budget).</li>
            </ul>
          </div>

          {/* Section 4 */}
          <div className="arch-card">
            <div className="arch-card-header">
              <span className="arch-step-badge">04</span>
              <h5>Exact O(1) LRU vs. Exact O(1) LFU Cache Eviction</h5>
            </div>
            <p className="arch-card-body">
              MiniRedis implements exact O(1) cache eviction algorithms configurable via <code>CONFIG SET maxmemory-policy</code>:
            </p>
            <ul className="arch-list">
              <li><strong>LRU (allkeys-lru)</strong>: Sentinel doubly linked list + hash map. Keys spliced to head (MRU) in O(1); evicted from tail (LRU) in O(1).</li>
              <li><strong>LFU (allkeys-lfu)</strong>: Map of frequency buckets (<code>freq -&gt; DoublyLinkedList</code>) + <code>min_freq</code> pointer. Keys are promoted to next bucket on access; evicted from tail of <code>min_freq</code> bucket providing exact LRU tie-breaking among keys sharing the lowest access count.</li>
            </ul>
          </div>

          {/* Section 5 */}
          <div className="arch-card">
            <div className="arch-card-header">
              <span className="arch-step-badge">05</span>
              <h5>AOF Persistence, Epoch Timestamps &amp; Atomic Compaction</h5>
            </div>
            <p className="arch-card-body">
              Durability is maintained via an append-only file with configurable fsync policies (<code>always</code>, <code>everysec</code> at 1 Hz via background thread, <code>no</code>):
            </p>
            <ul className="arch-list">
              <li>Relative TTLs (e.g. <code>EX 60</code>) are converted to absolute epoch timestamps (<code>PEXPIREAT key epoch_ms</code>) to prevent key resurrection on reboot.</li>
              <li>Crashed partial tail records are automatically detected and pruned via <code>ftruncate()</code> on startup replay.</li>
              <li>Log compaction (<code>BGREWRITEAOF</code>) writes minimal state to a temporary file and atomically swaps using POSIX <code>rename()</code>.</li>
            </ul>
          </div>
        </div>

        <div className="modal-actions">
          <button type="button" onClick={onClose} className="modal-submit-btn">
            Close Guide
          </button>
        </div>
      </div>
    </div>
  );
}

export default ArchitectureGuideModal;
