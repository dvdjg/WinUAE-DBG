#!/usr/bin/env node
/**
 * Patch Amiga Debug extension.js: wait for real program sections (not CON 0xff9c20)
 * before symbolTable.relocate(), and re-relocate on first SIGTRAP stop in a.exe.
 * Re-run after extension updates.
 */
const fs = require('fs');
const path =
  process.argv[2] ||
  (process.env.USERPROFILE || process.env.HOME) +
    '/.cursor/extensions/bartmanabyss.amiga-debug-1.8.2/dist/extension.js';

if (!fs.existsSync(path)) {
  console.error('extension.js not found:', path);
  process.exit(1);
}

let s = fs.readFileSync(path, 'utf8');

if (s.includes('pollProgramSections')) {
  console.log('Already patched:', path);
  process.exit(0);
}

const getSectionsNeedle =
  'getSections(){const e=await this.sendUserInput("info file"),t=[];if(e){const n=/0x([0-9a-fA-F]+) - +0x([0-9a-fA-F]+) is (.*)/;e.output.forEach((e=>{let s;(s=n.exec(e))&&t.push({name:s[3],address:parseInt(s[1],16),size:parseInt(s[2],16)-parseInt(s[1],16)})}))}return t}';

const getSectionsReplacement =
  'getSections(){const e=await this.sendUserInput("info file"),t=[];if(e){const n=/0x([0-9a-fA-F]+) - +0x([0-9a-fA-F]+) is (.*)/;e.output.forEach((e=>{let s;(s=n.exec(e))&&t.push({name:s[3],address:parseInt(s[1],16),size:parseInt(s[2],16)-parseInt(s[1],16)})}))}return t}filterProgramSections(e){return e.filter((e=>e.size>=512&&e.address>=1024&&e.address<33554432&&!e.name.toLowerCase().includes("kick")))}async pollProgramSections(){for(let e=0;e<180;e++){const t=this.filterProgramSections(await this.getSections());if(t.length>0)return t;await new Promise((e=>setTimeout(e,500)))}return this.getSections()}';

const initNeedle =
  'const e=this.getSections().then((e=>{this.emit("sections-loaded",e)}));c.push(e)';
const initReplacement =
  'const e=this.pollProgramSections().then((e=>{this.emit("sections-loaded",e)}));c.push(e)';

const signalNeedle =
  'signalStopEvent(e){const t=e.record("signal-name");this.stoppedReason="SIGEMT"===t?"TRAP #7 (undefined behavior)":"SIGSEGV"===t?"NULL access (undefined behavior)":"SIGBUS"===t?"address error":"SIGILL"===t?"illegal instruction":"user request",this.stopped=!0,this.disableSendStoppedEvents?this.stoppedEventPending=!0:(this.sendEvent(new h.StoppedEvent(this.stoppedReason,this.currentThreadId)),this.sendEvent(new I(this.stoppedReason,this.currentThreadId)))}';

const signalReplacement =
  'signalStopEvent(e){const t=e.record("signal-name");this.stoppedReason="SIGEMT"===t?"TRAP #7 (undefined behavior)":"SIGSEGV"===t?"NULL access (undefined behavior)":"SIGBUS"===t?"address error":"SIGILL"===t?"illegal instruction":"user request",this.stopped=!0;const n=async()=>{if(this.symbolTable&&this.miDebugger){const e=this.miDebugger.filterProgramSections(await this.miDebugger.getSections());e.length>0&&this.symbolTable.relocate(e)}};n().catch((()=>{}));this.disableSendStoppedEvents?this.stoppedEventPending=!0:(this.sendEvent(new h.StoppedEvent(this.stoppedReason,this.currentThreadId)),this.sendEvent(new I(this.stoppedReason,this.currentThreadId)))}';

const patches = [
  { name: 'pollProgramSections', from: getSectionsNeedle, to: getSectionsReplacement },
  { name: 'init poll', from: initNeedle, to: initReplacement },
  { name: 'relocate on stop', from: signalNeedle, to: signalReplacement },
];

for (const p of patches) {
  const n = s.split(p.from).length - 1;
  if (n !== 1) {
    console.error(`Patch "${p.name}" failed: expected 1 match, got ${n}`);
    process.exit(1);
  }
  s = s.replace(p.from, p.to);
  console.log('OK', p.name);
}

fs.writeFileSync(path, s);
const vscodePath = path.replace('.cursor', '.vscode');
if (vscodePath !== path && fs.existsSync(vscodePath)) {
  fs.writeFileSync(vscodePath, s);
  console.log('Also patched:', vscodePath);
}
console.log('Patched:', path);
