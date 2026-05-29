// artboards/main-view.jsx — three directions for Vane's main performance view.
// Exports: window.MainViewA, MainViewB, MainViewC

/* eslint-disable no-undef */

const VnSeg = ({ items, value, onChange }) => (
  <div className="vn-seg">
    {items.map(([k, label]) => (
      <button key={k} className={value === k ? 'is-active' : ''} onClick={() => onChange && onChange(k)}>{label}</button>
    ))}
  </div>
);

const Meter = ({ label, kind, value, cc }) => (
  <div style={{ display: 'grid', gridTemplateColumns: 'auto 1fr auto', gap: 10, alignItems: 'center' }}>
    <span style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 12, fontWeight: 600 }}>
      <span className={`vn-dot ${kind === 'breath' ? 'b' : kind === 'expr' ? 'e' : kind === 'pressure' ? 'p' : kind === 'slide' ? 's' : 'bend'}`} />
      {label}
    </span>
    <div className={`vn-meter is-${kind}`}><div className="vn-meter-fill" style={{ width: `${value}%` }} /></div>
    <span className="vn-num" style={{ fontSize: 11, color: 'var(--vn-muted)' }}>{cc}</span>
  </div>
);

const ParamSlider = ({ label, value, suffix, color = 'var(--vn-breath)' }) => (
  <div>
    <div style={{ display: 'flex', justifyContent: 'space-between', fontSize: 11, color: 'var(--vn-muted)', marginBottom: 4, fontWeight: 600 }}>
      <span style={{ textTransform: 'uppercase', letterSpacing: '.1em' }}>{label}</span>
      <span className="vn-num" style={{ color: 'var(--vn-text)' }}>{value}{suffix}</span>
    </div>
    <div className="vn-slider"><div className="vn-slider-fill" style={{ width: `${Math.min(100, Math.max(0, value > 1 ? value : value * 100))}%`, background: color }} /></div>
  </div>
);

// ---------- BreathCurve: an SVG breath flow line, vane motif ----------
const BreathCurve = ({ stroke = 'var(--vn-breath)', opacity = 1, dasharray, seed = 0, amp = 14, baseline = 50, w = 600 }) => {
  // deterministic “breath” path: a flowing sine + drift
  const pts = [];
  for (let i = 0; i <= 60; i++){
    const x = (i / 60) * w;
    const y = baseline + Math.sin(i * 0.32 + seed) * amp + Math.sin(i * 0.13 + seed * 1.7) * amp * 0.6;
    pts.push(`${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`);
  }
  return <path d={pts.join(' ')} fill="none" stroke={stroke} strokeWidth={2.5} strokeLinecap="round" opacity={opacity} strokeDasharray={dasharray} />;
};

