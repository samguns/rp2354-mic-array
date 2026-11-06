"""Exclusive-mode WASAPI four-channel UAC capture, without third-party packages."""
import argparse
import ctypes as C
import json
import struct
import time
import uuid
import wave
from pathlib import Path

GUID = C.c_ubyte*16
def guid(value): return GUID.from_buffer_copy(uuid.UUID(value).bytes_le)
def checked(hr,operation):
    if hr < 0: raise RuntimeError(f"{operation}: HRESULT 0x{hr & 0xffffffff:08x}")
def call(obj,index,types,*args):
    table = C.cast(obj,C.POINTER(C.POINTER(C.c_void_p))).contents
    function = C.WINFUNCTYPE(C.c_long,C.c_void_p,*types)(table[index])
    result = function(obj,*args)
    checked(result,f"COM method {index}")
    return result
def release(obj):
    if obj: call(obj,2,[])


class Key(C.Structure):
    _fields_ = [("id",GUID),("pid",C.c_ulong)]
class Variant(C.Structure):
    _fields_ = [("vt",C.c_ushort),("reserved",C.c_ushort*3),("value",C.c_void_p),("extra",C.c_ulonglong)]


def endpoints():
    ole = C.OleDLL("ole32")
    ole.CoInitializeEx(None,0)
    ole.CoCreateInstance.argtypes = [C.POINTER(GUID),C.c_void_p,C.c_ulong,C.POINTER(GUID),C.POINTER(C.c_void_p)]
    enum = C.c_void_p()
    checked(ole.CoCreateInstance(C.byref(guid("bcde0395-e52f-467c-8e3d-c4579291692e")),None,23,
                                C.byref(guid("a95664d2-9614-4f35-a746-de8db63617e6")),C.byref(enum)),"enumerator")
    collection = C.c_void_p()
    call(enum,3,[C.c_int,C.c_ulong,C.POINTER(C.c_void_p)],1,1,C.byref(collection))
    count = C.c_uint(); call(collection,3,[C.POINTER(C.c_uint)],C.byref(count))
    result = []
    for i in range(count.value):
        device,store = C.c_void_p(),C.c_void_p()
        call(collection,4,[C.c_uint,C.POINTER(C.c_void_p)],i,C.byref(device))
        call(device,4,[C.c_ulong,C.POINTER(C.c_void_p)],0,C.byref(store))
        key = Key(guid("a45c254e-df1c-4efd-8020-67d146a850e0"),14);value = Variant()
        call(store,5,[C.POINTER(Key),C.POINTER(Variant)],C.byref(key),C.byref(value))
        name = C.wstring_at(value.value) if value.vt == 31 else "Unknown"
        ole.PropVariantClear(C.byref(value));release(store)
        result.append((name,device))
    release(collection);release(enum)
    return result


def capture(output,seconds,rate=48000):
    choices = endpoints()
    matching = [(n,d) for n,d in choices if "Pico" in n]
    if len(matching)!=1: raise RuntimeError(f"Expected one Pico audio endpoint: {[n for n,d in choices]}")
    name,device = matching[0]
    client,capture_client = C.c_void_p(),C.c_void_p()
    call(device,3,[C.POINTER(GUID),C.c_ulong,C.c_void_p,C.POINTER(C.c_void_p)],
         C.byref(guid("1cb9ad4c-dbfa-4c32-b178-c2f568a703b2")),23,None,C.byref(client))
    mix = C.c_void_p();call(client,8,[C.POINTER(C.c_void_p)],C.byref(mix))
    mix_header = struct.unpack("<HHIIHHH",C.string_at(mix,18))
    print('Windows mix format:',mix_header,flush=True)
    C.OleDLL('ole32').CoTaskMemFree(mix)
    fmt = C.create_string_buffer(struct.pack("<HHIIHHHHI",0xfffe,4,rate,rate*8,8,16,22,16,0x33)
                                 + bytes.fromhex("0100000000001000800000aa00389b71"))
    call(client,7,[C.c_int,C.c_void_p,C.c_void_p],1,fmt,None)
    default,minimum = C.c_longlong(),C.c_longlong()
    call(client,9,[C.POINTER(C.c_longlong),C.POINTER(C.c_longlong)],C.byref(default),C.byref(minimum))
    period = default.value
    call(client,3,[C.c_int,C.c_ulong,C.c_longlong,C.c_longlong,C.c_void_p,C.c_void_p],1,0,max(period*4,5000000),period,fmt,None)
    buffer_frames = C.c_uint();call(client,4,[C.POINTER(C.c_uint)],C.byref(buffer_frames))
    call(client,14,[C.POINTER(GUID),C.POINTER(C.c_void_p)],
         C.byref(guid("c8adbd64-e71e-48a0-a4de-185c395cd317")),C.byref(capture_client))
    output.parent.mkdir(parents=True,exist_ok=True)
    partial = output.with_suffix('.partial.wav');frames = 0;flags_count = {};start = time.monotonic()
    first_packet = True;next_position = None;position_gaps = []
    try:
        call(client,10,[])
        with wave.open(str(partial),'wb') as wav:
            wav.setparams((4,2,rate,0,'NONE','not compressed'))
            deadline = time.monotonic()+10
            while frames<seconds*rate:
                available = C.c_uint()
                call(capture_client,5,[C.POINTER(C.c_uint)],C.byref(available))
                if not available.value:
                    if time.monotonic()>deadline: raise TimeoutError('WASAPI capture stalled')
                    time.sleep(.002);continue
                data = C.c_void_p();count,flags = C.c_uint(),C.c_ulong()
                position,timestamp = C.c_ulonglong(),C.c_ulonglong()
                call(capture_client,3,[C.POINTER(C.c_void_p),C.POINTER(C.c_uint),C.POINTER(C.c_ulong),
                                      C.POINTER(C.c_ulonglong),C.POINTER(C.c_ulonglong)],
                     C.byref(data),C.byref(count),C.byref(flags),C.byref(position),C.byref(timestamp))
                take = min(count.value,seconds*rate-frames)
                if next_position is not None and position.value != next_position:
                    position_gaps.append({'output_frame':frames,'expected':next_position,'actual':position.value})
                next_position = position.value+count.value
                wav.writeframesraw(bytes(take*8) if flags.value & 2 else C.string_at(data,take*8))
                if flags.value:
                    label = ('initial:' if first_packet else '')+str(flags.value)
                    flags_count[label] = flags_count.get(label,0)+1
                first_packet = False
                call(capture_client,4,[C.c_uint],count.value)
                frames += take;deadline = time.monotonic()+10
                if frames%(rate*10) == 0:print(f'Recorded {frames//rate} s',flush=True)
    finally:
        call(client,11,[]);release(capture_client);release(client)
        for _,d in choices:release(d)
    partial.replace(output)
    report = {'device':name,'api':'WASAPI exclusive','rate':rate,'channels':4,'frames':frames,
              'wall_seconds':time.monotonic()-start,'packet_flags':flags_count,
              'device_position_gaps':position_gaps,'buffer_frames':buffer_frames.value,
              'windows_mix_format':mix_header}
    output.with_suffix('.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path,nargs='?')
    parser.add_argument('--seconds',type=int,default=60)
    parser.add_argument('--rate',type=int,choices=[16000,48000],default=48000)
    parser.add_argument('--list',action='store_true')
    args = parser.parse_args()
    if args.list:
        for name,dev in endpoints(): print(name);release(dev)
    elif args.output and 1<=args.seconds<=3600:capture(args.output,args.seconds,args.rate)
    else:parser.error('Specify output and --seconds 1..3600, or --list')
