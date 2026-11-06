// Exercise offline viewer navigation and rendering with a small DOM/canvas double.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(process.argv[2],'utf8'),code=html.match(/<script>([\s\S]*?)<\/script>/)[1];
let timer,draws=0,delay;
const context=new Proxy({}, {get:(_,key)=> (...args)=>{if(['arc','lineTo','moveTo'].includes(key))assert(args.every(Number.isFinite));draws++;},set:()=>true});
const elements={};for(const id of ['time','hands','stamp','diagnostics','config','play'])elements[id]={value:0,textContent:'',getContext:()=>context,addEventListener(name,fn){this[name]=fn;}};
const hand={active:true,joints:Array.from({length:26},()=>({position:[.2,1.5,-.3],flags:5}))};
const samples=[0,1,2].map(i=>({elapsed_ms:i*100,frame:i+1,settings:{steering:{full_lock_degrees:90+i}},diagnostics:['FRAME '+(i+1)],hands:[hand,{active:false,joints:[]}]}));
samples[1].hands=[{...hand,joints:hand.joints.map((j,i)=>i?j:{position:[null,0,0],flags:0})},{active:false,joints:[]}];
const sandbox={document:{getElementById:id=>elements[id]},setTimeout(fn,ms){timer=fn;delay=ms;return 1;},clearTimeout(){timer=null;}};
vm.createContext(sandbox);vm.runInContext(code.replace('__FLIGHT_DATA__',JSON.stringify({samples})),sandbox);
assert(draws>100);assert.match(elements.stamp.textContent,/klatka 1/);
elements.play.onclick();assert.equal(delay,100);assert.equal(elements.time.value,0,'play skipped first sample');timer();assert.equal(+elements.time.value,1);
elements.play.onclick();assert.equal(timer,null,'pause left timer active');
elements.time.value=2;elements.time.input();assert.equal(elements.diagnostics.textContent,'FRAME 3');assert.match(elements.config.textContent,/92/);
elements.play.onclick();assert.equal(+elements.time.value,0,'play at end did not restart');timer();timer();assert.equal(+elements.time.value,2);assert.equal(elements.play.textContent,'Odtwórz');
console.log('PASS flight viewer: pose drawing, invalid joints, seek, timed playback, pause and restart');
