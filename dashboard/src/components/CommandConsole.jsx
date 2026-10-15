import React, { useState, useRef, useEffect, useMemo } from 'react';

const REDIS_COMMAND_HINTS = [
  { cmd: 'SET', syntax: 'SET <key> <value> [EX seconds | PX ms] [NX | XX]', desc: 'Store string value with optional TTL and conditions' },
  { cmd: 'GET', syntax: 'GET <key>', desc: 'Retrieve string value of key' },
  { cmd: 'DEL', syntax: 'DEL <key> [key ...]', desc: 'Remove one or more keys' },
  { cmd: 'EXISTS', syntax: 'EXISTS <key> [key ...]', desc: 'Determine if one or more keys exist' },
  { cmd: 'EXPIRE', syntax: 'EXPIRE <key> <seconds>', desc: 'Set key time-to-live in relative seconds' },
  { cmd: 'PEXPIRE', syntax: 'PEXPIRE <key> <milliseconds>', desc: 'Set key time-to-live in milliseconds' },
  { cmd: 'TTL', syntax: 'TTL <key>', desc: 'Get remaining time-to-live in seconds' },
  { cmd: 'PTTL', syntax: 'PTTL <key>', desc: 'Get remaining time-to-live in milliseconds' },
  { cmd: 'PERSIST', syntax: 'PERSIST <key>', desc: 'Remove expiration from key' },
  { cmd: 'KEYS', syntax: 'KEYS <pattern>', desc: 'Find all keys matching wildcard pattern (e.g. KEYS *)' },
  { cmd: 'DBSIZE', syntax: 'DBSIZE', desc: 'Return number of keys in database' },
  { cmd: 'FLUSHALL', syntax: 'FLUSHALL', desc: 'Delete all keys from database' },
  { cmd: 'CONFIG SET', syntax: 'CONFIG SET <param> <value>', desc: 'Set maxmemory or maxmemory-policy' },
  { cmd: 'CONFIG GET', syntax: 'CONFIG GET <param>', desc: 'Get configuration parameter' },
  { cmd: 'MEMORY USAGE', syntax: 'MEMORY USAGE <key>', desc: 'Estimate memory occupied by key in bytes' },
  { cmd: 'INFO', syntax: 'INFO', desc: 'Get full server telemetry and metrics' },
  { cmd: 'EVENTS', syntax: 'EVENTS [limit]', desc: 'Stream recent eviction and expiration events' },
  { cmd: 'LRUKEYS', syntax: 'LRUKEYS [limit]', desc: 'Inspect keys in current MRU-to-LRU order' },
  { cmd: 'LFUKEYS', syntax: 'LFUKEYS [limit]', desc: 'Inspect keys in current frequency order' },
  { cmd: 'PING', syntax: 'PING [message]', desc: 'Ping the server liveness' }
];

