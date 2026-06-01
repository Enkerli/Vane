/* ═══════════════════════════════════════════════════════════════════════
   Vane — AREA 4 · MTS-ESP / tuning surface
   Today tuning is a single header chip (MTS-ESP on/off). This layer gives it
   a real home that expresses the three things the brief asks for:
     · NAME + system of the active tuning
     · INTERNAL tunings (a browsable set, used when no master is present)
     · HOLES (MIDI keys a scale leaves unmapped) shown explicitly
     · PRECEDENCE — MTS-ESP master  >  internal  >  off(12-EDO) — made visible,
       with the losing layer shown but locked, never faked.

   Two presentations, switched by VANE_X.tuning:
     'tab'   — a 4th Inspector tab (peer of Patch / Matrix / Presets)
     'stage' — a 4th Stage view (peer of Sources / Spectrum / Pitch)

   The tuning map is one reusable SVG primitive (an octave on a cents ruler):
   12-ET reference ticks behind, tuned degrees as nodes (horizontal offset =
   cents deviation), holes struck through, sounding notes lit. Built so it can
   be lifted verbatim; the engine would feed `tuningTable` (per-key cents) and
   `tuningStatus{connected,name,system}` over the existing bridge.
   ═══════════════════════════════════════════════════════════════════════ */
