use serde_json::{json, Value};
use std::{collections::VecDeque,sync::mpsc::{self,Sender},thread,time::{Duration,Instant,SystemTime,UNIX_EPOCH}};

pub enum Request { Stop, Estimation(bool), Beam{mode:u32,azimuth:u32,elevation:u32}, Learn, Noise{mode:u32,hum:u32,channel:u32},SaveSettings }
pub struct Session { pub tx:Sender<Request>, thread:thread::JoinHandle<()> }
impl Session {
    pub fn stop(self) { let _=self.tx.send(Request::Stop);let _=self.thread.join(); }
}

#[derive(Default)]
struct Decoder { bytes:Vec<u8>, overflow:bool }
impl Decoder {
    fn push(&mut self,byte:u8)->Option<Value> {
        if byte==b'\n' {
            let value=if self.overflow {None}else{serde_json::from_slice::<Value>(&self.bytes).ok()};
            self.bytes.clear();self.overflow=false;
            return value.filter(|v|v["v"]==2);
        }
        if !self.overflow {
            if self.bytes.len()<2048 {self.bytes.push(byte);}else{self.overflow=true;}
        }
        None
    }
}

// Keep the recorded-session schema compatible with existing mic_app sessions.
// protocol_v preserves provenance; v=1 is the internal measurement schema.
fn measurement(mut value:Value)->Option<Value> {
    if value["v"]!=2 || value["type"]!="telemetry" {return None;}
    for key in ["seq","epoch","uptime_ms","enabled","current","age_ms","end_age_us","interval_ms","publish_end_us","reject_mask","fit_milli","dropped","faults","overwritten","resets","tx_skipped","rx_errors"] {
        value[key].as_u64()?;
    }
    value["angle_mdeg"].as_i64()?;
    for (key,count) in [("rms_milli",4),("lag_milli",6),("coherence_milli",6)] {
        let a=value[key].as_array()?;
        if a.len()!=count || a.iter().any(|v|v.as_i64().is_none()){return None;}
    }
    value["protocol_v"]=json!(2);value["v"]=json!(1);Some(value)
}

