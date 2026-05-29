// artboards/visualiser.jsx — realtime CC / breath visualiser, three directions.
// Exports: VisA, VisB, VisC

/* eslint-disable no-undef */

// generate a 'scrolling history' style polyline path for a given mod source
function historyPath(seed, baseline, amp, w = 600, h = 100, samples = 80){
  const pts = [];
  for (let i = 0; i <= samples; i++){
    const x = (i / samples) * w;
    const t = i * 0.18 + seed;
    const v = (Math.sin(t) * 0.6 + Math.sin(t * 2.3) * 0.3 + Math.sin(t * 0.4 + 1) * 0.2);
    const y = h - (baseline + v * amp);
    pts.push(`${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${Math.max(2, Math.min(h - 2, y)).toFixed(1)}`);
  }
  return pts.join(' ');
}

// =====================================================================
// A · MULTI-STREAM WIND TUNNEL — scrolling history per source
// =====================================================================
function VisA(){
  const streams = [
    { label: 'Breath',     color: 'var(--vn-breath)',   tint: 'var(--vn-breath-tint)',   value: 72, cc: 'CC2',    seed: 0.4 },
    { label: 'Expression', color: 'var(--vn-expr)',     tint: 'var(--vn-expr-tint)',     value: 38, cc: 'CC11',   seed: 1.1 },
    { label: 'Pressure',   color: 'var(--vn-pressure)', tint: 'var(--vn-pressure-tint)', value: 54, cc: 'MPE Z',  seed: 2.2 },
    { label: 'Slide',      color: 'var(--vn-slide)',    tint: 'var(--vn-slide-tint)',    value: 61, cc: 'CC74',   seed: 3.0 },
    { label: 'Pitchbend',  color: 'var(--vn-bend)',     tint: 'var(--vn-bend-tint)',     value: 50, cc: 'PB',     seed: 4.5 },
  ];
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Visualiser · streams</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>What the controller is doing</div>
        </div>
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span className="vn-chip is-active">10 s</span>
          <span className="vn-chip">30 s</span>
          <span className="vn-chip">2 min</span>
          <button className="vn-icon-btn">⏸</button>
        </div>
      </div>
      <div className="vn-panel" style={{ padding: 6, flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
        {streams.map((s, i) => (
          <div key={i} style={{ display: 'grid', gridTemplateColumns: '120px 1fr 60px', gap: 8, alignItems: 'center', padding: '4px 8px', borderBottom: i < streams.length - 1 ? '1px dashed var(--vn-line-soft)' : 'none', flex: 1, minHeight: 0 }}>
            <div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                <span className="vn-dot" style={{ background: s.color }} />
                <span style={{ fontWeight: 600, fontSize: 12 }}>{s.label}</span>
              </div>
              <div style={{ fontFamily: 'var(--vn-mono)', fontSize: 10, color: 'var(--vn-muted)' }}>{s.cc}</div>
            </div>
            <div style={{ position: 'relative', height: '100%', minHeight: 50 }}>
              <svg viewBox="0 0 600 100" preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
                <defs>
                  <linearGradient id={`fillA${i}`} x1="0" x2="0" y1="0" y2="1">
                    <stop offset="0" stopColor={s.color} stopOpacity="0.3" />
                    <stop offset="1" stopColor={s.color} stopOpacity="0" />
                  </linearGradient>
                </defs>
                <path d={historyPath(s.seed, s.value * 0.7, 25) + ` L600,100 L0,100 Z`} fill={`url(#fillA${i})`} />
                <path d={historyPath(s.seed, s.value * 0.7, 25)} fill="none" stroke={s.color} strokeWidth="2" strokeLinecap="round" />
                <line x1="595" x2="595" y1="0" y2="100" stroke={s.color} strokeWidth="2" />
                <circle cx="595" cy={100 - s.value * 0.7 - 5} r="4" fill={s.color} />
              </svg>
            </div>
            <div style={{ textAlign: 'right' }}>
              <div className="vn-num" style={{ fontSize: 22, fontWeight: 700, color: s.color, lineHeight: 1 }}>{s.value}</div>
              <div style={{ fontSize: 9, color: 'var(--vn-muted)' }}>/127</div>
            </div>
          </div>
        ))}
      </div>
      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ time scrolls right→left · each stream a discrete row</div>
    </div>
  );
}

// =====================================================================
// B · MPE PER-CHANNEL GRID — 15 voices side-by-side
// =====================================================================
function VisB(){
  // 8 active member channels (Sylphyo default); the rest are idle.
  const channels = Array.from({ length: 15 }, (_, i) => {
    const active = i < 6;
    const note = active ? ['A3', 'C4', 'E4', 'G4', 'B3', 'D4'][i] : null;
    const breath = active ? [70, 55, 80, 40, 60, 30][i] : 0;
    const press = active ? [50, 70, 30, 40, 55, 20][i] : 0;
    const slide = active ? [40, 30, 60, 50, 45, 25][i] : 0;
    return { ch: i + 2, active, note, breath, press, slide };
  });

  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Visualiser · MPE channels</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>15 member voices</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <span className="vn-chip is-active">Poly</span>
          <span className="vn-chip">Mono</span>
          <span className="vn-chip"><span className="vn-num">6</span>/15 active</span>
        </div>
      </div>

      <div className="vn-panel" style={{ padding: 12, flex: 1, minHeight: 0, display: 'grid', gridTemplateColumns: 'repeat(15, 1fr)', gap: 6, overflow: 'hidden' }}>
        {channels.map(c => (
          <div key={c.ch} style={{ display: 'flex', flexDirection: 'column', gap: 4, opacity: c.active ? 1 : 0.35, alignItems: 'center', minWidth: 0 }}>
            <div style={{ fontFamily: 'var(--vn-mono)', fontSize: 10, color: 'var(--vn-muted)' }}>ch{c.ch}</div>
            <div style={{ fontWeight: 700, fontSize: 14, color: c.active ? 'var(--vn-breath)' : 'var(--vn-muted-2)' }}>{c.note || '·'}</div>
            {/* breath vertical bar */}
            {[['breath', c.breath, 'var(--vn-breath)'], ['press', c.press, 'var(--vn-pressure)'], ['slide', c.slide, 'var(--vn-slide)']].map(([k, v, color]) => (
              <div key={k} style={{ width: '100%', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 2 }}>
                <div style={{ position: 'relative', width: 8, height: 70, borderRadius: 4, background: 'var(--vn-field)', overflow: 'hidden' }}>
                  <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: `${v}%`, background: color, borderRadius: 4 }} />
                </div>
                <span style={{ fontSize: 8, fontFamily: 'var(--vn-mono)', color: 'var(--vn-muted)' }}>{v}</span>
              </div>
            ))}
          </div>
        ))}
      </div>

      <div className="vn-panel" style={{ padding: 10, display: 'flex', gap: 10, alignItems: 'center' }}>
        <span className="vn-eyebrow">Legend</span>
        {[['Breath', 'var(--vn-breath)'], ['Pressure', 'var(--vn-pressure)'], ['Slide', 'var(--vn-slide)']].map(([l, c]) => (
          <span key={l} style={{ display: 'flex', alignItems: 'center', gap: 5, fontSize: 11 }}>
            <span style={{ width: 8, height: 8, borderRadius: 2, background: c }} />{l}
          </span>
        ))}
        <span style={{ flex: 1 }} />
        <span style={{ fontSize: 11, color: 'var(--vn-muted)' }}>Manager: <strong>ch 1</strong> · Members: <strong>ch 2–16</strong></span>
      </div>
      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ grid for debugging polyphonic MPE — each voice gets a stripe</div>
    </div>
  );
}

