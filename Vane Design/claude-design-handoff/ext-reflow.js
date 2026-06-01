/* ═══════════════════════════════════════════════════════════════════════
   Vane — AREA 6 · iPad / touch / AUv3 reflow  (+ cross-cutting touch fixes)
   The hard constraint: ONE UI on desktop mouse AND a narrow/short iPadOS AUv3
   window — no hover, no modifier keys, no mouse-only affordances.

   Two reflow approaches, switched by VANE_X.reflow (only differ ≤860px):
     'stack' — refined single scroll: Stage pins to the top while params scroll
               under it; bottom tab bar jumps between Patch/Matrix/Presets.
               (evolves what ships)
     'focus' — one pane at a time: a top segmented switch swaps the whole
               surface (Stage · Patch · Matrix · Presets), no long scroll.

   Cross-cutting, applied in BOTH (the two things the brief flagged):
     · text selection is killed globally (ext.css) so a performance drag never
       selects a label;
     · typed entry works on a single TAP — the app opens number editors on
       dblclick, which is unreliable on touch. We bridge tap→edit for coarse
       pointers only, leaving desktop double-click untouched.
   ═══════════════════════════════════════════════════════════════════════ */
(function(){

  // ── Cross-cutting: single-tap typed entry on touch ────────────────────────
  // The editable value fields carry an inline ondblclick (slotType / arEdit /
  // ccEdit). Drag controls (.bipolar/.track) set their dblclick in JS, so the
  // [ondblclick] attribute selector below never catches a slider — a tap that
  // begins a drag can't accidentally open an editor.
  const coarse = matchMedia('(pointer:coarse)').matches || ('ontouchstart' in window);
  if(coarse){
    document.addEventListener('click', e=>{
      const el=e.target.closest('[ondblclick]'); if(!el) return;
      if(el.querySelector('input')) return;          // already editing
      e.preventDefault(); e.stopPropagation();
      if(typeof el.ondblclick==='function') el.ondblclick.call(el, e);
    }, true);
    document.documentElement.classList.add('is-touch');
  }

  // ── Approach B scaffold: the focus switcher ───────────────────────────────
  function buildFocusbar(){
    const app=document.getElementById('app'); if(!app) return;
    if(document.getElementById('focusbar')) return;
    const header=app.querySelector('.header');
    const bar=document.createElement('div'); bar.className='focusbar'; bar.id='focusbar';
    const panes=[['stage','Stage'],['patch','Patch'],['matrix','Matrix'],['presets','Presets']];
    if(window.VANE_X.tuning==='tab') panes.push(['tuning','Tuning']);
    bar.innerHTML=panes.map(([id,lbl],k)=>`<button data-focus="${id}" class="${k===0?'active':''}">${lbl}</button>`).join('');
    header.insertAdjacentElement('afterend', bar);
    bar.querySelectorAll('button').forEach(b=> b.onclick=()=>focusPane(b.dataset.focus));
  }
  function focusPane(id){
    const main=document.querySelector('.main'); if(!main) return;
    document.querySelectorAll('#focusbar button').forEach(b=>b.classList.toggle('active',b.dataset.focus===id));
    if(id==='stage'){ main.dataset.pane='stage'; }
    else { main.dataset.pane='inspector'; if(typeof setTab==='function') setTab(id); }
  }

  // ── Approach A scaffold: a pin/expand toggle for the sticky stage ─────────
  function buildStagePin(){
    const head=document.querySelector('.stage .stage-head'); if(!head) return;
    if(head.querySelector('.stage-pin')) return;
    const b=document.createElement('button'); b.className='viewseg stage-pin';
    b.style.cssText='padding:6px 10px;cursor:pointer';
    b.innerHTML='⤢'; b.title='Expand / collapse the stage';
    b.onclick=()=>{ document.querySelector('.stage').classList.toggle('expanded'); };
    head.appendChild(b);
  }

  // ── Host-defer toggle (AUv3 inside a host with its own preset chrome) ──────
  // Driven by VANE_X.host==='auv3'; collapses the in-plugin preset nav so it
  // doesn't duplicate the host's transport/preset bar (data-host hook in CSS).

  window.__installReflow=function(){
    const app=document.getElementById('app'); if(!app) return;
    app.dataset.reflow = window.VANE_X.reflow||'stack';
    if(window.VANE_X.host) app.dataset.host = window.VANE_X.host;
    if(app.dataset.reflow==='focus'){ buildFocusbar(); document.querySelector('.main').dataset.pane='stage';
      document.body.classList.add('focus-active'); }
    else { buildStagePin(); }
  };
})();