// ===================================================================
// A · STAGE — DrawnQurve-sibling layout, single-page, warm light
// ===================================================================
function MainViewA(){
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start', gap: 12 }}>
        <div>
          <div className="vn-eyebrow">Vane · Performance</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Breath Stage</div>
        </div>
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span className="vn-chip"><span className="vn-chip-dot" style={{ background: 'var(--vn-expr)' }} />Sylphyo</span>
          <span className="vn-chip is-ghost">12-EDO · MTS-ESP</span>
          <button className="vn-icon-btn" title="Panic">!</button>
          <button className="vn-icon-btn">⌘</button>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1.55fr 1fr', gap: 12, flex: 1, minHeight: 0 }}>
        {/* Stage */}
        <div className="vn-panel" style={{ padding: 12, display: 'flex', flexDirection: 'column', gap: 10 }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <div className="vn-eyebrow">Stage · live</div>
            <div style={{ display: 'flex', gap: 6 }}>
              <span className="vn-chip is-active">Breath</span>
              <span className="vn-chip">Spectrum</span>
              <span className="vn-chip">Pitch</span>
            </div>
          </div>
          <div className="vn-panel-sub vn-stage-bg" style={{ flex: 1, position: 'relative', borderRadius: 14, overflow: 'hidden', minHeight: 0 }}>
            <svg viewBox="0 0 600 220" preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
              {/* secondary flow */}
              <BreathCurve stroke="var(--vn-slide)" opacity={0.22} dasharray="4 6" baseline={80} amp={26} seed={1.4} />
              <BreathCurve stroke="var(--vn-pressure)" opacity={0.25} dasharray="4 6" baseline={150} amp={20} seed={2.6} />
              {/* primary breath */}
              <BreathCurve stroke="var(--vn-breath)" baseline={120} amp={36} seed={0.4} />
            </svg>
            <div style={{ position: 'absolute', top: 10, right: 12, display: 'flex', gap: 6 }}>
              <span className="vn-chip" style={{ background: 'rgba(255,255,255,.8)', backdropFilter: 'blur(4px)' }}>♪ A3</span>
              <span className="vn-chip" style={{ background: 'rgba(255,255,255,.8)' }}>Saw · LP 1.8k</span>
            </div>
            <div style={{ position: 'absolute', bottom: 10, left: 12, fontFamily: 'var(--vn-mono)', fontSize: 11, color: 'var(--vn-muted)' }}>
              t = 4.213s &nbsp; · &nbsp; ch 1
            </div>
          </div>

          {/* Mod source meters — required must-have, always visible */}
          <div className="vn-panel-sub" style={{ padding: 10, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
            <Meter label="Breath" kind="breath" value={72} cc="CC2" />
            <Meter label="Expression" kind="expr" value={38} cc="CC11" />
            <Meter label="Pressure" kind="pressure" value={54} cc="MPE Z" />
            <Meter label="Slide" kind="slide" value={61} cc="CC74" />
          </div>
        </div>

        {/* Right rail */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10, minHeight: 0 }}>
          <div className="vn-panel" style={{ padding: 12 }}>
            <div className="vn-eyebrow">Voice</div>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginTop: 4, marginBottom: 8 }}>
              <div className="vn-h2">Reedlike · Lyrical</div>
              <VnSeg items={[['mono', 'Mono'], ['poly', 'Poly']]} value="mono" />
            </div>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 10 }}>
              <ParamSlider label="Cutoff" value={65} suffix=" %" />
              <ParamSlider label="Reso" value={32} suffix=" %" />
              <ParamSlider label="Glide" value={12} suffix=" ms" />
              <ParamSlider label="Vel→VCA" value={0} suffix="" />
            </div>
            <div style={{ display: 'flex', gap: 6, marginTop: 10, flexWrap: 'wrap' }}>
              <span className="vn-pill is-active">Saw</span>
              <span className="vn-pill">Tri</span>
              <span className="vn-pill">Sq</span>
              <span className="vn-pill">Sine</span>
              <span className="vn-pill">Noise</span>
              <span className="vn-pill">LP/BP/HP</span>
            </div>
          </div>

          <div className="vn-panel" style={{ padding: 12, flex: 1, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 8 }}>
              <div className="vn-eyebrow">ModMatrix · 6 routes</div>
              <button className="vn-btn">Open</button>
            </div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 6, fontSize: 12, overflow: 'auto', minHeight: 0 }}>
              {[
                ['b', 'Breath', '→', 'VCA Level', 1.0],
                ['e', 'Expression', '→', 'VCA Level', 1.0],
                ['b', 'Breath', '→', 'Cutoff', 0.5],
                ['e', 'Expression', '→', 'Cutoff', 0.5],
                ['p', 'Pressure', '→', 'VCA Level', 0.5],
                ['s', 'Slide', '⇄', 'Cutoff', 0.5],
              ].map((r, i) => (
                <div key={i} style={{ display: 'grid', gridTemplateColumns: 'auto 1fr auto auto', gap: 8, alignItems: 'center', padding: '6px 8px', borderRadius: 8, background: i % 2 ? 'var(--vn-panel2)' : 'transparent' }}>
                  <span className={`vn-dot ${r[0]}`} />
                  <span style={{ fontWeight: 500 }}>{r[1]} <span style={{ color: 'var(--vn-muted)' }}>{r[2]}</span> {r[3]}</span>
                  <span className="vn-num" style={{ color: 'var(--vn-muted)' }}>×{r[4].toFixed(2)}</span>
                  <span className="vn-meter is-breath" style={{ width: 40, height: 6 }}><span className="vn-meter-fill" style={{ width: `${30 + i * 8}%` }} /></span>
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>

      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 8 }}>
        ↑ DrawnQurve sibling — stage hero, right rail, breath ribbons replace curves
      </div>
    </div>
  );
}

