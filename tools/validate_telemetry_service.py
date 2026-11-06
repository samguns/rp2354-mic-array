"""Hardware checks for MIC/2 controls, lease expiry and fresh-session reconnect."""
import argparse
import json
import time
from pathlib import Path
import serial


def validate(port_name, output):
    records=[]
    with serial.Serial(port_name,115200,timeout=.05) as port:
        buffer=bytearray()
        nonce=(time.time_ns() & 0xffffffff) or 1
        def send(token, ident, command):
            port.write(f'MIC/2 {token} {ident} {command}\n'.encode())
        def receive(timeout):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if b'\n' in buffer:
                    line,_,tail=buffer.partition(b'\n');buffer[:]=tail
                    try: value=json.loads(line)
                    except (ValueError,UnicodeDecodeError): continue
                    records.append(value)
                    return value
                buffer.extend(port.read(port.in_waiting or 1))
            return None
        def wait(predicate, timeout=3):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                value=receive(deadline-time.monotonic())
                if value is not None and predicate(value):return value
            raise AssertionError('Expected service response did not arrive')
        def ack(token, ident):
            return wait(lambda d:d.get('session')==token and d.get('id')==ident and d.get('type')=='ack')
        port.write(b'\n');send(nonce,1,'SUBSCRIBE')
        hello=wait(lambda d:d.get('type')=='hello' and d.get('session')==nonce)
        assert hello['service']=='mic_array' and hello['v']==2
        send(nonce,2,'DOA 0');assert ack(nonce,2)['enabled']==0
        wait(lambda d:d.get('type')=='telemetry' and d['session']==nonce and d['enabled']==0)
        start=time.monotonic();last_frame=0;heartbeats=0
        while time.monotonic()-start<6:
            packet=receive(.15)
            if packet and packet.get('type')=='telemetry' and packet['session']==nonce:
                last_frame=time.monotonic()-start;heartbeats+=1
        assert heartbeats>=2 and last_frame<4.5,(heartbeats,last_frame)
        # New session must recover after the old lease expired, even with DTR high.
        fresh=(nonce+1) & 0xffffffff or 1
        send(fresh,1,'SUBSCRIBE');wait(lambda d:d.get('type')=='hello' and d.get('session')==fresh)
        # A stale session cannot change estimator state.
        send(nonce,3,'DOA 1');send(fresh,2,'PING')
        wait(lambda d:d.get('type')=='pong' and d.get('session')==fresh and d.get('id')==2)
        status=wait(lambda d:d.get('type')=='telemetry' and d['session']==fresh)
        assert status['enabled']==0
        send(fresh,3,'DOA 1');assert ack(fresh,3)['enabled']==1
        current=wait(lambda d:d.get('type')=='telemetry' and d['session']==fresh and d['enabled']==1 and d['current']==1)
        assert current['dropped']==current['faults']==current['tx_skipped']==current['rx_errors']==0
        send(fresh,4,'UNSUBSCRIBE');ack(fresh,4)
        stop=time.monotonic()
        while time.monotonic()-stop<1.5:
            packet=receive(.1)
            if packet and packet.get('type')=='telemetry':assert time.monotonic()-stop<.5
    report={'result':'PASS','off_heartbeats':heartbeats,'last_frame_before_lease_expiry_seconds':last_frame,
            'checks':['hello','acknowledged off/on','off-state heartbeat','lease expiry','fresh session',
                      'stale session rejected','unsubscribe'],'packets':records}
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='packets'},indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--output',type=Path,default=Path('build-tests/cdc-service/protocol.json'))
    args=parser.parse_args();validate(args.port,args.output)
