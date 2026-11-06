mod serial_service;
use serial_service as transport;
use std::sync::{Mutex,atomic::{AtomicU64,Ordering}};
use tauri::{Emitter, Manager, State};
use tauri_plugin_dialog::DialogExt;
#[derive(Default)]
struct Connection(Mutex<Option<transport::Session>>,AtomicU64);
#[tauri::command]
fn ports() -> Result<Vec<serde_json::Value>, String> {
    Ok(serialport::available_ports().map_err(|e| e.to_string())?.into_iter().map(|p| {
        let (label, telemetry) = match p.port_type {
            serialport::SerialPortType::UsbPort(u) if u.vid==0x2fe3 && u.pid==6 => {
                let telemetry=matches!(u.interface,Some(2|3));
                (format!("{} - {}",p.port_name,if telemetry {"Mic Array Telemetry"}else{"Diagnostics shell"}),telemetry)
            },
            serialport::SerialPortType::UsbPort(u) => (format!("{} - {}",p.port_name,u.product.unwrap_or_default()),false),
            _ => (p.port_name.clone(),false),
        };
        serde_json::json!({"name":p.port_name,"label":label,"telemetry":telemetry})
    }).collect())
}
fn stop(state: &Connection) {
    state.1.fetch_add(1,Ordering::SeqCst);
    if let Some(session) = state.0.lock().unwrap().take() { session.stop(); }
}
#[tauri::command]
fn disconnect(state: State<Connection>) { stop(&state); }
#[tauri::command]
async fn connect(app: tauri::AppHandle, state: State<'_,Connection>, port: String) -> Result<(), String> {
    stop(&state);
    let generation=state.1.load(Ordering::SeqCst);
    let session = tauri::async_runtime::spawn_blocking(move || transport::start(&port, move |event, value| { let _ = app.emit(event, value); })).await.map_err(|e|e.to_string())??;
    let mut guard=state.0.lock().unwrap();
    if state.1.load(Ordering::SeqCst)!=generation {
        drop(guard);session.stop();return Err("Connection cancelled".into());
    }
    *guard = Some(session);
    Ok(())
}
#[tauri::command]
fn estimation(state: State<Connection>, enabled: bool) -> Result<(), String> {
    let guard = state.0.lock().unwrap();
    guard.as_ref().ok_or("Not connected")?.tx.send(transport::Request::Estimation(enabled)).map_err(|e|e.to_string())
}
#[tauri::command]
fn beam(state:State<Connection>,mode:u32,azimuth:u32,elevation:u32)->Result<(),String>{
    if mode>2 || azimuth>=3600 || elevation>900{return Err("Invalid beam direction".into());}
    state.0.lock().unwrap().as_ref().ok_or("Not connected")?.tx.send(transport::Request::Beam{mode,azimuth,elevation}).map_err(|e|e.to_string())
}
#[tauri::command]
fn learn_noise(state:State<Connection>)->Result<(),String>{
    state.0.lock().unwrap().as_ref().ok_or("Not connected")?.tx.send(transport::Request::Learn).map_err(|e|e.to_string())
}
#[tauri::command]
fn noise(state:State<Connection>,mode:u32,hum:u32,channel:u32)->Result<(),String>{
 if mode>2 || ![0,50,60].contains(&hum) || !(1..=4).contains(&channel){return Err("Invalid noise settings".into());}
 state.0.lock().unwrap().as_ref().ok_or("Not connected")?.tx.send(transport::Request::Noise{mode,hum,channel}).map_err(|e|e.to_string())
}
#[tauri::command]
fn save_settings(state:State<Connection>)->Result<(),String>{
 state.0.lock().unwrap().as_ref().ok_or("Not connected")?.tx.send(transport::Request::SaveSettings).map_err(|e|e.to_string())
}
#[tauri::command]
async fn export_report(app: tauri::AppHandle, name: String, content: String) -> Result<bool,String> {
    tauri::async_runtime::spawn_blocking(move || {
        let file = app.dialog().file().set_file_name(&name).blocking_save_file();
        if let Some(file) = file {
            let path = file.into_path().map_err(|e| e.to_string())?;
            std::fs::write(path, content).map_err(|e|e.to_string())?;
            Ok(true)
        } else { Ok(false) }
    }).await.map_err(|e|e.to_string())?
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .manage(Connection::default())
        .plugin(tauri_plugin_dialog::init())
        .invoke_handler(tauri::generate_handler![ports,connect,disconnect,estimation,beam,learn_noise,noise,save_settings,export_report])
        .on_window_event(|window,event| {
            if matches!(event,tauri::WindowEvent::Destroyed) { stop(&window.state::<Connection>()); }
        })
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
