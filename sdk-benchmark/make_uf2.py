"""Wrap the RP2350 Arm Secure benchmark binary in UF2 blocks."""
import struct
import sys
from pathlib import Path

data = Path(sys.argv[1]).read_bytes()
if not 0 < len(data) < 4*1024*1024:
    raise ValueError('Invalid benchmark image length')
count = (len(data)+255)//256
result = bytearray()
for i in range(count):
    result += (struct.pack('<8I',0x0A324655,0x9E5D5157,0x2000,
                           0x10000000+i*256,256,i,count,0xe48bff57)
               +data[i*256:(i+1)*256].ljust(256,b'\0')+bytes(220)
               +struct.pack('<I',0x0AB16F30))
Path(sys.argv[2]).write_bytes(result)
