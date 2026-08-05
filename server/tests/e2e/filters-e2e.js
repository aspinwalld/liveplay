// Do the channel high-pass and low-pass actually filter audio, end to end?
//
// The unit tests prove the sections are correct in isolation. This proves the
// whole path: REST -> bus definition -> engine coefficients -> the render
// thread -> the bus meter. It is the part that would still be broken if the
// parameters never reached the strip.
//
// The test signal is a repeating sweep, so a filter shows up as a change in
// the level reaching the bus rather than needing a per-frequency measurement.
// The sweep spends its time between 200 Hz and 2 kHz, so:
//   * a high-pass parked well above that removes most of it
//   * a low-pass parked well below that removes most of it
//   * both parked at the ends of their travel change nothing at all
//
// usage: node filters-e2e.js <port> <wavPath>
const WebSocket = require(require.resolve('ws', { paths: [process.cwd()] }));
const PORT = process.argv[2], WAV = process.argv[3];
const BASE = `http://127.0.0.1:${PORT}`;

let failures = 0;
const ok = (n, p, d = '') => { console.log(`${p ? 'PASS' : 'FAIL'}  ${n}${d ? '   ' + d : ''}`); if (!p) failures++; };
const rest = async (p, o = {}) => {
  const r = await fetch(BASE + p, { headers: { 'content-type': 'application/json' }, ...o });
  const t = await r.text();
  try { return { status: r.status, body: JSON.parse(t) }; } catch { return { status: r.status, body: t }; }
};
const sleep = ms => new Promise(r => setTimeout(r, ms));

class Meters {
  constructor(ws) { this.f = []; ws.on('message', raw => {
    let m; try { m = JSON.parse(raw); } catch { return; }
    if (m.type === 'meters') this.f.push(m); }); }
  reset() { this.f = []; }
  // RMS-ish: the mean of the per-frame RMS readings over the window, which is
  // far steadier than a peak across a sweep.
  rms(id) {
    let sum = 0, n = 0;
    for (const fr of this.f) for (const c of (fr.mixer_channels || []))
      if (c.mixer_id === id && (c.rms_db ?? -200) > -190) { sum += c.rms_db; n++; }
    return n ? sum / n : -200;
  }
}
const measure = async (m, ms) => { m.reset(); await sleep(ms); };

const uuid = 'item-filters-0001';

