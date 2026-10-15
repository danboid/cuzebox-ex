const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const html = fs.readFileSync('tools/web_debugger/debugger.html', 'utf8');
for (const match of html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)) new vm.Script(match[1]);
const code = html.slice(html.indexOf('let debugStepBusy='), html.indexOf('async function toggleBeam()'));
assert(code.includes('async function debugStep('));
const mode = {value:'source'};
const buttons = [{disabled:false}, {disabled:false}, {disabled:false}];
const calls = [];
let failArm = false;
const context = {
 $: id => {assert.equal(id, 'stepMode'); return mode},
 document: {querySelectorAll: () => buttons},
 localStorage: {getItem: () => 'instruction', setItem: (key,value) => {assert.equal(key,'cuzebox.stepMode'); assert.equal(value,mode.value)}},
 beamStepOrigin:null, sdStepOrigin:null,
 command: async cmd => {
  calls.push(cmd);
  return {ok:!(failArm && /^(SOURCE_STEP|STEP_)/.test(cmd)), paused:true, pc:9};
 },
 waitDebugStepStop: async () => ({paused:true}),
};
for (const name of ['refreshSdStatus','refreshBeam','refreshSdTrace','refreshSdFsHistory','refreshSdFaults','refreshSdReplay','refreshAudioDebug','refresh']) context[name] = async () => {};
vm.createContext(context);
vm.runInContext(code, context);
(async () => {
 assert.equal(mode.value,'instruction', 'restore saved mode');
 for (const operation of ['INTO','OVER','OUT']) {
  mode.value='source'; calls.length=0;
  await context.debugStep('STEP_'+operation);
  assert(calls.includes('SOURCE_STEP '+operation));
  assert(!calls.includes('STEP_'+operation));
  assert(buttons.every(b=>!b.disabled));
 }
 mode.value='instruction'; calls.length=0;
 await context.debugStep('STEP_OVER'); assert(calls.includes('STEP_OVER'));
 context.saveStepMode();
 mode.value='source'; calls.length=0;
 await Promise.all([context.debugStep('STEP_INTO'),context.debugStep('STEP_INTO')]);
 assert.equal(calls.filter(c=>c==='SOURCE_STEP INTO').length,1, 'ignore overlapping clicks');
 failArm=true; await context.debugStep('STEP_INTO');
 assert(buttons.every(b=>!b.disabled), 'failed arm releases controls');
 console.log('Web source stepping regression: PASS');
})().catch(error => {console.error(error); process.exitCode=1});
