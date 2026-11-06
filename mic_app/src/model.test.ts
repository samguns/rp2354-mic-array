import { describe, it, expect } from 'vitest';
import { angleError, statistics, simulated, parseSession, usable, csv, type Trial } from './model';
const trial = (angles: number[]): Trial => ({ id:'test',started:'now',mode:'simulation',expected:0,distance:'30 cm',elevation:'0',notes:'a,"b',samples:angles.map((a,i)=>({at:i*171,data:{...simulated(i),angle_mdeg:a*1000}})) });
describe('validation math and persistence',()=>{
  it('wraps signed errors',()=>{ expect(angleError(359,1)).toBe(-2); expect(angleError(1,359)).toBe(2); });
  it('uses circular rather than arithmetic means',()=>{ const s=statistics(trial([359,1]));expect(Math.abs(s.bias!)).toBeLessThan(.001);expect(s.mae).toBe(1);expect(s.deviation).toBeCloseTo(1,2); });
  it('excludes stale, disabled, old-epoch and rejected results',()=>{
    for(const change of [{age_ms:900},{enabled:0},{current:0},{reject_mask:4}]) expect(usable({...simulated(1),...change})).toBe(false);
    const t=trial([10]);t.samples[0]!.data.reject_mask=4;expect(statistics(t).mean).toBeNull();
  });
  it('round trips and rejects malformed sessions',()=>{
    const s={schema:'mic_app.session',version:1,trials:[trial([10,20])]};expect(parseSession(JSON.stringify(s)).trials.length).toBe(1);
    expect(()=>parseSession(JSON.stringify({...s,version:2}))).toThrow();
    s.trials[0]!.samples[0]!.data.rms_milli=[];expect(()=>parseSession(JSON.stringify(s))).toThrow();
  });
  it('quotes CSV metadata',()=>{expect(csv([trial([359])])).toContain('"a,""b"');});
});