pub fn start(port:&str,emit:impl Fn(&str,Value)+Send+'static)->Result<Session,String> {
    let mut serial=serialport::new(port,115200).timeout(Duration::from_millis(20)).open().map_err(|e|e.to_string())?;
    serial.write_data_terminal_ready(true).map_err(|e|e.to_string())?;
    serial.clear(serialport::ClearBuffer::Input).map_err(|e|e.to_string())?;
    let nonce=(SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos() as u32).max(1);
    let (tx,rx)=mpsc::channel();let (ready_tx,ready_rx)=mpsc::channel();
    let thread=thread::spawn(move || {
        let result=(||->Result<(),String>{
            serial.write_all(format!("\nMIC/2 {nonce} 1 SUBSCRIBE\n").as_bytes()).map_err(|e|e.to_string())?;
            let mut decoder=Decoder::default();let mut buffer=[0u8;1024];
            let mut hello=false;let mut last=Instant::now();let started=Instant::now();
            let mut ping=Instant::now();let mut id=1u32;
            let mut requested=VecDeque::new();let mut pending:Option<(u32,Instant)>=None;
            loop {
                while let Ok(request)=rx.try_recv(){
                    match request {
                        Request::Stop=>return Ok(()),
                        request=>{if requested.len()>=16{return Err("Too many queued controls".into());}requested.push_back(request);}
                    }
                }
                if hello && pending.is_none() {
                    if let Some(request)=requested.pop_front() {
                        id=id.wrapping_add(1);
                        let command=match request {
                            Request::Estimation(on)=>format!("DOA {}",u8::from(on)),
                            Request::Beam{mode,azimuth,elevation}=>format!("BEAM {mode} {azimuth} {elevation}"),
                            Request::Noise{mode,hum,channel}=>format!("NR_CONFIG {mode} {hum} {channel}"),
                            Request::SaveSettings=>"SETTINGS_SAVE".into(),
                            Request::Learn=>"NR_LEARN".into(),Request::Stop=>unreachable!(),
                        };
                        serial.write_all(format!("MIC/2 {nonce} {id} {command}\n").as_bytes()).map_err(|e|e.to_string())?;
                        pending=Some((id,Instant::now()));
                    }
                }
                if hello && ping.elapsed()>=Duration::from_secs(1) {
                    id=id.wrapping_add(1);
                    serial.write_all(format!("MIC/2 {nonce} {id} PING\n").as_bytes()).map_err(|e|e.to_string())?;
                    ping=Instant::now();
                }
                match serial.read(&mut buffer) {
                    Ok(n)=>for &byte in &buffer[..n] {
                        let Some(value)=decoder.push(byte) else{continue};
                        if value["session"]!=nonce {continue;}
                        match value["type"].as_str() {
                            Some("hello") if value["service"]=="mic_array" && value["id"]==1 && value["rate"]==48000 && value["channels"]==4 => {
                                hello=true;last=Instant::now();let _=ready_tx.send(Ok(()));
                            },
                            Some("telemetry") if hello => if let Some(value)=measurement(value) {last=Instant::now();emit("telemetry",value);},
                            Some("pong") if hello => last=Instant::now(),
                            Some("ack") if hello => {
                                last=Instant::now();
                                if pending.is_some_and(|(id,_)|value["id"]==id){pending=None;
                                    if value["ok"]==0 {emit("control-error",json!("Control rejected. Close audio recording before changing or saving settings; learning requires suppression enabled."));}
                                    else {emit("control-ack",value);}}
                            },
                            _=>{},
                        }
                    },
                    Err(e) if matches!(e.kind(),std::io::ErrorKind::TimedOut|std::io::ErrorKind::WouldBlock)=>{},
                    Err(e)=>return Err(e.to_string()),
                }
                if !hello && started.elapsed()>Duration::from_secs(2) {
                    return Err("No MIC/2 service handshake. Select Mic Array Telemetry, not the diagnostics shell, and check firmware.".into());
                }
                if hello && last.elapsed()>Duration::from_secs(4) {return Err("Telemetry connection timed out".into());}
                if pending.is_some_and(|(_,time)|time.elapsed()>Duration::from_secs(2)){return Err("Estimator control acknowledgement timed out".into());}
            }
        })();
        let _=serial.write_all(format!("MIC/2 {nonce} 0 UNSUBSCRIBE\n").as_bytes());
        // Allow the bounded control response to drain before dropping DTR.
        thread::sleep(Duration::from_millis(40));let _=serial.write_data_terminal_ready(false);
        if let Err(error)=result {let _=ready_tx.send(Err(error.clone()));emit("connection-error",json!(error));}
    });
    let session=Session{tx,thread};
    match ready_rx.recv_timeout(Duration::from_secs(3)) {
        Ok(Ok(()))=>Ok(session),
        Ok(Err(error))=>{session.stop();Err(error)},
        Err(_)=>{session.stop();Err("Telemetry service handshake timed out".into())},
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test] fn framing_and_resynchronization(){
        let mut d=Decoder::default();
        for b in b"{\"v\":2,\"session\":5," {assert!(d.push(*b).is_none());}
        let mut result=None;for b in b"\"type\":\"hello\"}\n" {if let Some(v)=d.push(*b){result=Some(v);}}
        assert_eq!(result.unwrap()["session"],5);
        for b in vec![b'x';3000] {assert!(d.push(b).is_none());}
        assert!(d.push(b'\n').is_none());
        for b in b"{\"v\":1}\n" {assert!(d.push(*b).is_none());}
        let mut result=None;for b in b"{\"v\":2}\n"{if let Some(v)=d.push(*b){result=Some(v);}}
        assert!(result.is_some());
    }
    #[test] fn incomplete_measurements_are_rejected(){
        assert!(measurement(json!({"v":2,"type":"telemetry"})).is_none());
        assert!(measurement(json!({"v":1,"type":"telemetry"})).is_none());
    }
    #[test]
    #[ignore="Connected telemetry CDC required; MIC_APP_TEST_PORT"]
    fn live_device(){
        let port=std::env::var("MIC_APP_TEST_PORT").expect("Set MIC_APP_TEST_PORT");
        let (tx,rx)=mpsc::channel();
        let session=start(&port,move|event,value|{let _=tx.send((event.to_string(),value));}).unwrap();
        session.tx.send(Request::Estimation(true)).unwrap();
        let seconds=std::env::var("MIC_APP_TEST_SECONDS").ok().and_then(|v|v.parse::<u64>().ok()).unwrap_or(60);
        let end=Instant::now()+Duration::from_secs(seconds);let mut records=Vec::new();let mut errors=Vec::new();let mut acks=0;
        while Instant::now()<end {
            if let Ok((event,value))=rx.recv_timeout(Duration::from_millis(500)){
                match event.as_str(){"telemetry"=>records.push(value),"connection-error"=>errors.push(value),"control-ack"=>acks+=1,_=>{}}
            }
        }
        session.stop();
        if let Ok(path)=std::env::var("MIC_APP_TEST_OUTPUT"){
            std::fs::write(path,serde_json::to_string_pretty(&json!({"records":records,"errors":errors,"acks":acks})).unwrap()).unwrap();
        }
        assert!(errors.is_empty(),"{errors:?}");assert_eq!(acks,1);
        let unique=records.windows(2).filter(|p|p[0]["seq"]!=p[1]["seq"]).count();
        let manual=records.last().is_some_and(|v|v["bf_mode"]==2 && v["bf_elevation_tdeg"]!=900);
        let minimum=if manual {2}else{5};
        assert!(unique>seconds as usize*minimum,"Only {unique} new results (minimum {minimum} Hz)");
        let last=records.last().unwrap();
        for key in ["dropped","faults","overwritten","tx_skipped","rx_errors"]{assert_eq!(last[key],0,"{key}");}
        eprintln!("Dedicated CDC passed: {} packets, {unique} new results, {acks} control ack",records.len());
    }
}
