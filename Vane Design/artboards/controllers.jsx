// artboards/controllers.jsx — controller profile screens.
// Exports: CtrlA, CtrlB, CtrlC

/* eslint-disable no-undef */

// Stylised Sylphyo — a vertical wand / wind controller, abstracted to a hex-capped tube.
function SylphyoIllu({ active = 'breath' }){
  return (
    <svg viewBox="0 0 200 480" style={{ width: '100%', height: '100%' }} preserveAspectRatio="xMidYMid meet">
      <defs>
        <linearGradient id="syltube" x1="0" x2="1">
          <stop offset="0" stopColor="#e7e2d3" />
          <stop offset="0.5" stopColor="#f5f0e1" />
          <stop offset="1" stopColor="#d6cfbb" />
        </linearGradient>
      </defs>
      {/* mouthpiece */}
      <ellipse cx="100" cy="22" rx="34" ry="10" fill="#3a342a" />
      <ellipse cx="100" cy="22" rx="22" ry="6" fill="#1e1a14" />
      {/* tube */}
      <rect x="74" y="28" width="52" height="380" rx="24" fill="url(#syltube)" stroke="#9b9277" strokeWidth="1.5" />
      {/* row of key sensors */}
      {Array.from({ length: 8 }, (_, i) => (
        <g key={i} transform={`translate(100, ${80 + i * 38})`}>
          <circle r="11" fill="#fff" stroke="#9b9277" strokeWidth="1.5" />
          <circle r="5" fill="#bcb59f" />
        </g>
      ))}
      {/* base / strip */}
      <rect x="74" y="408" width="52" height="56" rx="14" fill="#2a2620" />
      <rect x="86" y="420" width="28" height="32" rx="6" fill={active === 'strip' ? 'var(--vn-slide)' : '#4a4438'} opacity={active === 'strip' ? 0.9 : 1} />

      {/* breath glow */}
      {active === 'breath' && (
        <g>
          <ellipse cx="100" cy="22" rx="46" ry="14" fill="var(--vn-breath)" opacity="0.25" />
          <path d="M70,22 Q100,8 130,22" fill="none" stroke="var(--vn-breath)" strokeWidth="2" strokeDasharray="3 4" />
        </g>
      )}
      {/* hotspot markers */}
      <g>
        <circle cx="60" cy="22" r="6" fill="var(--vn-breath)" />
        <circle cx="60" cy="170" r="6" fill="var(--vn-expr)" />
        <circle cx="60" cy="300" r="6" fill="var(--vn-pressure)" />
        <circle cx="60" cy="436" r="6" fill="var(--vn-slide)" />
      </g>
    </svg>
  );
}

function ExquisIllu({ activePad = -1 }){
  // 11×6 hex grid abstracted
  const cells = [];
  const rows = 6, cols = 11;
  const r = 14, dx = r * 1.8, dy = r * 1.55;
  for (let row = 0; row < rows; row++){
    for (let col = 0; col < cols; col++){
      const x = 30 + col * dx + (row % 2 ? dx / 2 : 0);
      const y = 30 + row * dy;
      const idx = row * cols + col;
      const hl = idx === activePad;
      cells.push(
        <polygon key={idx}
          points={hexPoints(x, y, r - 2)}
          fill={hl ? 'var(--vn-breath)' : (row % 2 === col % 3 ? '#f7f3e4' : '#ede8d5')}
          stroke="#a8a08a" strokeWidth="1" />
      );
    }
  }
  return (
    <svg viewBox="0 0 360 200" style={{ width: '100%', height: '100%' }} preserveAspectRatio="xMidYMid meet">
      <rect x="6" y="6" width="348" height="188" rx="14" fill="#dcd6c2" stroke="#a8a08a" />
      {cells}
    </svg>
  );
}
function hexPoints(cx, cy, r){
  return Array.from({ length: 6 }, (_, i) => {
    const a = (Math.PI / 3) * i - Math.PI / 6;
    return `${(cx + r * Math.cos(a)).toFixed(1)},${(cy + r * Math.sin(a)).toFixed(1)}`;
  }).join(' ');
}

function MorphIllu(){
  return (
    <svg viewBox="0 0 280 200" style={{ width: '100%', height: '100%' }} preserveAspectRatio="xMidYMid meet">
      <rect x="6" y="6" width="268" height="188" rx="20" fill="#1b1812" stroke="#3a342a" />
      <rect x="20" y="20" width="240" height="160" rx="12" fill="#2a2620" />
      {/* faint touch dots */}
      {[[80, 60], [180, 70], [120, 130], [200, 140]].map(([x, y], i) => (
        <g key={i}>
          <circle cx={x} cy={y} r="18" fill="var(--vn-slide)" opacity="0.5" />
          <circle cx={x} cy={y} r="6" fill="var(--vn-slide)" />
        </g>
      ))}
    </svg>
  );
}

