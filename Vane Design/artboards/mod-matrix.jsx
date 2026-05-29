// artboards/mod-matrix.jsx — three ModMatrix directions.
// Exports: MMGrid, MMStack, MMFlow

/* eslint-disable no-undef */

const SOURCES = [
  { id: 'breath',   label: 'Breath',     cc: 'CC2',     dot: 'b' },
  { id: 'expr',     label: 'Expression', cc: 'CC11',    dot: 'e' },
  { id: 'pressure', label: 'Pressure',   cc: 'MPE Z',   dot: 'p' },
  { id: 'slide',    label: 'Slide',      cc: 'CC74',    dot: 's' },
  { id: 'bend',     label: 'Pitchbend',  cc: 'PB',      dot: 'bend' },
  { id: 'vel',      label: 'Velocity',   cc: 'vel',     dot: 'v' },
];

const DESTS = [
  'VCA Level', 'Cutoff', 'Reso', 'Detune', 'Glide', 'Pitch',
];

// values[source][dest] = amount (-1..1). 0 = no route.
const SAMPLE_VALUES = {
  breath:   { 'VCA Level': 1.0, 'Cutoff': 0.5 },
  expr:     { 'VCA Level': 1.0, 'Cutoff': 0.5 },
  pressure: { 'VCA Level': 0.5, 'Cutoff': 0.3 },
  slide:    { 'Cutoff':  -0.6, 'Detune': 0.2 },
  bend:     { 'Pitch':   0.4 },
  vel:      { 'VCA Level': 0.1 },
};