// ===================================================================
// B · WIND TUNNEL — full-width horizontal flow, parameters as inline stations
// ===================================================================
function MainViewB(){
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Vane · Wind tunnel</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Reedlike · Lyrical</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <span className="vn-chip"><span className="vn-chip-dot" style={{ background: 'var(--vn-pressure)' }} />Exquis</span>
          <span className="vn-chip is-ghost">Saw · LP · Mono</span>
          <button className="vn-icon-btn">!</button>
        </div>
      </div>

      {/* The tunnel: full-width canvas with stations */}
      <div className="vn-panel" style={{ padding: 0, flex: 1, position: 'relative', overflow: 'hidden', minHeight: 0 }}>
        <div className="vn-stage-bg" style={{ position: 'absolute', inset: 0 }} />
        <svg viewBox="0 0 800 300" preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
          <defs>
            <linearGradient id="windGrad" x1="0" x2="1">
              <stop offset="0" stopColor="var(--vn-breath)" stopOpacity="0.05" />
              <stop offset="0.4" stopColor="var(--vn-breath)" stopOpacity="0.55" />
              <stop offset="1" stopColor="var(--vn-breath)" stopOpacity="0.85" />
            </linearGradient>
          </defs>
          {/* breath ribbons, multiple lanes flowing right */}
          {[0, 1, 2, 3, 4, 5].map(i => (
            <BreathCurve key={i} w={800}
              stroke="url(#windGrad)"
              opacity={0.55 - i * 0.06}
              baseline={70 + i * 28}
              amp={6 + (i % 3) * 4}
              seed={i * 0.7} />
          ))}
          {/* vertical station markers */}
          {[160, 320, 480, 640].map(x => (
            <line key={x} x1={x} x2={x} y1={20} y2={280} stroke="rgba(0,0,0,.08)" strokeDasharray="2 4" />
          ))}
        </svg>
        {/* Stations overlay */}
        <div style={{ position: 'absolute', inset: 0, display: 'grid', gridTemplateColumns: 'repeat(5, 1fr)', alignItems: 'stretch' }}>
          {[
            { eye: 'In', title: 'Breath', val: '72', kind: 'breath', sub: 'CC2 · slewed 5 / 80 ms' },
            { eye: 'Shape', title: 'Wave + Detune', val: 'Saw', kind: '', sub: 'detune 0 ¢' },
            { eye: 'Filter', title: 'Cytomic SVF', val: '1.8k Hz', kind: '', sub: 'LP · Q 0.32' },
            { eye: 'Amp', title: 'VCA', val: '0 dB', kind: '', sub: 'wind-first · vel 0' },
            { eye: 'Out', title: 'Mono · Ch 1', val: '♪ A3', kind: '', sub: 'MTS-ESP locked' },
          ].map((s, i) => (
            <div key={i} style={{ borderLeft: i ? '1px dashed rgba(0,0,0,.08)' : 'none', padding: 14, display: 'flex', flexDirection: 'column', justifyContent: 'space-between', gap: 6 }}>
              <div>
                <div className="vn-eyebrow">{s.eye}</div>
                <div className="vn-h2" style={{ marginTop: 2 }}>{s.title}</div>
              </div>
              <div>
                <div className="vn-num" style={{ fontSize: 18, fontWeight: 700, letterSpacing: '-.02em' }}>{s.val}</div>
                <div style={{ fontSize: 11, color: 'var(--vn-muted)', marginTop: 2 }}>{s.sub}</div>
              </div>
            </div>
          ))}
        </div>
      </div>

      {/* Bottom: source meters + tiny mod matrix peek */}
      <div style={{ display: 'grid', gridTemplateColumns: '1.4fr 1fr', gap: 10 }}>
        <div className="vn-panel" style={{ padding: 10 }}>
          <div className="vn-eyebrow" style={{ marginBottom: 6 }}>Sources</div>
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
            <Meter label="Breath" kind="breath" value={72} cc="CC2" />
            <Meter label="Expression" kind="expr" value={38} cc="CC11" />
            <Meter label="Pressure" kind="pressure" value={54} cc="MPE" />
            <Meter label="Slide" kind="slide" value={61} cc="CC74" />
          </div>
        </div>
        <div className="vn-panel" style={{ padding: 10, display: 'flex', alignItems: 'center', gap: 10 }}>
          <div style={{ flex: 1 }}>
            <div className="vn-eyebrow">ModMatrix · 6 routes active</div>
            <div style={{ display: 'flex', gap: 4, marginTop: 6 }}>
              {[40, 65, 30, 50, 38, 70].map((v, i) => (
                <div key={i} className="vn-meter is-breath" style={{ flex: 1, height: 14, borderRadius: 4 }}><div className="vn-meter-fill" style={{ width: `${v}%`, borderRadius: 4 }} /></div>
              ))}
            </div>
          </div>
          <button className="vn-btn">Open →</button>
        </div>
      </div>

      <div className="vn-anno" style={{ position: 'absolute', left: 24, bottom: 8 }}>
        ↑ signal flows L→R as a wind tunnel, stations replace pages
      </div>
    </div>
  );
}