function EWIIllu(){
  return (
    <svg viewBox="0 0 200 480" style={{ width: '100%', height: '100%' }} preserveAspectRatio="xMidYMid meet">
      <ellipse cx="100" cy="22" rx="30" ry="8" fill="#3a342a" />
      <rect x="80" y="28" width="40" height="400" rx="14" fill="#252320" stroke="#3a342a" />
      {/* finger plates */}
      {Array.from({ length: 9 }, (_, i) => (
        <rect key={i} x="84" y={70 + i * 36} width="32" height="22" rx="6" fill="#444038" stroke="#5a5448" />
      ))}
      {/* roller */}
      <circle cx="100" cy="450" r="14" fill="#3a342a" stroke="#5a5448" />
    </svg>
  );
}

// =====================================================================
// A · SYLPHYO PROFILE — single device with hotspots & mapping
// =====================================================================
function CtrlA(){
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Controller profile</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Aodyo Sylphyo</div>
          <div style={{ fontSize: 11, color: 'var(--vn-muted)', marginTop: 2 }}>wireless wind controller · primary MIDI in</div>
        </div>
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span className="vn-chip is-active">Active</span>
          <span className="vn-chip"><span className="vn-chip-dot" style={{ background: '#2d9d8a' }} />8 ch</span>
          <button className="vn-icon-btn">⌘</button>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '210px 1fr', gap: 12, flex: 1, minHeight: 0 }}>
        {/* device illustration with annotated hotspots */}
        <div className="vn-panel" style={{ padding: 8, position: 'relative', display: 'flex', alignItems: 'stretch', justifyContent: 'center', minHeight: 0 }}>
          <SylphyoIllu />
          {/* annotation arrows */}
          <div style={{ position: 'absolute', left: 8, top: 24, fontSize: 11, color: 'var(--vn-breath)', fontWeight: 600 }}>Breath</div>
          <div style={{ position: 'absolute', left: 8, top: '34%', fontSize: 11, color: 'var(--vn-expr)', fontWeight: 600 }}>Lip / Bite</div>
          <div style={{ position: 'absolute', left: 8, top: '60%', fontSize: 11, color: 'var(--vn-pressure)', fontWeight: 600 }}>Tilt</div>
          <div style={{ position: 'absolute', left: 8, bottom: 30, fontSize: 11, color: 'var(--vn-slide)', fontWeight: 600 }}>Strip</div>
        </div>

        {/* mapping table */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10, minHeight: 0 }}>
          <div className="vn-panel" style={{ padding: 12, flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
            <div className="vn-eyebrow" style={{ marginBottom: 8 }}>Source map</div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 6, flex: 1, minHeight: 0, overflow: 'auto' }}>
              {[
                ['Breath',     'b',    'CC2',  'breath sensor', 72],
                ['Expression', 'e',    'CC11', 'lip / bite',     38],
                ['Pressure',   'p',    'MPE Z','tilt → channel pressure', 54],
                ['Slide',      's',    'CC74', 'strip',          61],
                ['Pitchbend',  'bend', 'PB',   'strip glide',    50],
                ['Velocity',   'v',    'vel',  'attack initial', 100],
              ].map((row, i) => (
                <div key={i} className="vn-row" style={{ gridTemplateColumns: 'auto 1fr auto 1fr auto', padding: '8px 10px' }}>
                  <span className={`vn-dot ${row[1]}`} />
                  <div>
                    <div style={{ fontWeight: 600, fontSize: 12 }}>{row[0]}</div>
                    <div style={{ fontSize: 10, color: 'var(--vn-muted)' }}>{row[3]}</div>
                  </div>
                  <span style={{ fontFamily: 'var(--vn-mono)', fontSize: 11, color: 'var(--vn-muted)' }}>{row[2]}</span>
                  <div className={`vn-meter is-${row[1] === 'b' ? 'breath' : row[1] === 'e' ? 'expr' : row[1] === 'p' ? 'pressure' : row[1] === 's' ? 'slide' : 'bend'}`} style={{ height: 7 }}>
                    <div className="vn-meter-fill" style={{ width: `${row[4]}%` }} />
                  </div>
                  <button className="vn-btn">teach</button>
                </div>
              ))}
            </div>
          </div>
          <div className="vn-panel" style={{ padding: 12, display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 10 }}>
            <div>
              <div className="vn-eyebrow">MPE</div>
              <div style={{ fontWeight: 600, marginTop: 4 }}>Member ch 2–8</div>
            </div>
            <div>
              <div className="vn-eyebrow">Breath curve</div>
              <div style={{ fontWeight: 600, marginTop: 4 }}>S-curve · γ 1.8</div>
            </div>
            <div>
              <div className="vn-eyebrow">Tilt amount</div>
              <div style={{ fontWeight: 600, marginTop: 4 }}>0.6</div>
            </div>
          </div>
        </div>
      </div>
      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ stylised side view + hotspots → mapping table</div>
    </div>
  );
}