// =====================================================================
// A · GRID — sources × destinations spreadsheet
// =====================================================================
function MMGrid(){
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">ModMatrix · grid</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Sources × destinations</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <span className="vn-chip is-active">Amount</span>
          <span className="vn-chip">Curve</span>
          <span className="vn-chip">Slew</span>
          <button className="vn-btn">+ Route</button>
        </div>
      </div>

      <div className="vn-panel" style={{ padding: 16, flex: 1, overflow: 'auto', minHeight: 0 }}>
        <table style={{ width: '100%', borderCollapse: 'separate', borderSpacing: 0, fontSize: 12 }}>
          <thead>
            <tr>
              <th style={{ textAlign: 'left', padding: '8px 10px', position: 'sticky', left: 0, background: 'var(--vn-panel)' }}></th>
              {DESTS.map(d => (
                <th key={d} style={{ textAlign: 'center', padding: '8px 4px', color: 'var(--vn-muted)', fontWeight: 600, textTransform: 'uppercase', fontSize: 10, letterSpacing: '.1em', whiteSpace: 'nowrap' }}>
                  {d}
                </th>
              ))}
            </tr>
          </thead>
          <tbody>
            {SOURCES.map(s => (
              <tr key={s.id}>
                <td style={{ padding: '8px 10px', position: 'sticky', left: 0, background: 'var(--vn-panel)' }}>
                  <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
                    <span className={`vn-dot ${s.dot}`} />
                    <div>
                      <div style={{ fontWeight: 600 }}>{s.label}</div>
                      <div style={{ fontSize: 10, fontFamily: 'var(--vn-mono)', color: 'var(--vn-muted)' }}>{s.cc}</div>
                    </div>
                  </div>
                </td>
                {DESTS.map(d => {
                  const v = (SAMPLE_VALUES[s.id] || {})[d];
                  return (
                    <td key={d} style={{ padding: 3 }}>
                      <ModCell sourceId={s.id} value={v} />
                    </td>
                  );
                })}
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <div style={{ display: 'flex', gap: 10, alignItems: 'center', fontSize: 11, color: 'var(--vn-muted)' }}>
        <span className="vn-anno" style={{ fontSize: 17 }}>tap a cell ↑ to set amount + curve. empty cells stay quiet.</span>
      </div>
    </div>
  );
}

function ModCell({ sourceId, value }){
  if (value == null || value === 0){
    return (
      <div style={{ height: 36, border: '1px dashed var(--vn-line)', borderRadius: 8, opacity: 0.5 }} />
    );
  }
  const pos = value > 0;
  const mag = Math.min(1, Math.abs(value));
  const colorBySrc = {
    breath: 'var(--vn-breath)', expr: 'var(--vn-expr)', pressure: 'var(--vn-pressure)',
    slide: 'var(--vn-slide)', bend: 'var(--vn-bend)', vel: 'var(--vn-vel)',
  };
  const tintBySrc = {
    breath: 'var(--vn-breath-tint)', expr: 'var(--vn-expr-tint)', pressure: 'var(--vn-pressure-tint)',
    slide: 'var(--vn-slide-tint)', bend: 'var(--vn-bend-tint)', vel: '#ece1ff',
  };
  return (
    <div style={{
      position: 'relative', height: 36, borderRadius: 8,
      background: tintBySrc[sourceId], border: `1px solid ${colorBySrc[sourceId]}`,
      display: 'flex', flexDirection: 'column', justifyContent: 'space-between', padding: '4px 6px', overflow: 'hidden',
    }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <span style={{ fontFamily: 'var(--vn-mono)', fontSize: 11, fontWeight: 700, color: colorBySrc[sourceId] }}>
          {pos ? '+' : '−'}{(mag).toFixed(2)}
        </span>
        <svg width="22" height="10" viewBox="0 0 22 10">
          <path d={pos ? 'M1,9 C8,9 12,1 21,1' : 'M1,1 C8,1 12,9 21,9'} fill="none" stroke={colorBySrc[sourceId]} strokeWidth="1.5" strokeLinecap="round" />
        </svg>
      </div>
      {/* amount bar */}
      <div style={{ position: 'relative', height: 4, borderRadius: 999, background: 'rgba(0,0,0,.05)' }}>
        <div style={{ position: 'absolute', left: pos ? '50%' : `${50 - mag * 50}%`, top: 0, bottom: 0, width: `${mag * 50}%`, background: colorBySrc[sourceId], borderRadius: 999 }} />
        <div style={{ position: 'absolute', left: '50%', top: -2, bottom: -2, width: 1, background: 'rgba(0,0,0,.15)' }} />
      </div>
    </div>
  );
}

// =====================================================================
// B · STACK — Bitwig-style modulator stack
// =====================================================================
function MMStack(){
  const routes = [
    { src: 'Breath',     scc: 'CC2',   sdot: 'b', amount: 1.00, dest: 'VCA Level',  curve: 'lin',  atk: 5,  rel: 80 },
    { src: 'Expression', scc: 'CC11',  sdot: 'e', amount: 1.00, dest: 'VCA Level',  curve: 'lin',  atk: 5,  rel: 80 },
    { src: 'Breath',     scc: 'CC2',   sdot: 'b', amount: 0.50, dest: 'Cutoff',     curve: 'exp',  atk: 5,  rel: 80 },
    { src: 'Expression', scc: 'CC11',  sdot: 'e', amount: 0.50, dest: 'Cutoff',     curve: 'lin',  atk: 5,  rel: 80 },
    { src: 'Pressure',   scc: 'MPE',   sdot: 'p', amount: 0.50, dest: 'VCA Level',  curve: 'exp',  atk: 3,  rel: 50 },
    { src: 'Slide',      scc: 'CC74',  sdot: 's', amount: 0.50, dest: 'Cutoff',     curve: 'bip',  atk: 2,  rel: 20 },
  ];
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">ModMatrix · stack</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Routes</div>
        </div>
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span className="vn-chip is-active">All</span>
          <span className="vn-chip">Active</span>
          <span className="vn-chip">By source</span>
          <button className="vn-btn">+ New route</button>
        </div>
      </div>

      <div className="vn-panel" style={{ padding: 10, flex: 1, overflow: 'auto', minHeight: 0, display: 'flex', flexDirection: 'column', gap: 6 }}>
        {routes.map((r, i) => (
          <div key={i} className="vn-row" style={{ gridTemplateColumns: 'auto 1.1fr auto 1fr auto auto auto', padding: '10px 12px' }}>
            <span className={`vn-dot ${r.sdot}`} />
            <div>
              <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                <span style={{ fontWeight: 600 }}>{r.src}</span>
                <span style={{ fontFamily: 'var(--vn-mono)', fontSize: 10, color: 'var(--vn-muted)' }}>{r.scc}</span>
              </div>
              <div className={`vn-meter is-${r.sdot === 'b' ? 'breath' : r.sdot === 'e' ? 'expr' : r.sdot === 'p' ? 'pressure' : r.sdot === 's' ? 'slide' : 'bend'}`} style={{ height: 5, marginTop: 5 }}>
                <span className="vn-meter-fill" style={{ width: `${30 + i * 9}%` }} />
              </div>
            </div>
            <span style={{ color: 'var(--vn-muted)', fontSize: 18 }}>→</span>
            <div>
              <div style={{ fontWeight: 600 }}>{r.dest}</div>
              <div style={{ fontSize: 11, color: 'var(--vn-muted)' }}>amount × source meter</div>
            </div>
            {/* amount slider */}
            <div style={{ width: 140 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 10, color: 'var(--vn-muted)' }}>
                <span>−1</span><span className="vn-num" style={{ color: 'var(--vn-text)', fontWeight: 700 }}>{r.amount.toFixed(2)}</span><span>+1</span>
              </div>
              <div className="vn-slider" style={{ marginTop: 4 }}>
                <div className="vn-slider-fill" style={{ width: `${50 + r.amount * 50}%`, background: 'var(--vn-breath)' }} />
              </div>
            </div>
            <div className="vn-seg" style={{ padding: 3 }}>
              <button className={r.curve === 'lin' ? 'is-active' : ''}>lin</button>
              <button className={r.curve === 'exp' ? 'is-active' : ''}>exp</button>
              <button className={r.curve === 'bip' ? 'is-active' : ''}>±</button>
            </div>
            <div style={{ display: 'flex', gap: 6, fontFamily: 'var(--vn-mono)', fontSize: 11, color: 'var(--vn-muted)' }}>
              <span>↑ {r.atk}</span><span>↓ {r.rel}</span>
            </div>
          </div>
        ))}

        <div className="vn-row" style={{ gridTemplateColumns: '1fr', padding: 12, border: '1.5px dashed var(--vn-muted-2)', background: 'transparent', color: 'var(--vn-muted)', justifyItems: 'center' }}>
          + add a route — pick source, destination, amount
        </div>
      </div>

      <div className="vn-anno" style={{ position: 'absolute', right: 24, bottom: 6 }}>
        ↑ stack reads top-to-bottom like a Bitwig device chain
      </div>
    </div>
  );
}

// =====================================================================
// C · FLOW GARDEN — sources stream right into destinations; ribbons = amount
// =====================================================================
function MMFlow(){
  // layout: left column sources, right column destinations.
  // For each route, an SVG ribbon flows from src to dest, thickness = |amount|.
  const sources = [
    { y: 70,  label: 'Breath',     cc: 'CC2',    color: 'var(--vn-breath)',   tint: 'var(--vn-breath-tint)' },
    { y: 130, label: 'Expression', cc: 'CC11',   color: 'var(--vn-expr)',     tint: 'var(--vn-expr-tint)' },
    { y: 190, label: 'Pressure',   cc: 'MPE',    color: 'var(--vn-pressure)', tint: 'var(--vn-pressure-tint)' },
    { y: 250, label: 'Slide',      cc: 'CC74',   color: 'var(--vn-slide)',    tint: 'var(--vn-slide-tint)' },
    { y: 310, label: 'Pitchbend',  cc: 'PB',     color: 'var(--vn-bend)',     tint: 'var(--vn-bend-tint)' },
  ];
  const dests = [
    { y: 80,  label: 'VCA Level' },
    { y: 150, label: 'Cutoff' },
    { y: 220, label: 'Reso' },
    { y: 290, label: 'Pitch' },
  ];
  // route: [srcIdx, destIdx, amount (-1..1)]
  const routes = [
    [0, 0, 1.0],
    [1, 0, 1.0],
    [0, 1, 0.5],
    [1, 1, 0.5],
    [2, 0, 0.5],
    [3, 1, -0.6],
    [4, 3, 0.4],
  ];

  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">ModMatrix · flow garden <span className="vn-hand" style={{ fontSize: 16, color: 'var(--vn-muted)' }}>(novel)</span></div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Breath flows into parameters</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <span className="vn-chip is-active">Drag</span>
          <span className="vn-chip">Curve</span>
          <span className="vn-chip">Hide quiet</span>
        </div>
      </div>

      <div className="vn-panel" style={{ flex: 1, position: 'relative', overflow: 'hidden', minHeight: 0, padding: 0 }}>
        <div className="vn-stage-bg" style={{ position: 'absolute', inset: 0, opacity: 0.4 }} />
        <svg viewBox="0 0 700 400" preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
          <defs>
            {sources.map((s, i) => (
              <linearGradient key={i} id={`ribbon${i}`} x1="0" x2="1">
                <stop offset="0" stopColor={s.color} stopOpacity="0.65" />
                <stop offset="1" stopColor={s.color} stopOpacity="0.15" />
              </linearGradient>
            ))}
          </defs>
          {/* ribbons */}
          {routes.map(([si, di, amt], k) => {
            const s = sources[si], d = dests[di];
            const thickness = 4 + Math.abs(amt) * 22;
            const x1 = 180, x2 = 520;
            const c1x = x1 + 140, c2x = x2 - 140;
            const negative = amt < 0;
            return (
              <g key={k}>
                <path
                  d={`M${x1},${s.y} C${c1x},${s.y} ${c2x},${d.y} ${x2},${d.y}`}
                  fill="none" stroke={`url(#ribbon${si})`} strokeWidth={thickness} strokeLinecap="round"
                  opacity={0.85}
                  strokeDasharray={negative ? '6 5' : undefined}
                />
                {/* amount label at midpoint */}
                <text x={(c1x + c2x) / 2} y={(s.y + d.y) / 2 - 4} fontSize="10" fontFamily="JetBrains Mono, monospace" fill={s.color} textAnchor="middle" fontWeight="700">
                  {amt > 0 ? '+' : '−'}{Math.abs(amt).toFixed(2)}
                </text>
              </g>
            );
          })}
          {/* dotted hover route */}
          <path d={`M180,${250} C320,${250} 380,${290} 520,${290}`} fill="none" stroke="var(--vn-muted-2)" strokeWidth="2" strokeDasharray="3 4" opacity="0.7" />
        </svg>

        {/* source pucks (left) */}
        <div style={{ position: 'absolute', left: 18, top: 18, bottom: 18, width: 170, display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
          <div className="vn-eyebrow">Sources</div>
          {sources.map((s, i) => (
            <div key={i} className="vn-panel-sub" style={{ padding: '8px 10px', borderColor: s.color, background: '#fff', display: 'flex', alignItems: 'center', gap: 8 }}>
              <span className="vn-dot" style={{ background: s.color }} />
              <div style={{ flex: 1 }}>
                <div style={{ fontSize: 12, fontWeight: 600 }}>{s.label}</div>
                <div style={{ fontSize: 10, fontFamily: 'var(--vn-mono)', color: 'var(--vn-muted)' }}>{s.cc}</div>
              </div>
              <div style={{ width: 24, height: 24, borderRadius: 6, background: s.tint, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 11, fontWeight: 700, color: s.color, fontFamily: 'var(--vn-mono)' }}>
                {[72, 38, 54, 61, 50][i]}
              </div>
            </div>
          ))}
        </div>

        {/* destination pucks (right) */}
        <div style={{ position: 'absolute', right: 18, top: 18, bottom: 18, width: 170, display: 'flex', flexDirection: 'column', justifyContent: 'space-between' }}>
          <div className="vn-eyebrow" style={{ textAlign: 'right' }}>Destinations</div>
          {dests.map((d, i) => (
            <div key={i} className="vn-panel-sub" style={{ padding: '8px 10px', background: '#fff', display: 'flex', alignItems: 'center', gap: 8 }}>
              <div style={{ flex: 1 }}>
                <div style={{ fontSize: 12, fontWeight: 600 }}>{d.label}</div>
                <div style={{ fontSize: 10, color: 'var(--vn-muted)' }}>{['1.0×', '0.5×', '0.0×', '0.4×'][i]} composite</div>
              </div>
              <svg width="34" height="14"><path d="M2,12 C8,12 12,2 32,2" fill="none" stroke="var(--vn-breath)" strokeWidth="1.5" /></svg>
            </div>
          ))}
        </div>

        {/* drag-affordance hint */}
        <div style={{ position: 'absolute', top: '50%', left: '50%', transform: 'translate(-50%, -50%)', pointerEvents: 'none', textAlign: 'center', color: 'var(--vn-muted)' }}>
          <div className="vn-hand" style={{ fontSize: 19 }}>drag from a source ribbon to a destination ➜</div>
          <div className="vn-hand" style={{ fontSize: 16, opacity: 0.7 }}>thickness = |amount|, dashed = inverted</div>
        </div>
      </div>

      <div className="vn-anno" style={{ position: 'absolute', left: 24, bottom: 6 }}>
        ↑ novel: routes are visible ribbons, sized by amount. Drag to redirect.
      </div>
    </div>
  );
}

Object.assign(window, { MMGrid, MMStack, MMFlow });
