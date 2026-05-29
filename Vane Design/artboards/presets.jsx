// artboards/presets.jsx — sweet-spot browser, three directions of vertical list w/ filters.
// Exports: PresetA, PresetB, PresetC

/* eslint-disable no-undef */

const PRESETS = [
  { name: 'Reedlike · Lyrical',     tags: ['breath', 'mono', 'soft'],       ctrl: 'sylphyo', wave: 'saw',  cut: 65, route: 6, scale: 'C Dorian',  fav: true },
  { name: 'Buzz · Clarinet-ish',    tags: ['breath', 'fat',  'reso'],       ctrl: 'sylphyo', wave: 'sq',   cut: 48, route: 7, scale: '12-EDO',    fav: false },
  { name: 'Glass Tube',             tags: ['expr',   'pure', 'high'],       ctrl: 'ewi',     wave: 'sine', cut: 88, route: 4, scale: '12-EDO',    fav: true },
  { name: 'Hex Drone',              tags: ['mpe',    'drone','wide'],       ctrl: 'exquis',  wave: 'saw',  cut: 30, route: 8, scale: 'Bohlen-P',  fav: false },
  { name: 'Touch Whisper',          tags: ['mpe',    'soft', 'noise'],      ctrl: 'morph',   wave: 'noise',cut: 75, route: 5, scale: 'A Phrygian',fav: false },
  { name: 'Bent Reed',              tags: ['breath', 'bend', 'micro'],      ctrl: 'sylphyo', wave: 'tri',  cut: 55, route: 9, scale: '31-EDO',    fav: true },
  { name: 'Wax & Wane',             tags: ['expr',   'slow', 'pad'],        ctrl: 'lightpad',wave: 'saw',  cut: 60, route: 6, scale: 'D Lydian',  fav: false },
  { name: 'Squelch · LP Wobble',    tags: ['breath', 'reso', 'sweep'],      ctrl: 'sylphyo', wave: 'saw',  cut: 35, route: 7, scale: '12-EDO',    fav: false },
  { name: 'Hex Trio',               tags: ['mpe',    'poly', 'chord'],      ctrl: 'exquis',  wave: 'tri',  cut: 70, route: 6, scale: 'C Dorian',  fav: false },
];

const TAG_COLORS = {
  breath: 'var(--vn-breath)', expr: 'var(--vn-expr)', mpe: 'var(--vn-pressure)',
  bend: 'var(--vn-bend)', slide: 'var(--vn-slide)', vel: 'var(--vn-vel)',
};

const TinyBreath = ({ seed = 0, w = 80, h = 22, color = 'var(--vn-breath)', opacity = 1 }) => {
  const pts = [];
  for (let i = 0; i <= 20; i++){
    const x = (i / 20) * w;
    const y = h/2 + Math.sin(i * 0.4 + seed) * (h/3) + Math.sin(i * 0.18 + seed * 1.3) * (h/6);
    pts.push(`${i === 0 ? 'M' : 'L'}${x.toFixed(1)},${y.toFixed(1)}`);
  }
  return <svg width={w} height={h}><path d={pts.join(' ')} fill="none" stroke={color} strokeWidth="1.5" strokeLinecap="round" opacity={opacity} /></svg>;
};