// =====================================================================
// B · EXQUIS — hex pad top view with layout + per-region CC
// =====================================================================
function CtrlB(){
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Controller profile</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Intuitive Instruments · Exquis</div>
          <div style={{ fontSize: 11, color: 'var(--vn-muted)', marginTop: 2 }}>hex grid MPE controller · 61 keys · pressure + glide</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <span className="vn-chip is-active">Wicki-Hayden</span>
          <span className="vn-chip">Harmonic</span>
          <span className="vn-chip">Custom</span>
        </div>
      </div>

      <div className="vn-panel" style={{ padding: 16, display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}>
        <div style={{ width: '100%', maxWidth: 540 }}>
          <ExquisIllu activePad={28} />
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10, flex: 1, minHeight: 0 }}>
        <div className="vn-panel" style={{ padding: 12, overflow: 'auto', minHeight: 0 }}>
          <div className="vn-eyebrow" style={{ marginBottom: 6 }}>Per-pad expression</div>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            {[
              ['X · slide',    's',    'CC74', 0.6],
              ['Y · pitch',    'bend', 'PB',    0.4],
              ['Z · pressure', 'p',    'MPE',   1.0],
              ['Initial vel',  'v',    'vel',   0.4],
            ].map((r, i) => (
              <div key={i} className="vn-row" style={{ gridTemplateColumns: 'auto 1fr auto auto', padding: '8px 10px' }}>
                <span className={`vn-dot ${r[1]}`} />
                <div style={{ fontWeight: 600 }}>{r[0]}</div>
                <span className="vn-num" style={{ fontSize: 11, color: 'var(--vn-muted)' }}>{r[2]}</span>
                <div className="vn-num" style={{ fontSize: 11, color: 'var(--vn-text)' }}>{r[3].toFixed(2)}</div>
              </div>
            ))}
          </div>
        </div>
        <div className="vn-panel" style={{ padding: 12, overflow: 'auto', minHeight: 0 }}>
          <div className="vn-eyebrow" style={{ marginBottom: 6 }}>Tuning · MTS-ESP</div>
          <div style={{ fontWeight: 600, marginBottom: 6 }}>31-EDO · ODDSound master</div>
          <div className="vn-ph" style={{ height: 50, marginBottom: 8 }}>scale strip placeholder</div>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6, fontSize: 11 }}>
            <div>Root: <strong>C</strong></div>
            <div>Member ch: <strong>2–16</strong></div>
            <div>Octave shift: <strong>+0</strong></div>
            <div>Glide cap: <strong>200 ms</strong></div>
          </div>
        </div>
      </div>
      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ hex grid top view · layout + MTS-ESP profile</div>
    </div>
  );
}

// =====================================================================
// C · MULTI-CONTROLLER PICKER
// =====================================================================
function CtrlC(){
  const ctrls = [
    { name: 'Sylphyo',  illu: <SylphyoIllu />, kind: 'wind', cc: 'CC2 · CC11 · CC74 · MPE', active: true },
    { name: 'EWI',      illu: <EWIIllu />,     kind: 'wind', cc: 'CC2 · CC11 · pitchbend' },
    { name: 'Exquis',   illu: <ExquisIllu />,  kind: 'mpe',  cc: 'MPE 2–16 · CC74' },
    { name: 'Morph',    illu: <MorphIllu />,   kind: 'mpe',  cc: 'MPE · per-region' },
  ];
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Controllers</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Pick a profile</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <span className="vn-chip is-active">Wind</span>
          <span className="vn-chip">MPE</span>
          <span className="vn-chip">All</span>
          <button className="vn-btn">+ New profile</button>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 12, flex: 1, minHeight: 0 }}>
        {ctrls.map((c, i) => (
          <div key={i} className="vn-panel" style={{ padding: 12, border: c.active ? '2px solid var(--vn-breath)' : undefined, display: 'flex', flexDirection: 'column' }}>
            <div style={{ flex: 1, minHeight: 0, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 6, background: 'var(--vn-bg)', borderRadius: 10 }}>
              {c.illu}
            </div>
            <div style={{ marginTop: 10 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
                <div className="vn-h2">{c.name}</div>
                {c.active && <span className="vn-chip is-active" style={{ padding: '2px 8px', fontSize: 10 }}>active</span>}
              </div>
              <div style={{ fontFamily: 'var(--vn-mono)', fontSize: 10, color: 'var(--vn-muted)', marginTop: 4 }}>{c.cc}</div>
              <div style={{ marginTop: 8, display: 'flex', gap: 4 }}>
                <button className="vn-btn" style={{ flex: 1 }}>Edit</button>
                <button className="vn-btn">⋯</button>
              </div>
            </div>
          </div>
        ))}
      </div>

      <div className="vn-panel" style={{ padding: 10, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <div>
          <div className="vn-eyebrow">Detected on MIDI in</div>
          <div style={{ fontSize: 12, marginTop: 4 }}><span className="vn-num" style={{ color: 'var(--vn-breath)', fontWeight: 700 }}>Sylphyo</span> auto-detected · sending CC2 + MPE</div>
        </div>
        <span className="vn-chip">Auto-switch</span>
      </div>
      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ stylised thumbnails for fast profile-switching mid-set</div>
    </div>
  );
}

Object.assign(window, { CtrlA, CtrlB, CtrlC, SylphyoIllu, ExquisIllu, MorphIllu, EWIIllu });