(async () => {
  await rest('/api/project/document', {
    method: 'PUT',
    body: JSON.stringify({ name: 'filters', items: [{
      uuid, type: 'audio', displayName: 'Sweep', mediaServerPath: WAV,
      volume: 1, endBehavior: 'stop' }] }),
  });
  for (let i = 0; i < 80; i++) {
    const p = await rest('/api/project/progress');
    if (p.body && p.body.loading === false) break;
    await sleep(100);
  }

  const bus = (await rest('/api/buses', {
    method: 'POST', body: JSON.stringify({ name: 'Filt', width: 2 }) })).body.id;
  await rest(`/api/project/items/${uuid}`, {
    method: 'PATCH', body: JSON.stringify({ busId: bus }) });

  let buses = (await rest('/api/buses')).body;
  const b = buses.find(x => x.id === bus);
  ok('the bus reports its tone controls', !!b.dsp && !!b.dsp.hpf && !!b.dsp.lpf,
     JSON.stringify(b.dsp));
  ok('filters start parked out of circuit',
     b.dsp.hpf.freq === 20 && b.dsp.lpf.freq === 20000,
     `hpf ${b.dsp.hpf.freq} lpf ${b.dsp.lpf.freq}`);

  const ws = new WebSocket(`ws://127.0.0.1:${PORT}/ws`);
  await new Promise((res, rej) => { ws.on('open', res); ws.on('error', rej); });
  const m = new Meters(ws);

  await rest(`/api/project/items/${uuid}/play`, { method: 'POST', body: '{}' });
  await sleep(700);
  await measure(m, 1200);
  const flat = m.rms(b.mixerId);
  ok('signal is reaching the bus', flat > -40, `${flat.toFixed(1)} dB RMS`);

  // ---- High-pass, live over the drag endpoint ----
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ hpf: { freq: 4000, q: 0.7071 } }) });
  await sleep(700);
  await measure(m, 1200);
  const hp = m.rms(b.mixerId);
  ok('a 4 kHz high-pass removes the sweep', hp < flat - 10,
     `${flat.toFixed(1)} -> ${hp.toFixed(1)} dB RMS`);

  // Parking it again must restore the signal exactly — this is the "out of
  // circuit" rule, and a filter that stayed slightly in would show up here.
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ hpf: { freq: 20, q: 0.7071 } }) });
  await sleep(700);
  await measure(m, 1200);
  const parked = m.rms(b.mixerId);
  ok('parking the high-pass restores it', Math.abs(parked - flat) < 0.6,
     `${flat.toFixed(1)} -> ${parked.toFixed(1)} dB RMS`);

  // ---- Low-pass ----
  // 300 Hz, not 1 kHz. The sweep runs 200 Hz to 2 kHz linearly in frequency,
  // so a 1 kHz corner leaves 44% of its running time in the passband and only
  // takes about 2 dB off the total — which is the correct answer for that
  // signal, and a poor test of whether the filter is in circuit. 300 Hz puts
  // nearly all of the sweep in the stopband, mirroring the high-pass check.
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ lpf: { freq: 300, q: 0.7071 } }) });
  await sleep(700);
  await measure(m, 1200);
  const lp = m.rms(b.mixerId);
  ok('a 300 Hz low-pass removes the sweep', lp < flat - 10,
     `${flat.toFixed(1)} -> ${lp.toFixed(1)} dB RMS`);

  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ lpf: { freq: 20000, q: 0.7071 } }) });
  await sleep(700);
  await measure(m, 1200);
  ok('parking the low-pass restores it', Math.abs(m.rms(b.mixerId) - flat) < 0.6,
     `${flat.toFixed(1)} -> ${m.rms(b.mixerId).toFixed(1)} dB RMS`);

  // ---- Persistence through PATCH ----
  await rest(`/api/buses/${bus}`, {
    method: 'PATCH',
    body: JSON.stringify({ dsp: { hpf: { freq: 250, q: 0.7071 },
                                  lpf: { freq: 9000, q: 0.7071 } } }) });
  buses = (await rest('/api/buses')).body;
  const after = buses.find(x => x.id === bus);
  ok('PATCH persists the filter settings',
     after.dsp.hpf.freq === 250 && after.dsp.lpf.freq === 9000,
     `hpf ${after.dsp.hpf.freq} lpf ${after.dsp.lpf.freq}`);

  // ...and survives a whole-document save, the path every property edit takes.
  const doc = (await rest('/api/project')).body;
  await rest('/api/project/document', {
    method: 'PUT',
    body: JSON.stringify({ name: doc.name, items: doc.items,
                           cartItems: doc.cartItems ?? [],
                           settings: doc.settings, theme: doc.theme }) });
  buses = (await rest('/api/buses')).body;
  const saved = buses.find(x => x.id === bus);
  ok('filters survive a document round trip',
     saved && saved.dsp.hpf.freq === 250 && saved.dsp.lpf.freq === 9000,
     saved ? `hpf ${saved.dsp.hpf.freq} lpf ${saved.dsp.lpf.freq}` : '(bus missing)');

  await rest(`/api/project/items/${uuid}/stop`, { method: 'POST', body: '{}' });
  ws.close();
  console.log(`\n${failures === 0 ? 'ALL PASS' : 'FAILURES'} (${failures})`);
  process.exit(failures === 0 ? 0 : 1);
})().catch(e => { console.error('harness error:', e); process.exit(2); });