// =====================================================================
// A · CLASSIC LIST + SIDEBAR FILTERS
// =====================================================================
function PresetA(){
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Sweet spots · library</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>Browse</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <input className="vn-pill" placeholder="search…" style={{ width: 180, textAlign: 'left' }} />
          <button className="vn-btn">+ Save current</button>
        </div>
      </div>

      <div style={{ display: 'grid', gridTemplateColumns: '210px 1fr', gap: 12, flex: 1, minHeight: 0 }}>
        {/* sidebar */}
        <div className="vn-panel" style={{ padding: 12, display: 'flex', flexDirection: 'column', gap: 16, overflow: 'auto' }}>
          <div>
            <div className="vn-eyebrow" style={{ marginBottom: 6 }}>Controller</div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
              {[['Sylphyo', 3, true], ['EWI', 1, false], ['Exquis', 2, false], ['Sensel Morph', 1, false], ['Lightpad', 1, false], ['Generic MPE', 0, false]].map(([n, c, on]) => (
                <div key={n} style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', padding: '6px 8px', borderRadius: 8, background: on ? 'var(--vn-breath-tint)' : 'transparent', color: on ? '#234e7e' : 'inherit', fontWeight: on ? 600 : 500, fontSize: 12 }}>
                  <span>{n}</span><span className="vn-num" style={{ color: 'var(--vn-muted)' }}>{c}</span>
                </div>
              ))}
            </div>
          </div>
          <div>
            <div className="vn-eyebrow" style={{ marginBottom: 6 }}>Tags</div>
            <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
              {['breath', 'expr', 'mpe', 'bend', 'soft', 'reso', 'pad', 'drone', 'mono', 'poly', 'micro', 'wide'].map(t => (
                <span key={t} className={'vn-chip' + (t === 'breath' ? ' is-active' : '')} style={{ padding: '4px 8px', fontSize: 11 }}>{t}</span>
              ))}
            </div>
          </div>
          <div>
            <div className="vn-eyebrow" style={{ marginBottom: 6 }}>Tuning</div>
            <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
              {['12-EDO', '31-EDO', 'Bohlen-P', 'Custom MTS'].map(t => (
                <span key={t} className="vn-chip is-ghost" style={{ padding: '4px 8px', fontSize: 11, justifyContent: 'flex-start' }}>{t}</span>
              ))}
            </div>
          </div>
        </div>

        {/* list */}
        <div className="vn-panel" style={{ padding: 8, overflow: 'auto', minHeight: 0 }}>
          <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
            {PRESETS.map((p, i) => (
              <div key={i} className="vn-row" style={{ gridTemplateColumns: 'auto 1.4fr auto auto auto', padding: '10px 12px', background: i === 0 ? 'var(--vn-breath-tint)' : 'var(--vn-panel2)', borderColor: i === 0 ? 'var(--vn-breath)' : 'var(--vn-line-soft)' }}>
                <span style={{ width: 26, textAlign: 'center', color: p.fav ? 'var(--vn-bend)' : 'var(--vn-muted-2)', fontSize: 16 }}>{p.fav ? '★' : '☆'}</span>
                <div>
                  <div style={{ fontWeight: 600 }}>{p.name}</div>
                  <div style={{ display: 'flex', gap: 4, marginTop: 4, flexWrap: 'wrap' }}>
                    {p.tags.map(t => (
                      <span key={t} style={{ fontSize: 10, padding: '2px 6px', borderRadius: 999, background: '#fff', color: TAG_COLORS[t] || 'var(--vn-muted)', border: `1px solid ${TAG_COLORS[t] || 'var(--vn-line)'}`, fontFamily: 'var(--vn-mono)' }}>{t}</span>
                    ))}
                  </div>
                </div>
                <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'flex-end', gap: 2 }}>
                  <span style={{ fontSize: 11, color: 'var(--vn-muted)', textTransform: 'uppercase', letterSpacing: '.1em' }}>{p.ctrl}</span>
                  <span className="vn-num" style={{ fontSize: 11, color: 'var(--vn-muted)' }}>{p.route} routes</span>
                </div>
                <TinyBreath seed={i * 0.7} />
                <button className="vn-btn">Load</button>
              </div>
            ))}
          </div>
        </div>
      </div>
      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ list + sidebar filters · sane default</div>
    </div>
  );
}

// =====================================================================
// B · CARDS — richer previews, each row shows the patch's expression DNA
// =====================================================================
function PresetB(){
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Sweet spots · DNA cards</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>What this patch feels like</div>
        </div>
        <div style={{ display: 'flex', gap: 6, alignItems: 'center' }}>
          <span className="vn-chip is-active">For Sylphyo</span>
          <span className="vn-chip">All</span>
          <span className="vn-chip">★ Favs</span>
        </div>
      </div>

      <div className="vn-panel" style={{ padding: 10, flex: 1, overflow: 'auto', minHeight: 0 }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
          {PRESETS.slice(0, 6).map((p, i) => {
            const usage = { breath: [70, 30, 20, 60, 40, 55][i], expr: [40, 50, 70, 20, 30, 35][i], pressure: [10, 20, 5, 80, 60, 25][i], slide: [25, 40, 10, 50, 30, 70][i] };
            return (
              <div key={i} className="vn-row" style={{ gridTemplateColumns: '60px 1.3fr 1fr auto', padding: '10px 12px', alignItems: 'stretch' }}>
                {/* signature shape */}
                <div className="vn-panel-sub" style={{ background: 'var(--vn-bg)', display: 'flex', alignItems: 'center', justifyContent: 'center', padding: 6 }}>
                  <TinyBreath w={50} h={36} seed={i * 1.1} color="var(--vn-breath)" />
                </div>
                <div>
                  <div style={{ fontWeight: 600, fontSize: 13 }}>{p.name}</div>
                  <div style={{ fontSize: 11, color: 'var(--vn-muted)', marginTop: 2 }}>{p.scale} · {p.wave} · {p.route} routes</div>
                  <div style={{ display: 'flex', gap: 4, marginTop: 6 }}>
                    {p.tags.map(t => (
                      <span key={t} style={{ fontSize: 9, padding: '2px 6px', borderRadius: 999, background: '#fff', color: TAG_COLORS[t] || 'var(--vn-muted)', border: `1px solid ${TAG_COLORS[t] || 'var(--vn-line)'}`, fontFamily: 'var(--vn-mono)', textTransform: 'lowercase' }}>{t}</span>
                    ))}
                  </div>
                </div>
                {/* expression DNA: four tiny meters showing which sources matter for this patch */}
                <div style={{ display: 'grid', gridTemplateColumns: '50px 1fr', gap: '4px 8px', alignItems: 'center', fontSize: 10 }}>
                  {[['Breath', 'breath', usage.breath], ['Expr', 'expr', usage.expr], ['Press', 'pressure', usage.pressure], ['Slide', 'slide', usage.slide]].map(([l, k, v]) => (
                    <React.Fragment key={l}>
                      <span style={{ color: 'var(--vn-muted)' }}>{l}</span>
                      <div className={`vn-meter is-${k}`} style={{ height: 6 }}><div className="vn-meter-fill" style={{ width: `${v}%` }} /></div>
                    </React.Fragment>
                  ))}
                </div>
                <div style={{ display: 'flex', flexDirection: 'column', gap: 6, justifyContent: 'center' }}>
                  <button className="vn-btn" style={{ background: 'var(--vn-breath)', color: '#fff', borderColor: 'var(--vn-breath)' }}>Load</button>
                  <button className="vn-icon-btn">★</button>
                </div>
              </div>
            );
          })}
        </div>
      </div>
      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ each row shows breath / expr / press / slide usage — pick by feel</div>
    </div>
  );
}

