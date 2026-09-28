import React, { useState, useRef, useEffect } from 'react';

export function CommandConsole({ onExecute, online, prefillCommand }) {
  const [input, setInput] = useState('');
  const [history, setHistory] = useState([
    {
      type: 'system',
      text: 'Connected to MiniRedis Console. Type a command (e.g. SET foo bar) or click quick actions below.'
    }
  ]);
  const [cmdIndex, setCmdIndex] = useState(-1);
  const [pastCommands, setPastCommands] = useState([]);
  const [loading, setLoading] = useState(false);
  const consoleBottomRef = useRef(null);

  useEffect(() => {
    if (prefillCommand) {
      setInput(prefillCommand);
    }
  }, [prefillCommand]);

  useEffect(() => {
    if (typeof consoleBottomRef.current?.scrollIntoView === 'function') {
      consoleBottomRef.current.scrollIntoView({ behavior: 'smooth' });
    }
  }, [history]);

  const handleSubmit = async (e) => {
    if (e) e.preventDefault();
    const trimmed = input.trim();
    if (!trimmed || loading) return;

    if (!online) {
      setHistory((prev) => [
        ...prev,
        { type: 'input', text: trimmed },
        { type: 'error', text: 'Error: MiniRedis server is offline. Cannot execute command.' }
      ]);
      setInput('');
      return;
    }

    setPastCommands((prev) => [trimmed, ...prev]);
    setCmdIndex(-1);
    setLoading(true);

    setHistory((prev) => [...prev, { type: 'input', text: trimmed }]);
    setInput('');

    try {
      const res = await onExecute(trimmed);
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

      setHistory((prev) => [...prev, { type: 'output', text: formattedResult }]);
    } catch (err) {
      setHistory((prev) => [...prev, { type: 'error', text: `(error) ${err.message}` }]);
    } finally {
      setLoading(false);
    }
  };

  const handleKeyDown = (e) => {
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
    }
  };

  const setQuickCommand = (cmd) => {
    setInput(cmd);
  };

  return (
    <div className="panel-card console-panel" data-testid="console-panel">
      <div className="panel-header">
        <div>
          <h2 className="panel-title">Interactive Command Console</h2>
          <span className="panel-desc">Direct RESP2 proxy with real-time feedback</span>
        </div>
        <div className="quick-actions">
          <button type="button" className="quick-btn" onClick={() => setQuickCommand('SET mykey "Hello World"')}>
            SET
          </button>
          <button type="button" className="quick-btn" onClick={() => setQuickCommand('GET mykey')}>
            GET
          </button>
          <button type="button" className="quick-btn" onClick={() => setQuickCommand('TTL mykey')}>
            TTL
          </button>
          <button type="button" className="quick-btn" onClick={() => setQuickCommand('DEL mykey')}>
            DEL
          </button>
          <button type="button" className="quick-btn" onClick={() => setQuickCommand('KEYS *')}>
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

      <form className="console-form" onSubmit={handleSubmit} data-testid="console-form">
        <span className="terminal-prefix">redis&gt;</span>
        <input
          type="text"
          className="console-input"
          data-testid="console-input"
          value={input}
          onChange={(e) => setInput(e.target.value)}
          onKeyDown={handleKeyDown}
          placeholder={online ? 'Enter command (e.g. SET alpha 100 EX 60)...' : 'Server offline...'}
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
  );
}
