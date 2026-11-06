// Unit-test our own UI logic in a minimal document/canvas double. This is not
// a browser automation fallback or a claim of visual verification in a browser.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(process.argv[2],'utf8'),code=[...html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g)].at(-1)[1];
let raf,draws=0;
const context2d=new Proxy({},{get:(target,key)=>key in target?target[key]:(...args)=>{if(key==='lineTo'||key==='arc')assert(args.every(Number.isFinite));draws++;},set:(target,key,value)=>(target[key]=value,true)});
class Element{constructor(id=''){this.id=id;this.children=[];this.value='0';this.textContent='';this.dataset={};this.classList={toggle(){}};}getContext(){return context2d;}getBoundingClientRect(){return {width:900,height:this.id==='stage'?430:88};}append(e){this.children.push(e);}replaceChildren(){this.children=[];}setPointerCapture(){}}
const ids=['stage','timeline','initial','play','speed','seek','message','clock','frame','event','tracking','native','cadence','comparison','events','filters','front','side','file','status'];
const elements=Object.fromEntries(ids.map(id=>[id,new Element(id)]));elements.speed.value='1';
for(const filter of ['all','tracking','fist','run','block']){const b=new Element();b.dataset.filter=filter;elements.filters.append(b);}
const hand={active:true,joints:Array.from({length:26},(_,i)=>({p:[i*.005,1.2,-.3],tracked:true}))};
const sample=i=>({id:i,focused:true,head:[0,1.6,0],hands:[hand,hand]});
const event=(t,i,rest={})=>({t,sample:i,type:'sample',status:'CHOD',mode:'arm_swing',mask:0,sprint:false,fist:false,half_strokes:0,starts:0,blocks:0,physical:['0','0'],mismatch:false,...rest});
const data={schema:1,complete:true,overridden:false,mismatches:0,samples:[sample(1),sample(2),sample(3),sample(4)],events:[event(0,0),event(500,1,{fist:true,half_strokes:1}),event(1000,2,{fist:true,sprint:true,mask:132,starts:1}),event(1300,3,{type:'reset:physical_keyboard',blocks:8})]};
elements.initial.textContent=JSON.stringify(data);
const sandbox={document:{getElementById:id=>elements[id],createElement:()=>new Element()},window:{addEventListener(){}},requestAnimationFrame:f=>raf=f,performance:{now:()=>0},devicePixelRatio:1,console};
vm.createContext(sandbox);vm.runInContext(code,sandbox,{filename:'hand_replay_viewer.html'});
(async()=>{
 assert(draws>100,'skeleton/timeline did not render');assert.match(elements.frame.textContent,/arm_swing/);
 elements.play.onclick();raf(17);raf(600);assert.equal(elements.clock.textContent,'0,500 s','playback failed to accumulate fractional frames');
 raf(1100);assert.equal(elements.clock.textContent,'1,000 s');raf(1500);assert.equal(elements.play.textContent,'Odtwórz');
 elements.filters.onclick({target:elements.filters.children[3]});assert.equal(elements.events.children.length,1);assert.match(elements.events.children[0].textContent,/Start sprintu/);
 elements.events.children[0].onclick();assert.equal(elements.clock.textContent,'1,000 s');
 elements.seek.value='1';elements.seek.oninput({target:elements.seek});assert.equal(elements.clock.textContent,'0,500 s');
 const before=draws;elements.side.onclick();elements.front.onclick();elements.stage.onpointerdown({clientX:0,clientY:0,pointerId:1});elements.stage.onpointermove({clientX:30,clientY:10});elements.stage.onpointerup();assert(draws>before,'camera controls did not redraw');
 const lost=JSON.parse(JSON.stringify(data.samples[1]));lost.hands[0].joints[1].tracked=false;
 const overlapping={...data,samples:[data.samples[0],lost],events:[event(0,0,{blocks:8}),event(500,1,{blocks:8,type:'reset:tracking',fist:true,sprint:true,half_strokes:1}),event(1000,1,{blocks:8,fist:true,sprint:true,half_strokes:1})]};
 await elements.file.onchange({target:{files:[{size:100,text:async()=>JSON.stringify(overlapping)}]}});
 for(const [button,label] of [[1,/brak śledzenia/],[2,/pięści|Półwymach/],[3,/Start sprintu/]]){
  elements.filters.onclick({target:elements.filters.children[button]});
  assert.equal(elements.events.children.length,1);assert.match(elements.events.children[0].textContent,label,'overlapping category was hidden');
 }
 elements.filters.onclick({target:elements.filters.children[4]});assert.equal(elements.events.children.length,2,'unchanged block state flooded the timeline');
 await elements.file.onchange({target:{files:[{size:100,text:async()=>JSON.stringify({...data,events:[event(20,0),event(10,1)]})}]}});assert.match(elements.message.textContent,/kolejność/);
 await elements.file.onchange({target:{files:[{size:200*1024*1024,text:async()=>''}]}});assert.match(elements.message.textContent,/160 MiB/);
 console.log('PASS viewer UI logic: play/pause, accumulated time, seek, event filter, camera, canvas coordinates and invalid files');
})().catch(error=>{console.error(error);process.exitCode=1;});
