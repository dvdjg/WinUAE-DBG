#!/usr/bin/env node
/**
 * Patch Amiga Debug client.js: Screen tab palette + timeline CRT sweep.
 * Re-run after extension updates (path below or pass as argv[1]).
 */
const fs = require('fs');
const path =
	process.argv[2] ||
	(process.env.USERPROFILE || process.env.HOME) ||
		'/.cursor/extensions/bartmanabyss.amiga-debug-1.8.2/dist/client.js';

if (!fs.existsSync(path)) {
	console.error('client.js not found:', path);
	process.exit(1);
}

let s = fs.readFileSync(path, 'utf8');
if (s.includes('dma_loop:for(let e=0;e<zt;e++)')) {
	console.log('Already patched:', path);
	process.exit(0);
}

const patches = [
	{
		name: 'palette init',
		from: 'ne=new Uint32Array(256);let re=-1',
		to:
			'ne=new Uint32Array(256);{const _pal=hn(new Uint16Array(J));for(let _pi=0;_pi<_pal.length;_pi++)ne[_pi]=_pal[_pi];if(n.amiga.agaColors&&n.amiga.agaColors.length)for(let _pi=0;_pi<256&&_pi<n.amiga.agaColors.length;_pi++)ne[_pi]=n.amiga.agaColors[_pi]>>>0}let re=-1',
	},
	{
		name: 'useMemo time dep',
		from: ',[e,n,-1!==s.freeze?r:0,s])',
		to: ',[e,n,r,s])',
	},
	{
		name: 'timeline CRT sweep (Live)',
		from: '}for(let e=0;e<zt;e++)for(let t=0;t<Vt;t++){const r=n.amiga.dmaRecords[e*Vt+t];',
		to:
			'}const _totalDma=zt*Vt;let _maxDma=_totalDma;if(-1===i.freeze){const _dur=t.duration>0?t.duration:1;const _pos=Math.min(1,Math.max(0,r/_dur));_maxDma=Math.max(Vt,Math.min(_totalDma,Math.round(_pos*_totalDma)))}dma_loop:for(let e=0;e<zt;e++)for(let t=0;t<Vt;t++){const _di=e*Vt+t;if(_di>=_maxDma)break dma_loop;const r=n.amiga.dmaRecords[_di];',
	},
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
console.log('Patched:', path);