// ===================================================================
// C · STAGE / DARK — performance mode, meters dominate, "headless-prep"
// ===================================================================
function MainViewC(){
  return (
    <div className="vn vn-dark vn-board" style={{ background: 'var(--vn-d-bg)' }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Vane · Performance mode</div>
          <div className="vn-h1" style={{ marginTop: 2, color: 'var(--vn-d-text)' }}>Reedlike · Lyrical</div>
        </div>
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span className="vn-chip is-active">Sylphyo · 8ch</span>
          <span className="vn-chip">★ Sweet spot</span>
          <button className="vn-icon-btn">◐</button>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, flex: 1, minHeight: 0 }}>
        {/* big meters — performance feedback */}
        <div className="vn-panel" style={{ padding: 14, display: 'flex', flexDirection: 'column', gap: 14 }}>
          <div className="vn-eyebrow">Live expression</div>
          {[
            { label: 'Breath', kind: 'breath', value: 72, cc: 'CC2' },
            { label: 'Expression', kind: 'expr', value: 38, cc: 'CC11' },
            { label: 'Pressure', kind: 'pressure', value: 54, cc: 'MPE Z' },
            { label: 'Slide', kind: 'slide', value: 61, cc: 'CC74' },
            { label: 'Pitchbend', kind: 'bend', value: 50, cc: 'PB' },
          ].map(m => (
            <div key={m.label}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 4 }}>
                <span style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 13, fontWeight: 600 }}>
                  <span className={`vn-dot ${m.kind === 'breath' ? 'b' : m.kind === 'expr' ? 'e' : m.kind === 'pressure' ? 'p' : m.kind === 'slide' ? 's' : 'bend'}`} />
                  {m.label}
                </span>
                <span className="vn-num" style={{ fontSize: 18, fontWeight: 700 }}>{m.value}<span style={{ color: 'var(--vn-d-muted)', fontWeight: 400, fontSize: 11 }}> /127</span></span>
              </div>
              <div className={`vn-meter is-${m.kind}`} style={{ height: 14 }}><div className="vn-meter-fill" style={{ width: `${m.value}%` }} /></div>
              <div style={{ fontSize: 10, color: 'var(--vn-d-muted)', marginTop: 4, fontFamily: 'var(--vn-mono)' }}>{m.cc}</div>
            </div>
          ))}
        </div>

        <div style={{ display: 'flex', flexDirection: 'column', gap: 10, minHeight: 0 }}>
          {/* breath visualiser */}
          <div className="vn-panel vn-stage-bg" style={{ position: 'relative', flex: 1, overflow: 'hidden', minHeight: 0 }}>
            <svg viewBox="0 0 400 220" preserveAspectRatio="none" style={{ position: 'absolute', inset: 0, width: '100%', height: '100%' }}>
              <BreathCurve w={400} stroke="var(--vn-breath)" baseline={120} amp={48} seed={0.3} />
              <BreathCurve w={400} stroke="var(--vn-pressure)" opacity={0.45} dasharray="3 5" baseline={170} amp={18} seed={1.7} />
            </svg>
            <div style={{ position: 'absolute', top: 10, left: 12, display: 'flex', gap: 6 }}>
              <span className="vn-chip is-active">Live</span>
              <span className="vn-chip">A3 · Saw</span>
            </div>
          </div>

          {/* headless-prep summary — small */}
          <div className="vn-panel" style={{ padding: 12 }}>
            <div className="vn-eyebrow" style={{ marginBottom: 6 }}>Headless ready · MODEP</div>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6, fontSize: 11, color: 'var(--vn-d-muted)' }}>
              <div>● 6 routes locked</div>
              <div>● MTS-ESP cached</div>
              <div>● Sylphyo profile</div>
              <div>● Saved as <span style={{ color: 'var(--vn-d-text)', fontFamily: 'var(--vn-mono)' }}>reed_lyrical.vane</span></div>
            </div>
          </div>
        </div>
      </div>

      <div className="vn-anno" style={{ position: 'absolute', right: 24, bottom: 8, color: '#b6a47e' }}>
        ↑ dark / stage mode — for the gig + headless prep
      </div>
    </div>
  );
}

Object.assign(window, { MainViewA, MainViewB, MainViewC, VnSeg, Meter, ParamSlider, BreathCurve });
