/* S3 owns authoring actions; the server owns canonical semantics and paired cooking. */
'use strict';
const $ = id => document.getElementById(id);
let state, selected, copied, showActive = false, busy = false;
const kinds = ['Sequence', 'Increment', 'If', 'Repeat', 'DoorOpen', 'Return'];
const labels = {Sequence:'Sequence',Increment:'Count interaction',If:'If count reaches threshold',Repeat:'Bounded repeat',DoorOpen:'Request door open',Return:'Return'};
function message(text, error=false) { $('message').textContent=text; $('message').classList.toggle('error',error); }
async function action(action, extra={}) {
  if (busy) return;
  busy = true;
  document.body.setAttribute('aria-busy','true');
  message(action==='cook'?'Cooking preview…':'Working…');
  document.querySelectorAll('button').forEach(button=>button.disabled=true);
  try {
    const request = {action,...extra};
    if (action !== 'state') request.revision = state.revision;
    if (['cook','interact','breakpoint','continue','into','over','out','restart'].includes(action)) request.cursor=state.cursor;
    const response = await fetch('/action',{method:'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer '+document.querySelector('meta[name=session]').content},body:JSON.stringify(request)});
    const result = await response.json();
    if (result.state) { state=result.state; render(); }
    message(result.ok ? (action==='cook'?'Preview cooked; declared state retained on replacement.':action==='save'?'Sequence saved.': 'Ready.') : result.error, !result.ok);
    return result;
  } catch (error) { message(String(error),true); }
  finally { busy = false; document.body.setAttribute('aria-busy','false'); if(state)render(); }
}
function command(command) { return action('command',{command}); }
function button(parent,text,click) { const b=document.createElement('button');b.textContent=text;b.addEventListener('click',click);parent.append(b);return b; }
function currentNode() { return state.document.nodes.find(n=>n.id===selected); }
function activeNode() {
  const frame=state.preview?.runtime?.frames?.[0];
  if (!frame || !state.maps) return null;
  const spans=state.maps.spans.filter(s=>s.compiled_line===frame.compiled_line);
  return spans.length?spans[0].node:null;
}
function render() {
  document.querySelectorAll('button').forEach(button=>button.disabled=false);
  const doc=state.document, nodes=new Map(doc.nodes.map(n=>[n.id,n]));
  if (!nodes.has(selected)) selected=doc.root;
  $('document-status').textContent=(state.dirty?'Unsaved changes · ':'Saved · ')+'revision '+state.revision;
  $('undo').disabled=!state.undo; $('redo').disabled=!state.redo; $('save').disabled=!state.dirty;
  const runtime=state.preview?.runtime, paused=runtime?.partial;
  const changed=state.active_semantic && state.active_semantic!==state.semantic;
  $('running-status').textContent=runtime?(paused?'Paused':'Running revision '+runtime.revision)+(changed?' · draft differs':''):'Preview stopped';
  $('cook').disabled=!!paused || !!runtime?.faulted;
  for (const id of ['continue','into','over','out']) $(id).disabled=!paused;
  $('door0').disabled=$('door1').disabled=!runtime||!!paused||!!runtime.faulted;
  $('breakpoint').disabled=!runtime;
  $('outline').replaceChildren();
  function outline(id,depth) {
    const node=nodes.get(id);const b=button($('outline'),labels[node.kind],()=>{selected=id;render();});
    b.className='outline-node';b.classList.toggle('selected',id===selected);b.style.paddingLeft=(10+depth*12)+'px';b.dataset.node=id;
    for(const child of node.children) outline(child,depth+1);
  } outline(doc.root,0);
  const positions=new Map(doc.layout.positions.map(p=>[p.node,p]));
  doc.nodes.forEach((node,index)=>{if(!positions.has(node.id))positions.set(node.id,{x:80+(index%2)*280,y:60+index*105});});
  const width=Math.max(850,...[...positions.values()].map(p=>p.x+260));
  const height=Math.max(1100,...[...positions.values()].map(p=>p.y+240));
  for(const id of ['nodes','wires']){$(id).style.width=width+'px';$(id).style.height=height+'px';}
  const cards=$('nodes');cards.replaceChildren();$('edges').replaceChildren();
  const stopped=activeNode();
  doc.nodes.forEach(node=>{
    const position=positions.get(node.id);
    const card=document.createElement('div');card.className='node';card.dataset.node=node.id;
    card.classList.toggle('selected',node.id===selected);card.classList.toggle('paused',node.id===stopped);
    card.style.left=position.x+'px';card.style.top=position.y+'px';
    const title=document.createElement('div');title.className='node-title';title.textContent=labels[node.kind];
    const id=document.createElement('span');id.className='node-id';id.textContent=node.id.slice(-4);id.title=node.id;title.append(id);card.append(title);
    const body=document.createElement('div');body.className='node-body';card.append(body);
    if (['If','Repeat','Increment'].includes(node.kind)) {
      const field={If:'threshold',Repeat:'count',Increment:'amount'}[node.kind];
      const label=document.createElement('label');label.textContent=node.kind==='If'?'Threshold':node.kind==='Repeat'?'Iterations':'Amount';
      let input;
      if(node.kind==='Increment') {
        input=document.createElement('select');for(const value of ['event',1,2,3,4,5,6,7,8,9,10]) {const option=document.createElement('option');option.value=value;option.textContent=value==='event'?'Event':value;input.append(option);}
      } else {input=document.createElement('input');input.type='number';input.min='1';input.max=node.kind==='Repeat'?'4':'10';}
      input.setAttribute('aria-label',label.textContent+' '+node.id);input.value=node[field];
      const edit=()=>command({kind:'literal',node:node.id,value:input.value==='event'?'event':Number(input.value)});
      input.addEventListener('change',edit);input.addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();edit();}});
      label.append(input);body.append(label);
      if(node.kind==='If'){const unit=document.createElement('span');unit.textContent='interactions';body.append(unit);}
    } else {body.textContent=node.kind==='DoorOpen'?'Gameplay · DoorControl\nAccepted → OpenRequested\nRejected → open flag unchanged':node.kind==='Sequence'?node.children.length+' ordered steps':'End this invocation';}
    const port=document.createElement('div');port.className='port';port.textContent='● control in → control out ●';port.title=node.ports.in+' → '+node.ports.out;body.append(port);
    card.addEventListener('click',()=>{selected=node.id;render();});
    body.addEventListener('click',e=>{if(e.target.matches('input,select'))e.stopPropagation();});
    let drag;
    title.addEventListener('pointerdown',e=>{drag={x:e.clientX,y:e.clientY,from:position};title.setPointerCapture(e.pointerId);});
    title.addEventListener('pointermove',e=>{if(drag){card.style.left=(drag.from.x+e.clientX-drag.x)+'px';card.style.top=(drag.from.y+e.clientY-drag.y)+'px';}});
    title.addEventListener('pointerup',e=>{if(!drag)return;const dx=e.clientX-drag.x,dy=e.clientY-drag.y;selected=node.id;if(Math.abs(dx)+Math.abs(dy)>3)command({kind:'move',node:node.id,x:Math.round(drag.from.x+dx),y:Math.round(drag.from.y+dy)});else render();drag=null;});
    cards.append(card);
  });
  for(const node of doc.nodes) for(let index=0;index<node.children.length;index++){
    const child=node.children[index], predecessor=index===0?node.id:node.children[index-1];
    const a=positions.get(predecessor),b=positions.get(child);
    const edge=document.createElementNS('http://www.w3.org/2000/svg','path');
    edge.setAttribute('d',`M ${a.x+215} ${a.y+40} C ${a.x+260} ${a.y+40}, ${b.x-40} ${b.y+40}, ${b.x} ${b.y+40}`);
    edge.setAttribute('fill','none');edge.setAttribute('stroke','#6b8299');edge.setAttribute('stroke-width','2');edge.setAttribute('marker-end','url(#arrow)');$('edges').append(edge);
  }
  const node=currentNode();$('selection').replaceChildren();
  const info=document.createElement('div');info.textContent=labels[node.kind]+'\nStable node '+node.id+'\n'+node.children.length+' ordered children';$('selection').append(info);
  if(node.kind==='DoorOpen') {const op=state.operation;const description=document.createElement('p');description.textContent=`${op.name} · ${op.phase} · ${op.capability} · ${op.effect}. Target: EntityRef; Power: uint32. Failure leaves the open request unchanged.`;$('selection').append(description);}
  const isBlock=['Sequence','If','Repeat'].includes(node.kind);
  $('paste').disabled=!copied||!nodes.has(copied)||!isBlock; $('remove').disabled=selected===doc.root;
  const parent=doc.nodes.find(n=>n.children.includes(selected)), childIndex=parent?parent.children.indexOf(selected):-1;
  $('up').disabled=childIndex<=0;$('down').disabled=!parent||childIndex>=parent.children.length-1;
  $('restart').disabled=!runtime;
  $('runtime').replaceChildren();
  for(let i=0;i<2;i++){const div=document.createElement('div');div.className='state';const title=document.createElement('strong');title.textContent='Door '+(i+1);div.append(title);const value=document.createElement('span');value.textContent=runtime?(state.preview.open[i]?'Open':'Closed')+' · '+runtime.states[i].interactions+' interactions':'Preview not started';div.append(value);$('runtime').append(div);}
  const badge=document.createElement('p');badge.className='badge';badge.textContent=paused?'Paused changes are unpublished. Finish this tick before replacing.':changed?'Running state belongs to the prior source revision. Cook to replace.':'Declared state belongs to the running sequence.';$('runtime').append(badge);
  if(runtime?.operation===200){const result=document.createElement('p');result.textContent='RequestDoorOpen: '+state.preview.native_result;$('runtime').append(result);}
  $('debugger').replaceChildren();
  const debug=document.createElement('p');debug.textContent=paused?('Stopped at '+(nodes.get(stopped)?.kind||'active source')+' · node '+(stopped||'unmapped')+'\nTick '+runtime.tick):runtime?.faulted?'Runtime faulted. Reset preview before running again.':'No paused invocation';$('debugger').append(debug);
  for(const local of runtime?.locals||[]) {const div=document.createElement('div');div.textContent=local.name+' = '+(local.kind===2?local.number:local.kind===1?local.boolean:local.kind===3?'bytes '+local.bytes_hex:'opaque');$('debugger').append(div);}
  $('changes').textContent=state.review.semantic.length?state.review.semantic.map(c=>`${c.id} · ${c.field}\n${JSON.stringify(c.before)} → ${JSON.stringify(c.after)}`).join('\n\n'):'No executable changes.';
  $('changes').textContent+='\n\nLayout metadata: '+(state.review.layout?'changed':'unchanged');
  $('source').textContent=showActive?(state.maps?.source||'No running source'):state.draft_source;
  palette();
  if(busy)document.querySelectorAll('button').forEach(button=>button.disabled=true);
}
function palette(){const parent=$('palette');parent.replaceChildren();for(const kind of kinds.filter(k=>(labels[k]+' '+k).toLowerCase().includes($('search').value.toLowerCase()))){const b=button(parent,labels[kind],()=>command({kind:'add',parent:selected,type:kind}));b.disabled=!['Sequence','If','Repeat'].includes(currentNode()?.kind);}}
for(const id of ['save','undo','redo','cook','continue','into','over','out','restart']) $(id).addEventListener('click',()=>action(id));
$('door0').addEventListener('click',()=>action('interact',{instance:0,amount:1}));$('door1').addEventListener('click',()=>action('interact',{instance:1,amount:1}));
$('breakpoint').addEventListener('click',()=>action('breakpoint',{node:selected,enabled:true}));
$('copy').addEventListener('click',()=>{copied=selected;render();message('Subtree copied. Paste into a selected block.');});
$('paste').addEventListener('click',()=>command({kind:'paste',parent:selected,node:copied}));$('remove').addEventListener('click',()=>command({kind:'remove',node:selected}));
function reorder(delta){const parent=state.document.nodes.find(n=>n.children.includes(selected));if(!parent)return;const children=[...parent.children],index=children.indexOf(selected),next=index+delta;if(next<0||next>=children.length)return;[children[index],children[next]]=[children[next],children[index]];command({kind:'order',node:parent.id,children});}
$('up').addEventListener('click',()=>reorder(-1));$('down').addEventListener('click',()=>reorder(1));$('search').addEventListener('input',palette);
$('review').addEventListener('click',()=>{$('review-panel').open=!$('review-panel').open;});
$('draft-source').addEventListener('click',()=>{showActive=false;render();});$('active-source').addEventListener('click',()=>{showActive=true;render();});
$('merge-file').addEventListener('change',async event=>{try{const file=event.target.files[0];if(file.size>131072)throw Error('Branch document is too large');await action('merge',{remote:JSON.parse(await file.text())});}catch(error){message(String(error),true);}event.target.value='';});
action('state');

$('reopen').addEventListener('click',()=>{if(!state.dirty||confirm('Discard unsaved sequence edits and reopen the saved file?'))action('reopen',{discard:true});});
