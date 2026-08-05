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
  // Mean POWER across the window, converted to dB once at the end.
  //
  // Averaging the dB readings instead is not the same thing: it is a geometric
  // mean, it weights quiet frames far too heavily, and it made two identical
  // flat chains read 0.4 dB apart depending on where the measurement window
  // happened to land in the sweep. Averaging power first is the honest figure
  // and it is stable to within a tenth of a dB.
  rms(id) {
    let sum = 0, n = 0;
    for (const fr of this.f) for (const c of (fr.mixer_channels || []))
      if (c.mixer_id === id && (c.rms_db ?? -200) > -190) {
        sum += Math.pow(10, c.rms_db / 10);
        n++;
      }
    return n ? 10 * Math.log10(sum / n) : -200;
  }
}
const measure = async (m, ms) => { m.reset(); await sleep(ms); };

// How long to leave a change alone before believing the meter about it.
//
// Two things have to finish first. The engine ramps coefficients onto their
// new values over about 340 ms rather than snapping to them, and then the bus
// RMS meter integrates over a window of its own, so its readings lag the audio
// by a few hundred milliseconds more. At 700 ms the measurement window still
// caught the tail of a +12 dB boost and read 0.3 dB hot, which looked exactly
// like an EQ band failing to flatten. It was not: measured from 1.5 s the
// chain reads -9.03 dB, the exact RMS of the 0.5-amplitude test signal.
const SETTLE_MS = 1500;

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
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  const flat = m.rms(b.mixerId);
  ok('signal is reaching the bus', flat > -40, `${flat.toFixed(1)} dB RMS`);

  // ---- High-pass, live over the drag endpoint ----
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ hpf: { freq: 4000, q: 0.7071 } }) });
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  const hp = m.rms(b.mixerId);
  ok('a 4 kHz high-pass removes the sweep', hp < flat - 10,
     `${flat.toFixed(1)} -> ${hp.toFixed(1)} dB RMS`);

  // Parking it again must restore the signal exactly - this is the "out of
  // circuit" rule, and a filter that stayed slightly in would show up here.
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ hpf: { freq: 20, q: 0.7071 } }) });
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  const parked = m.rms(b.mixerId);
  ok('parking the high-pass restores it', Math.abs(parked - flat) < 0.6,
     `${flat.toFixed(1)} -> ${parked.toFixed(1)} dB RMS`);

  // ---- Low-pass ----
  // 300 Hz, not 1 kHz. The sweep runs 200 Hz to 2 kHz linearly in frequency,
  // so a 1 kHz corner leaves 44% of its running time in the passband and only
  // takes about 2 dB off the total - which is the correct answer for that
  // signal, and a poor test of whether the filter is in circuit. 300 Hz puts
  // nearly all of the sweep in the stopband, mirroring the high-pass check.
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ lpf: { freq: 300, q: 0.7071 } }) });
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  const lp = m.rms(b.mixerId);
  ok('a 300 Hz low-pass removes the sweep', lp < flat - 10,
     `${flat.toFixed(1)} -> ${lp.toFixed(1)} dB RMS`);

  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ lpf: { freq: 20000, q: 0.7071 } }) });
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  ok('parking the low-pass restores it', Math.abs(m.rms(b.mixerId) - flat) < 0.6,
     `${flat.toFixed(1)} -> ${m.rms(b.mixerId).toFixed(1)} dB RMS`);

  // ---- EQ bands ----
  // Park the low-pass first so the bands are measured against a flat chain.
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ lpf: { freq: 20000, q: 0.7071 } }) });

  const flatBands = [
    { freq: 100,   gain: 0, q: 0.7 },
    { freq: 500,   gain: 0, q: 1.0 },
    { freq: 2500,  gain: 0, q: 1.0 },
    { freq: 10000, gain: 0, q: 0.7 },
  ];
  ok('bands default to a conventional layout',
     JSON.stringify(b.dsp.eq.map(e => e.freq)) === JSON.stringify([100, 500, 2500, 10000]),
     JSON.stringify(b.dsp.eq.map(e => e.freq)));

  // Four bands at 0 dB must be a genuine no-op: every strip has them, so if
  // they coloured anything the whole desk would drift.
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ eq: flatBands }) });
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  ok('four flat bands change nothing', Math.abs(m.rms(b.mixerId) - flat) < 0.3,
     `${flat.toFixed(1)} -> ${m.rms(b.mixerId).toFixed(1)} dB RMS`);

  // A wide cut over the sweep's whole range must show up plainly.
  const cut = flatBands.map((e, i) =>
    i === 1 ? { freq: 700, gain: -18, q: 0.4 } : e);
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ eq: cut }) });
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  const cutDb = m.rms(b.mixerId);
  ok('a wide -18 dB band cuts the sweep', cutDb < flat - 6,
     `${flat.toFixed(1)} -> ${cutDb.toFixed(1)} dB RMS`);

  // ...and the matching boost must go the other way.
  const boost = flatBands.map((e, i) =>
    i === 1 ? { freq: 700, gain: 12, q: 0.4 } : e);
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ eq: boost }) });
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  const boostDb = m.rms(b.mixerId);
  ok('a wide +12 dB band lifts the sweep', boostDb > flat + 4,
     `${flat.toFixed(1)} -> ${boostDb.toFixed(1)} dB RMS`);

  // Back to flat, exactly.
  await rest(`/api/buses/${bus}/dsp`, {
    method: 'POST', body: JSON.stringify({ eq: flatBands }) });
  await sleep(SETTLE_MS);
  await measure(m, 2000);
  ok('flattening the bands restores it', Math.abs(m.rms(b.mixerId) - flat) < 0.3,
     `${flat.toFixed(1)} -> ${m.rms(b.mixerId).toFixed(1)} dB RMS`);

  // ---- Persistence through PATCH ----
  await rest(`/api/buses/${bus}`, {
    method: 'PATCH',
    body: JSON.stringify({ dsp: { hpf: { freq: 250, q: 0.7071 },
                                  lpf: { freq: 9000, q: 0.7071 },
                                  eq: boost } }) });
  buses = (await rest('/api/buses')).body;
  const after = buses.find(x => x.id === bus);
  ok('PATCH persists the filter settings',
     after.dsp.hpf.freq === 250 && after.dsp.lpf.freq === 9000,
     `hpf ${after.dsp.hpf.freq} lpf ${after.dsp.lpf.freq}`);
  ok('PATCH persists the EQ bands',
     after.dsp.eq[1].freq === 700 && after.dsp.eq[1].gain === 12,
     JSON.stringify(after.dsp.eq[1]));

  // A partial patch must leave the rest alone: the panels send only what
  // they own, and a band edit must not wipe the filters.
  await rest(`/api/buses/${bus}`, {
    method: 'PATCH', body: JSON.stringify({ dsp: { eq: flatBands } }) });
  buses = (await rest('/api/buses')).body;
  const partial = buses.find(x => x.id === bus);
  ok('a bands-only patch leaves the filters alone',
     partial.dsp.hpf.freq === 250 && partial.dsp.lpf.freq === 9000,
     `hpf ${partial.dsp.hpf.freq} lpf ${partial.dsp.lpf.freq}`);

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
