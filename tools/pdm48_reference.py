"""Independent bitwise CIC4 and direct FIR model for 48 kHz diagnostics."""
import math,re
from pathlib import Path

def coefficients():
 text=(Path(__file__).resolve().parents[1]/'src/pdm_coefficients.h').read_text(encoding='utf-8')
 arrays=[]
 for name in ['pdm_halfband','pdm_final','pdm_comp']:
  body=re.search(name+r'\[\d+\] = \{(.*?)\};',text,re.S).group(1)
  arrays.append([int(x) for x in re.findall(r'-?\d+',body)])
 return arrays

def quantize(value,scale):
 rounded=(value+scale//2)//scale if value>=0 else -((-value+scale//2)//scale)
 return max(-32768,min(32767,rounded))

def decode(words):
 half,final,comp=coefficients();integ=[[0]*4 for _ in range(4)];comb=[[0]*4 for _ in range(4)]
 histories=[[[0]*len(half),[0]*len(final),[0]*3] for _ in range(4)]
 out=[];tick=phase=0
 for word in words:
  for bit in range(8):
   for c in range(4):
    i=integ[c];i[0]+=1 if word&(1<<(4*bit+c)) else -1
    for k in range(1,4):i[k]+=i[k-1]
   tick+=1
   if tick%16:continue
   for c in range(4):
    value=integ[c][3]
    for k in range(4):old=value;value-=comb[c][k];comb[c][k]=old
    h=histories[c][0];h.pop(0);h.append(quantize(value,2))
    if not phase&1:continue
    value=quantize(sum(x*y for x,y in zip(h,half)),16384);h=histories[c][1];h.pop(0);h.append(value)
    if phase!=3:continue
    value=quantize(sum(x*y for x,y in zip(h,final)),16384);h=histories[c][2];h.pop(0);h.append(value)
    out.append(quantize(sum(x*y for x,y in zip(h,comp)),16384))
   phase=(phase+1)%4
 return out

def response(frequency):
 half,final,comp=coefficients()
 def fir(taps,f,rate):
  return abs(sum(v*complex(math.cos(-2*math.pi*f*n/rate),math.sin(-2*math.pi*f*n/rate)) for n,v in enumerate([x/16384 for x in taps])))
 cic=1 if frequency==0 else abs(math.sin(math.pi*frequency/192000)/(16*math.sin(math.pi*frequency/3072000)))**4
 return cic*fir(half,frequency,192000)*fir(final,frequency,96000)*fir(comp,frequency,48000)