// =====================================================================
// C · INTENSITY DASHBOARD — composite + sparklines, “how loud / how bright”
// =====================================================================
function VisC(){
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Visualiser · intensity</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>How the patch is responding</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <span className="vn-chip is-active">Composite</span>
          <span className="vn-chip">Per source</span>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, flex: 1, minHeight: 0 }}>
        {/* big composite circle */}
        <div className="vn-panel" style={{ padding: 16, position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center' }}>
          <div className="vn-eyebrow" style={{ position: 'absolute', top: 12, left: 16 }}>Intensity</div>
          <svg viewBox="0 0 240 240" style={{ width: '100%', maxWidth: 260, height: 'auto' }}>
            {/* concentric mod rings */}
            {[
              ['var(--vn-breath)',    72, 110],
              ['var(--vn-expr)',      38,  90],
              ['var(--vn-pressure)',  54,  70],
              ['var(--vn-slide)',     61,  50],
            ].map(([color, value, r], i) => {
              const c = 2 * Math.PI * r;
              return (
                <g key={i}>
                  <circle cx="120" cy="120" r={r} fill="none" stroke="var(--vn-field)" strokeWidth="6" />
                  <circle cx="120" cy="120" r={r} fill="none" stroke={color} strokeWidth="6" strokeDasharray={`${c * (value / 100)} ${c}`} strokeLinecap="round" transform="rotate(-90 120 120)" />
                </g>
              );
            })}
            <text x="120" y="124" textAnchor="middle" fontSize="36" fontFamily="JetBrains Mono, monospace" fontWeight="700" fill="var(--vn-text)">68</text>
            <text x="120" y="146" textAnchor="middle" fontSize="11" fill="var(--vn-muted)">composite</text>
          </svg>
          {/* legend */}
          <div style={{ display: 'flex', gap: 12, marginTop: 8, flexWrap: 'wrap', justifyContent: 'center' }}>
            {[['Breath', 'var(--vn-breath)'], ['Expr', 'var(--vn-expr)'], ['Press', 'var(--vn-pressure)'], ['Slide', 'var(--vn-slide)']].map(([l, c]) => (
              <span key={l} style={{ display: 'flex', alignItems: 'center', gap: 5, fontSize: 11 }}>
                <span style={{ width: 8, height: 8, borderRadius: 2, background: c }} />{l}
              </span>
            ))}
          </div>
        </div>

        {/* sparklines + numeric */}
        <div className="vn-panel" style={{ padding: 10, display: 'flex', flexDirection: 'column', gap: 6, minHeight: 0, overflow: 'hidden' }}>
          {[
            { label: 'Breath',     value: 72, color: 'var(--vn-breath)',   seed: 0.4 },
            { label: 'Expression', value: 38, color: 'var(--vn-expr)',     seed: 1.1 },
            { label: 'Pressure',   value: 54, color: 'var(--vn-pressure)', seed: 2.2 },
            { label: 'Slide',      value: 61, color: 'var(--vn-slide)',    seed: 3.0 },
            { label: 'Pitchbend',  value: 50, color: 'var(--vn-bend)',     seed: 4.5 },
          ].map((m, i) => (
            <div key={i} className="vn-row" style={{ gridTemplateColumns: '80px 1fr 50px', padding: '6px 10px', flex: 1, minHeight: 0 }}>
              <div>
                <div style={{ fontSize: 11, fontWeight: 600 }}>{m.label}</div>
                <div className="vn-num" style={{ fontSize: 16, fontWeight: 700, color: m.color }}>{m.value}</div>
              </div>
              <div style={{ position: 'relative', height: '100%' }}>
                <svg viewBox="0 0 200 60" preserveAspectRatio="none" style={{ width: '100%', height: '100%' }}>
                  <path d={historyPath(m.seed, m.value * 0.4, 16, 200, 60, 40)} fill="none" stroke={m.color} strokeWidth="1.5" />
                </svg>
              </div>
              <div style={{ textAlign: 'right', fontSize: 10, color: 'var(--vn-muted)', fontFamily: 'var(--vn-mono)' }}>peak<br /><span style={{ color: 'var(--vn-text)', fontSize: 12 }}>{Math.round(m.value * 1.15)}</span></div>
            </div>
          ))}
        </div>
      </div>

      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ composite rings + sparklines · for tuning ModMatrix curves</div>
    </div>
  );
}

Object.assign(window, { VisA, VisB, VisC });
