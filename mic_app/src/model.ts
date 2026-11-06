export interface Telemetry {
  bf_mode?:number; bf_azimuth_tdeg?:number; bf_elevation_tdeg?:number; bf_revision?:number;
  bf_pending?:number; bf_fade_left?:number; bf_delays_milli?:number[];
  nr_hum?:number;nr_channel?:number;settings_saved?:number;settings_saving?:number;settings_error?:number;uac_active?:number;
  nr_mode?:number; nr_learned?:number; nr_learn_left?:number;
  v: 1; epoch: number; seq: number; uptime_ms: number; enabled: number; current: number;
  age_ms: number; end_age_us: number; interval_ms: number; publish_end_us: number;
  angle_mdeg: number; reject_mask: number; rms_milli: number[]; lag_milli: number[];
  coherence_milli: number[]; fit_milli: number; dropped: number; faults: number;
  overwritten: number; resets: number;
}
export interface Sample { at: number; data: Telemetry }
export interface Trial {
  id: string; started: string; mode: string; expected: number; distance: string;
  elevation: string; notes: string; samples: Sample[];
}
export interface Session { schema: 'mic_app.session'; version: 1; trials: Trial[] }
export const reasons = [
  [1, 'Low level'], [2, 'Clipped'], [4, 'Insufficient broadband evidence'],
  [8, 'Delay outside physical bound'], [16, 'Poor direction fit'],
  [32, 'Near array normal'], [64, 'Invalid direction vector'],
] as const;
export function rejection(mask: number) { return reasons.filter(([bit]) => mask & bit).map(([, name]) => name); }
export function angleError(actual: number, expected: number) { return ((actual - expected + 540) % 360 + 360) % 360 - 180; }
export function usable(d: Telemetry) { return d.enabled === 1 && d.current === 1 && d.reject_mask === 0 && d.angle_mdeg >= 0 && d.age_ms <= 700; }
export function validateTelemetry(value: unknown): value is Telemetry {
  if (!value || typeof value !== 'object') return false;
  const d = value as Record<string, unknown>;
  if (d.v !== 1) return false;
  const keys = ['epoch','seq','uptime_ms','enabled','current','age_ms','end_age_us','interval_ms','publish_end_us','angle_mdeg','reject_mask','fit_milli','dropped','faults','overwritten','resets'];
  if (keys.some(k => typeof d[k] !== 'number' || !Number.isFinite(d[k]) || !Number.isInteger(d[k]))) return false;
  return (['rms_milli','lag_milli','coherence_milli'] as const).every((k, i) => Array.isArray(d[k]) && d[k].length === (i === 0 ? 4 : 6) && d[k].every((v: unknown) => typeof v === 'number' && Number.isFinite(v)));
}
export function statistics(trial: Trial) {
  const values = trial.samples.filter(s => usable(s.data)).map(s => s.data.angle_mdeg / 1000);
  const n = values.length;
  if (!n) return { total: trial.samples.length, accepted: 0, acceptance: 0, mean: null, bias: null, mae: null, p95: null, deviation: null };
  const radians = values.map(v => v * Math.PI / 180);
  const x = radians.reduce((s, v) => s + Math.cos(v), 0) / n;
  const y = radians.reduce((s, v) => s + Math.sin(v), 0) / n;
  const r = Math.hypot(x, y);
  const mean = r < 1e-6 ? null : (Math.atan2(y, x) * 180 / Math.PI + 360) % 360;
  const errors = values.map(v => Math.abs(angleError(v, trial.expected))).sort((a,b) => a-b);
  return { total: trial.samples.length, accepted: n, acceptance: n / trial.samples.length,
    mean, bias: mean === null ? null : angleError(mean, trial.expected),
    mae: errors.reduce((a,b) => a+b,0) / n, p95: errors[Math.max(0,Math.ceil(n*.95)-1)]!,
    deviation: r < 1e-6 ? null : Math.sqrt(-2 * Math.log(Math.min(1,r))) * 180 / Math.PI };
}
export function parseSession(text: string): Session {
  const s = JSON.parse(text);
  if (s.schema !== 'mic_app.session' || s.version !== 1 || !Array.isArray(s.trials) || s.trials.length > 1000) throw new Error('Unsupported session file');
  let count = 0;
  for (const t of s.trials) {
    if (typeof t.id !== 'string' || typeof t.started !== 'string' || typeof t.mode !== 'string' || !Number.isFinite(t.expected) || t.expected < 0 || t.expected >= 360 || !Array.isArray(t.samples)) throw new Error('Invalid trial');
    for (const key of ['distance','elevation','notes']) if (typeof t[key] !== 'string') throw new Error('Invalid trial metadata');
    for (const sample of t.samples) {
      if (!Number.isFinite(sample.at) || sample.at < 0 || !validateTelemetry(sample.data)) throw new Error('Invalid telemetry sample');
      if (++count > 200000) throw new Error('Session exceeds 200,000 samples');
    }
    if (t.samples.some((v: Sample,i: number) => i && v.at < t.samples[i-1].at)) throw new Error('Unordered timestamps');
  }
  return s;
}
function quote(v: unknown) { return '"' + String(v ?? '').replaceAll('"','""') + '"'; }
export function csv(trials: Trial[]) {
  const rows: unknown[][] = [['trial_id','started','mode','expected_deg','distance','elevation','notes','elapsed_ms','epoch','seq','enabled','current','angle_deg','error_deg','reject_mask','age_ms','end_age_us','interval_ms','dropped','faults']];
  for (const t of trials) for (const s of t.samples) {
    const d=s.data, angle=usable(d)?d.angle_mdeg/1000:null;
    rows.push([t.id,t.started,t.mode,t.expected,t.distance,t.elevation,t.notes,s.at,d.epoch,d.seq,d.enabled,d.current,angle,angle===null?null:angleError(angle,t.expected),d.reject_mask,d.age_ms,d.end_age_us,d.interval_ms,d.dropped,d.faults]);
  }
  return rows.map(r => r.map(quote).join(',')).join('\r\n');
}
export function simulated(seq: number): Telemetry {
  const rejected = seq % 45 > 37;
  return { v:1,epoch:1,seq,uptime_ms:seq*171,enabled:1,current:1,age_ms:25,end_age_us:69000,interval_ms:171,publish_end_us:44000,
    angle_mdeg:rejected?-1:Math.round((45+Math.sin(seq*.13)*4)*1000),reject_mask:rejected?4:0,
    rms_milli:[430000,380000,410000,450000],lag_milli:[-2400,-5000,-200,-2400,2300,4800],
    coherence_milli:[820,790,880,810,850,800],fit_milli:120,dropped:0,faults:0,overwritten:0,resets:0 };
}