function CommandConsoleComponent({ onExecute, online, prefillCommand }) {
  const [input, setInput] = useState('');
  const [history, setHistory] = useState([
    {
      type: 'system',
      text: 'Connected to MiniRedis Console. Type a command (e.g. SET foo bar) and press Enter.'
    }
  ]);
  const [cmdIndex, setCmdIndex] = useState(-1);
  const [pastCommands, setPastCommands] = useState([]);
  const [loading, setLoading] = useState(false);
  const [showSuggestions, setShowSuggestions] = useState(false);
  const [selectedSuggestionIdx, setSelectedSuggestionIdx] = useState(0);

  const consoleBottomRef = useRef(null);
  const inputRef = useRef(null);

  useEffect(() => {
    if (prefillCommand) {
      setInput(prefillCommand);
      if (inputRef.current) inputRef.current.focus();
    }
  }, [prefillCommand]);

  useEffect(() => {
    if (typeof consoleBottomRef.current?.scrollIntoView === 'function') {
      consoleBottomRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [history]);

  // Compute autocomplete suggestions
  const suggestions = useMemo(() => {
    const trimmed = input.trimStart();
    if (!trimmed) return [];
    const firstWord = trimmed.split(' ')[0].toUpperCase();
    return REDIS_COMMAND_HINTS.filter(
      (h) => h.cmd.startsWith(firstWord) || h.syntax.toUpperCase().startsWith(trimmed.toUpperCase())
    ).slice(0, 5);
  }, [input]);

  const activeHint = useMemo(() => {
    const firstWord = input.trim().split(' ')[0].toUpperCase();
    return REDIS_COMMAND_HINTS.find((h) => h.cmd === firstWord);
  }, [input]);

  const handleSubmit = async (e) => {
    if (e) e.preventDefault();
    const trimmed = input.trim();
    if (!trimmed || loading) return;

    setShowSuggestions(false);

    if (!online) {
      setHistory((prev) => [
        ...prev.slice(-250),
        { type: 'input', text: trimmed },
        { type: 'error', text: 'Error: MiniRedis server is offline. Cannot execute command.' }
      ]);
      setInput('');
      return;
    }

    setPastCommands((prev) => [trimmed, ...prev.slice(0, 49)]);
    setCmdIndex(-1);
    setLoading(true);

    const startTime = performance.now();
    setHistory((prev) => [...prev.slice(-250), { type: 'input', text: trimmed }]);
    setInput('');

    try {
      const res = await onExecute(trimmed);
      const elapsedMs = (performance.now() - startTime).toFixed(2);

      let formattedResult = '';
      if (res === null) {
        formattedResult = '(nil)';
      } else if (Array.isArray(res)) {
        formattedResult = res.length === 0 ? '(empty list or set)' : res.map((item, idx) => `${idx + 1}) "${item}"`).join('\n');
      } else if (typeof res === 'object') {
        formattedResult = JSON.stringify(res, null, 2);
      } else {
        formattedResult = String(res);
      }

      setHistory((prev) => [
        ...prev.slice(-250),
        { type: 'output', text: formattedResult, latency: `${elapsedMs} ms` }
      ]);
    } catch (err) {
      setHistory((prev) => [...prev.slice(-250), { type: 'error', text: `(error) ${err.message}` }]);
    } finally {
      setLoading(false);
    }
  };

  const handleKeyDown = (e) => {
    if (showSuggestions && suggestions.length > 0) {
      if (e.key === 'Tab') {
        e.preventDefault();
        const chosen = suggestions[selectedSuggestionIdx] || suggestions[0];
        setInput(`${chosen.cmd} `);
        setShowSuggestions(false);
        return;
      }
    }

    if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (pastCommands.length > 0 && cmdIndex + 1 < pastCommands.length) {
        const nextIdx = cmdIndex + 1;
        setCmdIndex(nextIdx);
        setInput(pastCommands[nextIdx]);
      }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (cmdIndex > 0) {
        const nextIdx = cmdIndex - 1;
        setCmdIndex(nextIdx);
        setInput(pastCommands[nextIdx]);
      } else if (cmdIndex === 0) {
        setCmdIndex(-1);
        setInput('');
      }
    } else if (e.key === 'Escape') {
      setShowSuggestions(false);
    }
  };

  const handleInputChange = (e) => {
    const val = e.target.value;
    setInput(val);
    setShowSuggestions(val.trim().length > 0 && !val.includes(' '));
    setSelectedSuggestionIdx(0);
  };

  const applySuggestion = (cmd) => {
    setInput(`${cmd} `);
    setShowSuggestions(false);
    if (inputRef.current) inputRef.current.focus();
  };

  return (
    <div className="panel-card console-panel" data-testid="console-panel">
      <div className="panel-header">
        <div>
          <h2 className="panel-title">Interactive Command Console</h2>
          <span className="panel-desc">Direct RESP2 proxy with syntax hints and execution latency</span>
        </div>
        <div className="quick-actions">

          <button type="button" className="quick-btn" onClick={() => setInput('SET mykey "Hello World"')}>
            SET
          </button>
          <button type="button" className="quick-btn" onClick={() => setInput('GET mykey')}>
            GET
          </button>
          <button type="button" className="quick-btn" onClick={() => setInput('TTL mykey')}>
            TTL
          </button>
          <button type="button" className="quick-btn" onClick={() => setInput('DEL mykey')}>
            DEL
          </button>
          <button type="button" className="quick-btn" onClick={() => setInput('KEYS *')}>
            KEYS *
          </button>
          <button
            type="button"
            className="quick-btn btn-danger"
            onClick={() => setHistory([])}
            title="Clear Console Output"
          >
            Clear
          </button>
        </div>
      </div>

      <div className="console-display" data-testid="console-display">
        {history.map((item, i) => (
          <div key={i} className={`console-line line-${item.type}`} data-testid={`console-line-${item.type}`}>
            {item.type === 'input' && <span className="prompt-arrow">&gt;&nbsp;</span>}
            <pre className="console-text">{item.text}</pre>
            {item.latency && <span className="console-latency-tag">⚡ {item.latency}</span>}
          </div>
        ))}
        {loading && (
          <div className="console-line line-system">
            <span className="prompt-arrow">&gt;&nbsp;</span>
            <span className="console-text">Executing command...</span>
          </div>
        )}
        <div ref={consoleBottomRef} />
      </div>

      {/* Syntax Helper Banner */}
      {activeHint && (
        <div className="syntax-hint-banner">
          <span className="syntax-title">Syntax:</span>
          <code className="syntax-code">{activeHint.syntax}</code>
          <span className="syntax-desc">— {activeHint.desc}</span>
        </div>
      )}

      {/* Autocomplete Popup */}
      <div className="console-form-wrapper">
        {showSuggestions && suggestions.length > 0 && (
          <div className="autocomplete-popup" data-testid="autocomplete-popup">
            {suggestions.map((s, i) => (
              <div
                key={s.cmd}
                className={`autocomplete-item ${i === selectedSuggestionIdx ? 'highlighted' : ''}`}
                onClick={() => applySuggestion(s.cmd)}
              >
                <span className="auto-cmd">{s.cmd}</span>
                <span className="auto-syntax">{s.syntax}</span>
                <span className="tab-hint">Tab ⇥</span>
              </div>
            ))}
          </div>
        )}

        <form className="console-form" onSubmit={handleSubmit} data-testid="console-form">
          <span className="terminal-prefix">redis&gt;</span>
          <input
            ref={inputRef}
            type="text"
            className="console-input"
            data-testid="console-input"
            value={input}
            onChange={handleInputChange}
            onKeyDown={handleKeyDown}
            placeholder={online ? 'Enter command (e.g. SET user:1 Alice EX 60)... Tab for autocomplete' : 'Server offline...'}
            disabled={!online || loading}
            autoFocus
          />
          <button
            type="submit"
            className="console-submit-btn"
            data-testid="console-submit"
            disabled={!online || loading || !input.trim()}
          >
            Execute
          </button>
        </form>
      </div>
    </div>
  );
}

export const CommandConsole = React.memo(CommandConsoleComponent);