(function(){
  const PC = ["C","C♯","D","E♭","E","F","F♯","G","A♭","A","B♭","B"];

  // ── Internal tuning library (cents from 1/1). keys:12 → a 12-key map that
  //    may have holes; edo:N → an N-equal division plotted directly. ──────────
  const TUN = {
    edo12:   { name:"12-tone Equal",      sys:"12-EDO · 100¢ step",       keys:12, cents:[0,100,200,300,400,500,600,700,800,900,1000,1100], holes:[] },
    just:    { name:"Just Intonation",    sys:"5-limit · C major",        keys:12, cents:[0,111.7,203.9,315.6,386.3,498.0,590.2,702.0,813.7,884.4,996.1,1088.3], holes:[] },
    pyth:    { name:"Pythagorean",        sys:"3-limit · chain of 5ths",  keys:12, cents:[0,113.7,203.9,294.1,407.8,498.0,611.7,702.0,815.6,905.9,996.1,1109.8], holes:[] },
    meanqc:  { name:"¼-comma Meantone",   sys:"fifth 696.6¢",             keys:12, cents:[0,76.0,193.2,310.3,386.3,503.4,579.5,696.6,772.6,889.7,1006.8,1082.9], holes:[] },
    werck3:  { name:"Werckmeister III",   sys:"1691 well-temperament",    keys:12, cents:[0,90.2,192.2,294.1,390.2,498.0,588.3,696.1,792.2,888.3,996.1,1092.2], holes:[] },
    diat7:   { name:"Just Diatonic",      sys:"7 notes · 5 holes",        keys:12, cents:[0,null,203.9,null,386.3,498.0,null,702.0,null,884.4,null,1088.3], holes:[1,3,6,8,10] },
    edo19:   { name:"19-tone Equal",      sys:"19-EDO · 63.2¢ step",      edo:19,  period:1200 },
    bp:      { name:"Bohlen–Pierce",      sys:"13 ÷ tritave (non-octave)",edo:13,  period:1901.955 },
  };
  const ORDER = ["edo12","just","pyth","meanqc","werck3","diat7","edo19","bp"];

  // ── Tuning state (precedence + what the master is providing). The engine
  //    would push the real master name/table; here we simulate a connected
  //    master serving Werckmeister III so precedence is demonstrable. ─────────
  state.tuning = { source: state.mts ? 'mts' : 'internal', internal: 'just',
    master: { id:'werck3' }, ref: 'A4 = 440' };
  function effective(){
    const t=state.tuning;
    if(t.source==='mts' && state.mts) return { tun:TUN[t.master.id], prov:'mts',  locked:true  };
    if(t.source==='off')               return { tun:TUN.edo12,        prov:'off',  locked:false };
    return { tun:TUN[t.internal]||TUN.edo12, prov:'int', locked:false };
  }

  // ── Live sounding pitch-classes / degrees, from the voice feed ─────────────
  const C0=16.351597;
  function liveCentsSet(tun){
    const out=new Set();
    for(const v of (state.voices||[])){
      const hz=v.hz|| (v.note!=null? C0*Math.pow(2,v.note/12) : 0); if(hz<=0) continue;
      let c=1200*Math.log2(hz/C0); const per=tun.period||1200;
      c=((c%per)+per)%per;
      if(tun.edo){ out.add(Math.round(c/(per/tun.edo))%tun.edo); }
      else out.add(Math.round(c/100)%12);
    }
    return out;
  }

  // ── THE MAP PRIMITIVE — a deviation profile ────────────────────────────────
  //   x: evenly-spaced keys/degrees · y: cents deviation from equal temperament
  //   (zero line = ET, amplified & labelled). A flat row reads as "equal"; the
  //   wiggle IS the temperament's character. Holes are struck slots; sounding
  //   notes light up with their exact deviation. EDO-N plots its N degrees and
  //   measures each against the nearest 12-ET semitone.
  function tuningMapSvg(tun, live, big){
    const W=big?560:330, H=big?168:138, padX=18, top=26, zero=Math.round(H*0.52), span=zero-top+ (H-26-zero);
    // build degree list → {label, dev, hole, idx}
    let degs=[];
    if(tun.edo){
      for(let k=0;k<tun.edo;k++){ const c=k*(tun.period||1200)/tun.edo;
        const nearest=Math.round(c/100); degs.push({label:String(k), dev:c-nearest*100, hole:false, idx:k}); }
    } else {
      for(let k=0;k<12;k++){ const c=tun.cents[k];
        degs.push(c==null ? {label:PC[k],dev:0,hole:true,idx:k} : {label:PC[k],dev:c-k*100,hole:false,idx:k}); }
    }
    const maxDev=Math.max(15, ...degs.filter(d=>!d.hole).map(d=>Math.abs(d.dev)));
    const half=Math.min(zero-top, H-24-zero);                  // px available each side of zero
    const Y=dev=>zero - (dev/maxDev)*half;
    const n=degs.length, X=i=> padX + (n<=1?0: i/(n-1)*(W-2*padX));
    let g='';
    // zero (ET) line + ± guides
    g+=`<line class="lane" x1="${padX}" y1="${zero}" x2="${W-padX}" y2="${zero}"/>`;
    g+=`<line class="et" x1="${padX}" y1="${Y(maxDev).toFixed(1)}" x2="${W-padX}" y2="${Y(maxDev).toFixed(1)}"/>`;
    g+=`<line class="et" x1="${padX}" y1="${Y(-maxDev).toFixed(1)}" x2="${W-padX}" y2="${Y(-maxDev).toFixed(1)}"/>`;
    g+=`<text class="cents" x="${padX}" y="${(Y(maxDev)-3).toFixed(1)}">+${Math.round(maxDev)}¢</text>`;
    g+=`<text class="cents" x="${padX}" y="${(Y(-maxDev)+9).toFixed(1)}">−${Math.round(maxDev)}¢</text>`;
    g+=`<text class="cents" x="${(W-padX).toFixed(1)}" y="${(zero-3).toFixed(1)}" text-anchor="end">equal</text>`;
    // connect the non-hole nodes into a profile line
    const poly=degs.map((d,i)=> d.hole?null:`${X(i).toFixed(1)},${Y(d.dev).toFixed(1)}`).filter(Boolean);
    g+=`<polyline class="dev" points="${poly.join(' ')}" fill="none" stroke="var(--vn-slide)" stroke-width="1.4" opacity="0.4"/>`;
    // nodes
    degs.forEach((d,i)=>{ const x=X(i);
      if(d.hole){
        g+=`<line class="holex" x1="${(x-4).toFixed(1)}" y1="${(zero-4).toFixed(1)}" x2="${(x+4).toFixed(1)}" y2="${(zero+4).toFixed(1)}"/>`
         + `<line class="holex" x1="${(x-4).toFixed(1)}" y1="${(zero+4).toFixed(1)}" x2="${(x+4).toFixed(1)}" y2="${(zero-4).toFixed(1)}"/>`
         + `<text class="pc" x="${x.toFixed(1)}" y="${(H-6).toFixed(1)}" text-anchor="middle" fill="var(--vn-pressure)">${d.label}</text>`;
        return;
      }
      const lit=live.has(d.idx), y=Y(d.dev);
      // stem from zero line to node
      g+=`<line x1="${x.toFixed(1)}" y1="${zero}" x2="${x.toFixed(1)}" y2="${y.toFixed(1)}" stroke="${lit?'var(--vn-bend)':'var(--vn-slide)'}" stroke-width="${lit?(big?3:2.4):1.6}" opacity="${lit?1:.7}" stroke-linecap="round"/>`;
      g+=`<circle class="node ${lit?'liven':''}" cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="${lit?(big?5.2:4.4):(big?3.6:3)}" fill="${lit?'var(--vn-bend)':'var(--vn-slide)'}"/>`;
      g+=`<text class="pc" x="${x.toFixed(1)}" y="${(H-6).toFixed(1)}" text-anchor="middle" fill="${lit?'var(--vn-bend)':'var(--vn-muted)'}">${d.label}</text>`;
      if(lit) g+=`<text class="livelbl" x="${x.toFixed(1)}" y="${(y + (d.dev>=0?-8:14)).toFixed(1)}" text-anchor="middle">${d.dev>=0?'+':''}${d.dev.toFixed(1)}</text>`;
    });
    return `<svg class="tm-svg" viewBox="0 0 ${W} ${H}" style="height:${H}px" preserveAspectRatio="xMidYMid meet">${g}</svg>`;
  }
  // expose for engine handoff / reuse
  window.VaneTuning = { TUN, effective, tuningMapSvg, liveCentsSet };

  // ── Precedence card ────────────────────────────────────────────────────────
  function precedenceHTML(){
    const t=state.tuning, eff=effective();
    const seg=(id,lbl,on,dis)=>`<button class="pbtn ${on?'on':''}" data-prec="${id}" ${dis?'disabled':''}>${lbl}</button>`;
    const provPill = eff.prov==='mts'
      ? `<span class="src-pill mts">● MTS-ESP master</span>`
      : eff.prov==='off' ? `<span class="src-pill off">○ Bypassed</span>`
      : `<span class="src-pill int">◆ Internal</span>`;
    const sub = eff.prov==='mts'
      ? `A host master is providing the scale. Vane <b>follows</b> it — internal selection is held in reserve.`
      : eff.prov==='off' ? `Tuning bypassed — standard <b>12-EDO</b>. Notes play at concert pitch.`
      : state.mts ? `A master is present but Vane is set to <b>use its own</b> internal tuning (override).`
                  : `No MTS-ESP master detected. Vane uses its <b>internal</b> tuning.`;
    return `<div class="tune-prec">
      <div class="pr-top">
        ${provPill}
        <span class="tname">${eff.tun.name}</span>
        <span class="tsys">${eff.tun.sys}</span>
        ${eff.prov==='mts'&&!state.mts?`<button class="btn retry" data-retry>⚠ retry</button>`:''}
      </div>
      <div class="pr-sub">${sub}</div>
      <div class="seg" style="margin-top:10px">
        ${seg('mts','Follow master',t.source==='mts',!state.mts)}
        ${seg('internal','Use internal',t.source==='internal',false)}
        ${seg('off','Bypass',t.source==='off',false)}
      </div>
    </div>`;
  }

  // ── Internal tuning list ───────────────────────────────────────────────────
  function listHTML(){
    const t=state.tuning, eff=effective(), pickable=(t.source==='internal');
    return `<div class="tune-sec">
      <span class="eyebrow">Internal tunings</span>
      <div class="tune-list">${ORDER.map(id=>{
        const tn=TUN[id], sel=(t.internal===id), nHoles=(tn.holes||[]).length;
        const isActiveInternal = sel && eff.prov==='int';
        return `<div class="tune-row ${sel?'sel':''} ${pickable?'':'locked'}" data-tun="${id}">
          <span class="tr-mark">${isActiveInternal?'◆':sel?'·':''}</span>
          <div><div class="tr-name">${tn.name}</div><div class="tr-sys">${tn.sys}</div></div>
          ${nHoles?`<span class="tr-holes">${nHoles} holes</span>`:`<span class="tr-holes" style="color:var(--vn-muted)">${tn.edo?tn.edo+' tones':'full'}</span>`}
        </div>`;}).join('')}</div>
      ${pickable?'':'<p class="fine-hint" style="margin-top:8px">Switch to <b>Use internal</b> to choose. The active tuning above wins.</p>'}
    </div>`;
  }

  function mapBlockHTML(big){
    const eff=effective(), live=liveCentsSet(eff.tun);
    return `<div class="tune-map">
      <div class="tm-head"><span class="eyebrow">${eff.tun.name} · octave map</span><span class="ref">${state.tuning.ref}</span></div>
      ${tuningMapSvg(eff.tun, live, big)}
      <div class="tm-legend">
        <span><i style="background:var(--vn-slide)"></i>tuned degree</span>
        <span><i style="background:var(--vn-bend)"></i>sounding</span>
        <span><i style="background:var(--vn-pressure)"></i>hole (unmapped key)</span>
        <span style="color:var(--vn-muted-2)"><i style="background:var(--vn-muted-2)"></i>12-ET reference</span>
      </div></div>`;
  }

  // ── Inspector-tab presentation (approach A) ────────────────────────────────
  window.renderTuning = function(){
    $('#tabbody').innerHTML = `<div class="tune">
      ${precedenceHTML()}
      ${mapBlockHTML(false)}
      ${listHTML()}
    </div>`;
    wireTuning($('#tabbody'));
  };
  function wireTuning(root){
    root.querySelectorAll('[data-prec]').forEach(b=> b.onclick=()=>{ if(b.disabled)return;
      state.tuning.source=b.dataset.prec; redrawTuning(); });
    root.querySelectorAll('.tune-row:not(.locked)').forEach(r=> r.onclick=()=>{
      state.tuning.internal=r.dataset.tun; redrawTuning(); });
    const rt=root.querySelector('[data-retry]'); if(rt) rt.onclick=()=>toggleMts();
  }
  function redrawTuning(){
    if(window.VANE_X.tuning==='tab' && tab==='tuning') renderTuning();
    if(window.VANE_X.tuning==='stage' && view==='tuning') paintStageTuning();
  }

  // ── Stage-view presentation (approach B) ───────────────────────────────────
  function ensureStageMap(){
    let el=document.getElementById('tuneStage'); if(el) return el;
    const vis=document.querySelector('.stage .vis'); if(!vis) return null;
    el=document.createElement('div'); el.id='tuneStage';
    vis.appendChild(el); return el;
  }
  function paintStageTuning(){
    const el=ensureStageMap(); if(!el) return;
    const eff=effective(), t=state.tuning;
    el.innerHTML=`<div class="tune-stagebar">
        ${precSegInline()}
        <span style="flex:1"></span>
        <select data-stagepick ${t.source==='internal'?'':'disabled'}>
          ${ORDER.map(id=>`<option value="${id}" ${t.internal===id?'selected':''}>${TUN[id].name}</option>`).join('')}
        </select>
      </div>
      <div class="tune-stage-map">${mapBlockHTML(true)}</div>`;
    el.querySelectorAll('[data-prec]').forEach(b=> b.onclick=()=>{ if(b.disabled)return;
      state.tuning.source=b.dataset.prec; paintStageTuning(); });
    const sel=el.querySelector('[data-stagepick]'); if(sel) sel.onchange=()=>{ state.tuning.internal=sel.value; paintStageTuning(); };
  }
  function precSegInline(){
    const t=state.tuning;
    const seg=(id,lbl,on,dis)=>`<button class="pbtn ${on?'on':''}" data-prec="${id}" ${dis?'disabled':''}>${lbl}</button>`;
    return `<div class="seg">${seg('mts','Master',t.source==='mts',!state.mts)}${seg('internal','Internal',t.source==='internal',false)}${seg('off','Bypass',t.source==='off',false)}</div>`;
  }

  // ── Wire the chosen presentation into the app shell ────────────────────────
  function installTab(){
    const tabs=document.querySelector('.inspector .tabs'); if(!tabs||tabs.querySelector('[data-tab="tuning"]')) return;
    const b=document.createElement('button'); b.dataset.tab='tuning'; b.textContent='Tuning';
    b.onclick=()=>setTab('tuning'); tabs.appendChild(b);
    // extend the render dispatch
    const _render=window.render;
    window.render=function(){ if(tab==='tuning'){ renderTuning(); return; } _render(); };
  }
  function installStageView(){
    const seg=document.getElementById('viewseg'); if(!seg||seg.querySelector('[data-view="tuning"]')) return;
    const b=document.createElement('button'); b.dataset.view='tuning'; b.textContent='Tuning';
    b.onclick=()=>setView('tuning'); seg.appendChild(b);
    const _setView=window.setView;
    window.setView=function(v){ _setView(v);
      ensureStageMap();
      const vis=document.querySelector('.stage .vis');
      if(vis) vis.classList.toggle('tuning-on', v==='tuning');
      if(v==='tuning') paintStageTuning();
    };
    // keep the live dots fresh while the stage map is open
    const _draw=window.drawVis;
    window.drawVis=function(){ if(view==='tuning'){ paintStageTuning(); return; } _draw(); };
  }

  // Header chip stays the at-a-glance status, but now also jumps to the surface.
  function upgradeChip(){
    const chip=$('#tuningChip'); if(!chip) return;
    chip.title='Tuning — open the tuning surface';
    const _toggle=chip.onclick;
    chip.onclick=(e)=>{
      if(window.VANE_X.tuning==='tab'){ setTab('tuning'); }
      else { setView('tuning'); }
    };
  }
  // Reflect the real connection state into our precedence model.
  const _setMts=window.setMtsLabel;
  window.setMtsLabel=function(connected){ _setMts(connected);
    if(!connected && state.tuning.source==='mts'){ /* master lost: surface stays, prov shows retry */ }
    redrawTuning();
  };

  window.__installTuning=function(){
    if(window.VANE_X.tuning==='stage') installStageView(); else installTab();
    upgradeChip();
  };
})();