// =====================================================================
// C · GROUPED BY CONTROLLER — headless-prep oriented
// =====================================================================
function PresetC(){
  const groups = [
    { ctrl: 'Sylphyo', icon: '🌬', count: 4, color: 'var(--vn-breath)', items: PRESETS.filter(p => p.ctrl === 'sylphyo') },
    { ctrl: 'Exquis',  icon: '⬢',  count: 2, color: 'var(--vn-pressure)', items: PRESETS.filter(p => p.ctrl === 'exquis') },
    { ctrl: 'EWI',     icon: '|',  count: 1, color: 'var(--vn-expr)', items: PRESETS.filter(p => p.ctrl === 'ewi') },
    { ctrl: 'Morph',   icon: '▢',  count: 1, color: 'var(--vn-slide)', items: PRESETS.filter(p => p.ctrl === 'morph') },
  ];
  return (
    <div className="vn vn-board">
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'flex-start' }}>
        <div>
          <div className="vn-eyebrow">Sweet spots · headless prep</div>
          <div className="vn-h1" style={{ marginTop: 2 }}>By controller</div>
        </div>
        <div style={{ display: 'flex', gap: 6 }}>
          <span className="vn-chip">Sort: recent</span>
          <button className="vn-btn">Export bank →</button>
        </div>
      </div>

      <div style={{ flex: 1, overflow: 'auto', minHeight: 0, display: 'flex', flexDirection: 'column', gap: 12 }}>
        {groups.map(g => (
          <div key={g.ctrl} className="vn-panel" style={{ padding: 12 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10, marginBottom: 10 }}>
              <div style={{ width: 28, height: 28, borderRadius: 8, background: g.color, color: '#fff', display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 14 }}>{g.icon}</div>
              <div style={{ flex: 1 }}>
                <div className="vn-h2">{g.ctrl}</div>
                <div style={{ fontSize: 11, color: 'var(--vn-muted)' }}>{g.items.length} sweet spots · default channel 1</div>
              </div>
              <span className="vn-chip is-ghost">+ slot</span>
            </div>
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(220px, 1fr))', gap: 6 }}>
              {g.items.map((p, i) => (
                <div key={i} className="vn-row" style={{ gridTemplateColumns: 'auto 1fr auto', padding: '8px 10px' }}>
                  <span className="vn-num" style={{ fontSize: 11, color: 'var(--vn-muted)', width: 18 }}>{(i + 1).toString().padStart(2, '0')}</span>
                  <div>
                    <div style={{ fontSize: 12, fontWeight: 600 }}>{p.name}</div>
                    <div style={{ fontSize: 10, color: 'var(--vn-muted)' }}>{p.scale}</div>
                  </div>
                  <TinyBreath w={40} h={16} seed={i * 1.7} color={g.color} />
                </div>
              ))}
            </div>
          </div>
        ))}
      </div>
      <div className="vn-anno" style={{ position: 'absolute', right: 22, bottom: 4 }}>↑ pick a controller first → its sweet spots → ship as a bank</div>
    </div>
  );
}

Object.assign(window, { PresetA, PresetB, PresetC });
