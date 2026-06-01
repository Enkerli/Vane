/* ═══════════════════════════════════════════════════════════════════════
   Vane — AREA 1 · Bézier mod-curves
   Replaces the discrete {Lin·Exp·S} curve choice on each route with an
   editable response curve. Model = a monotone cubic spline through fixed
   endpoints (0,0)-(1,1) and 1..3 draggable interior anchors:
     · default: ONE anchor at the midpoint → drag to bend (exp / log)
     · "+ point": add a 2nd/3rd anchor → S-curves and custom shapes
   Monotone (Fritsch–Carlson) so a response curve never overshoots/reverses.
   Bipolar sources (Pitchbend, ±1) edit the positive half; the negative half
   mirrors with odd symmetry (f(-x) = -f(x)) so the curve passes through 0.

   Two presentations, switched by VANE_X.curve:
     'A' — inline editor that expands inside the route row
     'B' — one dedicated editor docked above the list, edits the selected route

   Drop-in: this monkey-patches slotRow / wireSlot / renderMatrix. The new
   per-slot field is `anchors:[{x,y},…]`; persisted via slotEdit alongside the
   legacy `curve` index (kept for migration). Backend adds a curve-table param.
   ═══════════════════════════════════════════════════════════════════════ */
(function(){
  window.VANE_X = window.VANE_X || { curve:'A', tuning:'tab', reflow:'stack', host:'' };

  // ── Curve math ──────────────────────────────────────────────────────────
  function curvePts(anchors){
    const a=(anchors&&anchors.length?anchors:[{x:.5,y:.5}]).map(p=>({
      x:clamp(p.x,0.012,0.988), y:clamp(p.y,0,1) })).sort((p,q)=>p.x-q.x);
    return [{x:0,y:0},...a,{x:1,y:1}];
  }
  function tangents(pts){
    const n=pts.length, d=[], m=[];
    for(let i=0;i<n-1;i++) d[i]=(pts[i+1].y-pts[i].y)/(pts[i+1].x-pts[i].x);
    m[0]=d[0]; m[n-1]=d[n-2];
    for(let i=1;i<n-1;i++) m[i]=(d[i-1]*d[i]<=0)?0:(d[i-1]+d[i])/2;
    for(let i=0;i<n-1;i++){
      if(d[i]===0){ m[i]=0; m[i+1]=0; continue; }
      const a=m[i]/d[i], b=m[i+1]/d[i], h=Math.hypot(a,b);
      if(h>3){ const t=3/h; m[i]=t*a*d[i]; m[i+1]=t*b*d[i]; }
    }
    return m;
  }
  // unipolar response: x∈[0,1] → y∈[0,1]
  function evalUni(anchors,x){
    const pts=curvePts(anchors), m=tangents(pts), n=pts.length; x=clamp(x,0,1);
    let i=0; while(i<n-2 && x>pts[i+1].x) i++;
    const x0=pts[i].x,x1=pts[i+1].x,y0=pts[i].y,y1=pts[i+1].y,h=x1-x0;
    if(h<=0) return y0;
    const t=(x-x0)/h, t2=t*t, t3=t2*t;
    return clamp((2*t3-3*t2+1)*y0+(t3-2*t2+t)*h*m[i]+(-2*t3+3*t2)*y1+(t3-t2)*h*m[i+1],0,1);
  }
  // bipolar: odd symmetry around the origin
  function evalCurve(anchors,u,bipolar){
    if(!bipolar) return evalUni(anchors,u);
    const s=u<0?-1:1; return s*evalUni(anchors,Math.abs(clamp(u,-1,1)));
  }
  window.VaneCurve = { evalUni, evalCurve };   // exposed for the engine handoff

  // ── Per-slot model: lazy-migrate legacy curve index → anchors ────────────
  function anchorsFor(s){
    if(Array.isArray(s.anchors) && s.anchors.length) return s.anchors;
    // 0 Lin · 1 Exp · 2 S  →  starting anchor sets
    s.anchors = s.curve===1 ? [{x:.5,y:.27}]
              : s.curve===2 ? [{x:.32,y:.16},{x:.68,y:.84}]
              :               [{x:.5,y:.5}];
    return s.anchors;
  }
  const isBipolar = srcName => srcName==='Pitchbend';

  // ── SVG path for a curve (viewBox W×H, pad) ──────────────────────────────
  function pathFor(anchors, W, H, pad, bipolar){
    const N=48; let d='';
    const X = u => bipolar ? pad+(u+1)/2*(W-2*pad) : pad+u*(W-2*pad);
    const Y = y => bipolar ? (H/2) - y*(H/2-pad) : (H-pad) - y*(H-2*pad);
    const lo = bipolar?-1:0;
    for(let k=0;k<=N;k++){ const u=lo+(k/N)*(1-lo); const y=evalCurve(anchors,u,bipolar);
      d+=(k?'L':'M')+X(u).toFixed(1)+','+Y(y).toFixed(1); }
    return d;
  }

  // ── Thumbnail (replaces the miniseg) ─────────────────────────────────────
  function thumbSvg(s, srcName){
    const W=46,H=22,pad=3, bip=isBipolar(srcName);
    const col=cssv(SRC_COLOR[srcName]||'--vn-muted');
    const base = bip
      ? `<line class="tline" x1="${pad}" y1="${H/2}" x2="${W-pad}" y2="${H/2}"/>`
      : `<line class="tline" x1="${pad}" y1="${H-pad}" x2="${W-pad}" y2="${pad}"/>`;
    return `<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">${base}`
      + `<path d="${pathFor(anchorsFor(s),W,H,pad,bip)}" fill="none" stroke="${col}" stroke-width="1.6" stroke-linecap="round"/></svg>`;
  }
  function paintThumb(i){
    const t=document.querySelector(`.curve-thumb[data-slot="${i}"]`); if(!t)return;
    const s=state.slots[i]; t.innerHTML=thumbSvg(s,SOURCES[s.src]);
    t.style.color=cssv(SRC_COLOR[SOURCES[s.src]]||'--vn-muted');
  }

  // ── The interactive editor (shared by inline + dock) ─────────────────────
  function editorHTML(i, big){
    const s=state.slots[i], srcName=SOURCES[s.src], bip=isBipolar(srcName);
    const W=big?360:284, H=big?188:140;
    const col=SRC_COLOR[srcName]||'--vn-muted';
    const nA=anchorsFor(s).length;
    return `<div class="curve-ed" data-cv="${i}" style="color:var(${col})">
      <div class="ce-head">
        <span class="lbl">Response curve</span>
        ${bip?'<span class="biCh">bipolar ±1</span>':''}
        <span class="sp"></span>
        <button class="pbtn" data-add ${nA>=3?'disabled':''}>+ point</button>
      </div>
      <svg class="curve-svg" data-slot="${i}" data-bip="${bip?1:0}" viewBox="0 0 ${W} ${H}"
           style="height:${H}px" preserveAspectRatio="none"></svg>
      <div class="ce-foot">
        <span class="hint">Drag a point to shape · drag the curve's midpoint to bend.${bip?' Negative input mirrors.':''}</span>
        <button class="rm" data-rm hidden>Remove point</button>
      </div></div>`;
  }
  const sel = {};   // per-slot selected anchor index
  function drawEditor(svg, i){
    const s=state.slots[i], bip=svg.dataset.bip==='1', anchors=anchorsFor(s);
    const vb=svg.viewBox.baseVal, W=vb.width, H=vb.height, pad=16;
    const X = u => bip ? pad+(u+1)/2*(W-2*pad) : pad+u*(W-2*pad);
    const Y = y => bip ? (H/2) - y*(H/2-pad)   : (H-pad) - y*(H-2*pad);
    let g='';
    // grid
    for(let k=1;k<4;k++){ const gx=pad+k/4*(W-2*pad); g+=`<line class="grid" x1="${gx}" y1="${pad}" x2="${gx}" y2="${H-pad}"/>`; }
    for(let k=1;k<4;k++){ const gy=pad+k/4*(H-2*pad); g+=`<line class="grid" x1="${pad}" y1="${gy}" x2="${W-pad}" y2="${gy}"/>`; }
    if(bip){ g+=`<line class="axis" x1="${pad}" y1="${H/2}" x2="${W-pad}" y2="${H/2}"/>`
              +`<line class="axis" x1="${W/2}" y1="${pad}" x2="${W/2}" y2="${H-pad}"/>`; }
    // the curve (+ faint fill to the baseline)
    const dPath=pathFor(anchors,W,H,pad,bip);
    const baseY=bip?H/2:H-pad;
    g+=`<path class="crv crvfill" d="${dPath} L ${X(1)},${baseY} L ${X(bip?-1:0)},${baseY} Z" fill="currentColor" stroke="none"/>`;
    g+=`<path class="crv" d="${dPath}" stroke="currentColor"/>`;
    // anchors (draggable). In bipolar we only show/edit the positive-half ones.
    const si = sel[i]==null ? anchors.length-1 : sel[i];
    anchors.forEach((a,ai)=>{
      const cx=X(bip?Math.abs(a.x):a.x), cy=Y(a.y);
      g+=`<circle class="anchor ${ai===si?'sel':''}" cx="${cx.toFixed(1)}" cy="${cy.toFixed(1)}" r="6.5" stroke="currentColor"/>`
       + `<circle class="hit" data-ai="${ai}" cx="${cx.toFixed(1)}" cy="${cy.toFixed(1)}" r="17"/>`;
    });
    // live-value dot (positioned each tick)
    g+=`<circle class="live" data-live cx="-99" cy="-99" r="4.5"/>`
     + `<text class="livev" data-livev x="0" y="0" text-anchor="middle"></text>`;
    svg.innerHTML=g;
    // show "remove" only when >1 anchor and one is selected
    const ed=svg.closest('.curve-ed'); if(ed){ const rm=ed.querySelector('[data-rm]');
      if(rm) rm.hidden = anchors.length<=1; }
  }
  function liveInput(srcName){
    const m = state.meters[srcName]; if(m==null) return null;
    return isBipolar(srcName) ? clamp(m*2-1,-1,1) : clamp(m,0,1);
  }
  function updateLive(svg){
    const i=+svg.dataset.slot, bip=svg.dataset.bip==='1';
    const s=state.slots[i]; if(!s) return;
    const u=liveInput(SOURCES[s.src]); const dot=svg.querySelector('[data-live]'), lbl=svg.querySelector('[data-livev]');
    if(!dot) return;
    if(u==null){ dot.setAttribute('cx',-99); if(lbl)lbl.textContent=''; return; }
    const vb=svg.viewBox.baseVal, W=vb.width, H=vb.height, pad=16;
    const y=evalCurve(anchorsFor(s),u,bip);
    const px = bip ? pad+(u+1)/2*(W-2*pad) : pad+u*(W-2*pad);
    const py = bip ? (H/2)-y*(H/2-pad)     : (H-pad)-y*(H-2*pad);
    dot.setAttribute('cx',px.toFixed(1)); dot.setAttribute('cy',py.toFixed(1));
    if(lbl){ lbl.textContent=(s.amt*y).toFixed(2).replace('0.','.'); lbl.setAttribute('x',px.toFixed(1)); lbl.setAttribute('y',(py-9).toFixed(1)); }
  }

  function wireEditor(ed, i){
    const svg=ed.querySelector('.curve-svg'); if(!svg) return;
    drawEditor(svg,i);
    const bip=svg.dataset.bip==='1';
    const ptr = e=>{ const r=svg.getBoundingClientRect(), vb=svg.viewBox.baseVal, pad=16;
      const vx=(e.clientX-r.left)/r.width*vb.width, vy=(e.clientY-r.top)/r.height*vb.height;
      let x=(vx-pad)/(vb.width-2*pad), y;
      if(bip){ x=x*2-1; x=Math.abs(x); y=((vb.height/2)-vy)/(vb.height/2-pad); }
      else   { y=((vb.height-pad)-vy)/(vb.height-2*pad); }
      return { x:clamp(x,0.012,0.988), y:clamp(y,0,1) }; };
    let drag=-1;
    svg.addEventListener('pointerdown', e=>{
      const hit=e.target.closest('.hit'); if(!hit) return;
      drag=+hit.dataset.ai; sel[i]=drag; svg.setPointerCapture(e.pointerId);
      drawEditor(svg,i); e.preventDefault();
    });
    svg.addEventListener('pointermove', e=>{ if(drag<0) return;
      const a=anchorsFor(state.slots[i])[drag]; if(!a) return;
      const p=ptr(e); const arr=anchorsFor(state.slots[i]);
      const loX=drag>0?arr[drag-1].x+0.03:0.02, hiX=drag<arr.length-1?arr[drag+1].x-0.03:0.98;
      a.x=clamp(p.x,loX,hiX); a.y=p.y;
      drawEditor(svg,i); commitCurve(i); paintThumb(i);
    });
    const up=e=>{ if(drag>=0){ drag=-1; } };
    svg.addEventListener('pointerup',up); svg.addEventListener('pointercancel',up);
    const addB=ed.querySelector('[data-add]');
    if(addB) addB.onclick=()=>{ const arr=anchorsFor(state.slots[i]); if(arr.length>=3) return;
      // insert in the widest x-gap, sitting on the current curve
      const pts=[{x:0},...arr,{x:1}]; let gi=0,gw=-1;
      for(let k=0;k<pts.length-1;k++){ const w=pts[k+1].x-pts[k].x; if(w>gw){gw=w;gi=k;} }
      const nx=(pts[gi].x+pts[gi+1].x)/2; arr.splice(gi,0,{x:nx,y:evalUni(arr,nx)});
      arr.sort((p,q)=>p.x-q.x); sel[i]=arr.findIndex(p=>p.x===nx);
      addB.disabled=arr.length>=3; drawEditor(svg,i); commitCurve(i); paintThumb(i); };
    const rmB=ed.querySelector('[data-rm]');
    if(rmB) rmB.onclick=()=>{ const arr=anchorsFor(state.slots[i]); if(arr.length<=1) return;
      const si=sel[i]==null?arr.length-1:sel[i]; arr.splice(si,1); sel[i]=Math.max(0,si-1);
      const ab=ed.querySelector('[data-add]'); if(ab)ab.disabled=arr.length>=3;
      drawEditor(svg,i); commitCurve(i); paintThumb(i); };
  }
  function commitCurve(i){ const s=state.slots[i];
    // keep legacy index roughly in sync for migration / fallback
    const a=s.anchors; s.curve = (a.length>1)?2 : (a[0].y<0.45?1:0);
    if(typeof pushSlot==='function') pushSlot(i); markDirty();
  }

  // ── slotRow override: swap the miniseg for a curve thumbnail ─────────────
  window.slotRow = function(s,i){
    const srcName=SOURCES[s.src], col=SRC_COLOR[srcName]||'--vn-muted';
    const isAux=s.src>=7, pos=s.amt>=0, mag=Math.min(1,Math.abs(s.amt));
    const ccTxt = isAux ? ('CC'+((state.aux[s.src-7]||{}).cc??'?'))
                : BINDABLE[srcName] ? ('CC'+state.cc[srcName]) : (SRC_CC[srcName]||'');
    const dedicated = (window.VANE_X.curve==='B');
    const isSel = dedicated && (window.curveSel===i);
    return `<div class="slot ${s.on?'':'off'} ${isSel?'curve-selected':''}" data-slot="${i}">
      <div class="srccell">
        <div class="srctop"><span class="dot ${SRC_DOT[srcName]||''}"></span>
          <select onchange="slotSet(${i},'src',+this.value)" aria-label="source">${SOURCES.map((n,k)=>`<option value="${k}" ${k===s.src?'selected':''}>${srcLabel(k)}</option>`).join('')}</select></div>
        <div style="display:flex;align-items:center;gap:8px">
          <span class="cc ${BINDABLE[srcName]?'bindable':''}" ${BINDABLE[srcName]?`ondblclick="ccEdit(${i},'${srcName}',this)" title="double-click to rebind CC#"`:isAux?'title="bind in Controller setup"':''}>${ccTxt}</span>
          <span class="rowmeter" style="flex:1"><span class="f" data-rowmeter="${srcName}" style="background:var(${col})"></span></span></div>
      </div>
      <span class="arrow">→</span>
      <div class="destcell">
        <select onchange="slotSet(${i},'dst',+this.value)" aria-label="destination" style="width:100%">${DESTS.map((n,k)=>`<option value="${k}" ${k===s.dst?'selected':''}>${n}</option>`).join('')}</select>
        <div class="hint">amount × curve(source)</div>
      </div>
      <div class="amt">
        <div class="head"><span>−1</span><span class="v num" ondblclick="slotType(${i},this)">${(pos?'+':'−')+mag.toFixed(2)}</span><span>+1</span></div>
        <div class="bipolar" role="slider" aria-label="amount" style="color:var(${col})">
          <div class="fill" data-bipfill style="background:var(${col})"></div><div class="center"></div><div class="thumb" data-bipthumb></div></div>
      </div>
      <button class="curve-thumb" data-slot="${i}" aria-label="response curve" title="${dedicated?'edit this curve':'edit response curve'}">${thumbSvg(s,srcName)}</button>
      <div class="ar">
        <span class="arf">↑<span class="arv" ondblclick="arEdit(${i},'atk',this)">${s.atk}</span>ms</span>
        <span class="arf">↓<span class="arv" ondblclick="arEdit(${i},'rel',this)">${s.rel}</span>ms</span></div>
      <button class="tog ${s.on?'on':''}" onclick="slotSet(${i},'on',${s.on?0:1})" aria-label="enable route" title="${s.on?'enabled':'disabled'}"></button>
      <button class="del" title="remove route" onclick="slotDelete(${i})">✕</button>
    </div>`;
  };

  // ── wireSlot override: keep the amount drag, add curve behaviour ─────────
  const _wireSlot = window.wireSlot;
  window.wireSlot = function(i){
    _wireSlot(i);
    paintThumb(i);
    const row=document.querySelector(`.slot[data-slot="${i}"]`); if(!row) return;
    const thumb=row.querySelector('.curve-thumb'); if(!thumb) return;
    if(window.VANE_X.curve==='B'){
      thumb.onclick=()=>{ window.curveSel=i; renderMatrix(); };
    } else {
      thumb.onclick=()=>{
        let ed=row.querySelector('.curve-ed');
        if(ed){ ed.remove(); thumb.classList.remove('open'); return; }
        thumb.classList.add('open');
        row.insertAdjacentHTML('beforeend', editorHTML(i,false));
        ed=row.querySelector('.curve-ed'); wireEditor(ed,i);
      };
    }
  };

  // ── renderMatrix override: add the dedicated dock for approach B ─────────
  window.renderMatrix = function(){
    const all=state.slots.map((s,i)=>({s,i})).filter(x=>x.s.src!==0);
    let shown=all;
    if(mmFilter==='active') shown=all.filter(x=>x.s.on);
    else if(mmFilter!=='all') shown=all.filter(x=>SOURCES[x.s.src]===mmFilter);
    const canAdd=state.slots.some(s=>s.src===0);
    const usedSrc=[...new Set(all.map(x=>SOURCES[x.s.src]))];
    const filterBtns=['all','active',...usedSrc].map(f=>
      `<button class="${mmFilter===f?'active':''}" onclick="setMmFilter('${f}')">${f==='all'?'All':f==='active'?'Active':f}</button>`).join('');
    const dedicated=(window.VANE_X.curve==='B');
    if(dedicated){
      if(window.curveSel==null || !state.slots[window.curveSel] || state.slots[window.curveSel].src===0)
        window.curveSel = all.length?all[0].i:null;
    }
    let dock='';
    if(dedicated){
      const i=window.curveSel;
      if(i!=null && state.slots[i] && state.slots[i].src!==0){
        const s=state.slots[i], srcName=SOURCES[s.src];
        dock=`<div class="curve-dock"><div class="dk-head">
            <div class="dk-route"><span class="dot ${SRC_DOT[srcName]||''}"></span>
              <span class="src">${srcLabel(s.src)}</span><span class="arr">→</span><span class="dst">${DESTS[s.dst]}</span></div>
            <span class="dk-amt">${(s.amt>=0?'+':'−')+Math.min(1,Math.abs(s.amt)).toFixed(2)} × curve</span>
          </div>${editorHTML(i,true)}</div>`;
      } else {
        dock=`<div class="curve-dock empty">Select a route below to shape its response curve.</div>`;
      }
    }
    let body;
    if(all.length===0){
      body=`<div class="empty-state"><div class="big">No routes yet</div><div>Add a route to map a source onto a destination.</div></div>
        <div class="empty-add" onclick="addSlot()">+ add a route — pick source, destination, amount</div>`;
    } else {
      body=`<div class="slots">${shown.map(({s,i})=>slotRow(s,i)).join('')||'<div class="empty-state">No routes match this filter.</div>'}
        ${canAdd?`<div class="empty-add" onclick="addSlot()">+ add a route</div>`:''}</div>`;
    }
    $('#tabbody').innerHTML=`
      <div class="mm-toolbar">
        <span class="eyebrow" style="flex:1">Routes · ${all.length}/${NUM_SLOTS}</span>
        <div class="mm-filters">${filterBtns}</div>
        ${canAdd?'<button class="btn primary" onclick="addSlot()">+ New route</button>':''}
      </div>
      ${dock}
      ${body}
      <p class="fine-hint">Drag the amount bar · drag a curve point to shape the response · the moving dot is the live value.</p>`;
    shown.forEach(({i})=>wireSlot(i));
    if(dedicated){ const ed=$('#tabbody .curve-dock .curve-ed'); if(ed) wireEditor(ed,window.curveSel); }
  };

  // ── Live dots: piggy-back on the existing 40ms tick ──────────────────────
  const _tick = window.tickMeters;
  window.tickMeters = function(){
    _tick();
    document.querySelectorAll('.curve-svg').forEach(updateLive);
  };
})();
