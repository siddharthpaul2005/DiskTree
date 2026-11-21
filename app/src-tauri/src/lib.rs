// DiskTree desktop shell.
//
// The C++ engine ships next to this executable as `disktree-engine.exe`.
// On start we launch it as `serve --port 0 --token <random> --parent-pid <us>`,
// read the `{"event":"ready","port":N}` line it prints, and hand the port and
// token to the web UI through the `engine_info` command. The engine binds to
// 127.0.0.1 only, refuses requests without the token, and exits by itself if
// this process dies.
//
// "Restart as administrator" starts an elevated copy through the Windows
// permission prompt with `--wait-pid <us> [--rescan <path>]`, then exits.
// The copy waits for this process to be gone (so the single-instance check
// doesn't hand off to it), and the UI asks for the path to rescan.

use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::process::{Child, Command, Stdio};
use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;

use serde::Serialize;
use tauri::menu::{Menu, MenuItem};
use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use tauri::{AppHandle, Emitter, Manager, RunEvent, State};

#[derive(Clone, Serialize, Debug)]
pub struct EngineInfo {
    port: u16,
    token: String,
    pid: u32,
}

#[derive(Default)]
enum EngineState {
    #[default]
    Starting,
    Ready(EngineInfo),
    Failed(String),
}

#[derive(Default)]
struct Engine {
    state: Mutex<EngineState>,
    changed: Condvar,
    child: Mutex<Option<Child>>,
}

type SharedEngine = Arc<Engine>;

/// Folder to rescan right after a restart as administrator; the UI takes it once.
#[derive(Default)]
struct StartupScan(Mutex<Option<String>>);

#[derive(Default)]
struct LaunchArgs {
    wait_pid: Option<u32>,
    rescan: Option<String>,
}

fn parse_args() -> LaunchArgs {
    let mut out = LaunchArgs::default();
    let mut args = std::env::args().skip(1);
    while let Some(a) = args.next() {
        match a.as_str() {
            "--wait-pid" => out.wait_pid = args.next().and_then(|v| v.parse().ok()),
            "--rescan" => out.rescan = args.next().filter(|v| !v.is_empty()),
            _ => {}
        }
    }
    out
}

#[cfg(windows)]
mod win {
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Foundation::{CloseHandle, GetLastError, ERROR_CANCELLED};
    use windows_sys::Win32::System::Threading::{OpenProcess, WaitForSingleObject, PROCESS_SYNCHRONIZE};
    use windows_sys::Win32::UI::Shell::{ShellExecuteExW, SEE_MASK_NOCLOSEPROCESS, SHELLEXECUTEINFOW};
    use windows_sys::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL;

    fn wide(s: &OsStr) -> Vec<u16> {
        s.encode_wide().chain(Some(0)).collect()
    }

    /// Starts `exe` elevated; Windows shows its permission prompt first.
    /// Err("cancelled") when the user says no.
    pub fn run_as_admin(exe: &std::path::Path, params: &str) -> Result<(), String> {
        let verb = wide(OsStr::new("runas"));
        let file = wide(exe.as_os_str());
        let args = wide(OsStr::new(params));
        // SAFETY: a zeroed SHELLEXECUTEINFOW is valid; the strings outlive the call
        let mut info: SHELLEXECUTEINFOW = unsafe { std::mem::zeroed() };
        info.cbSize = std::mem::size_of::<SHELLEXECUTEINFOW>() as u32;
        info.fMask = SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = verb.as_ptr();
        info.lpFile = file.as_ptr();
        info.lpParameters = args.as_ptr();
        info.nShow = SW_SHOWNORMAL as i32;
        if unsafe { ShellExecuteExW(&mut info) } == 0 {
            let err = unsafe { GetLastError() };
            return Err(if err == ERROR_CANCELLED {
                "cancelled".into()
            } else {
                format!("Windows could not restart DiskTree (error {err})")
            });
        }
        if !info.hProcess.is_null() {
            unsafe { CloseHandle(info.hProcess) };
        }
        Ok(())
    }

    /// Waits up to `ms` for process `pid` to exit (returns at once if it already has).
    pub fn wait_for_exit(pid: u32, ms: u32) {
        unsafe {
            let h = OpenProcess(PROCESS_SYNCHRONIZE, 0, pid);
            if !h.is_null() {
                WaitForSingleObject(h, ms);
                CloseHandle(h);
            }
        }
    }
}

fn random_token() -> String {
    let mut buf = [0u8; 24];
    getrandom::fill(&mut buf).expect("OS random generator unavailable");
    buf.iter().map(|b| format!("{b:02x}")).collect()
}

fn engine_path() -> Result<PathBuf, String> {
    let exe = std::env::current_exe().map_err(|e| e.to_string())?;
    let dir = exe.parent().ok_or("no exe directory")?;
    let name = if cfg!(windows) { "disktree-engine.exe" } else { "disktree-engine" };
    let p = dir.join(name);
    if p.exists() {
        Ok(p)
    } else {
        Err(format!("Engine not found at {}", p.display()))
    }
}

fn set_state(engine: &Engine, s: EngineState) {
    *engine.state.lock().unwrap() = s;
    engine.changed.notify_all();
}

fn start_engine(app: &AppHandle, engine: SharedEngine) {
    let path = match engine_path() {
        Ok(p) => p,
        Err(e) => {
            set_state(&engine, EngineState::Failed(e.clone()));
            let _ = app.emit("engine-failed", e);
            return;
        }
    };
    let token = random_token();
    let mut cmd = Command::new(&path);
    cmd.args(["serve", "--port", "0", "--token", &token, "--parent-pid"])
        .arg(std::process::id().to_string())
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        const CREATE_NO_WINDOW: u32 = 0x0800_0000;
        cmd.creation_flags(CREATE_NO_WINDOW);
    }

    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(e) => {
            let msg = format!("Could not start the engine: {e}");
            set_state(&engine, EngineState::Failed(msg.clone()));
            let _ = app.emit("engine-failed", msg);
            return;
        }
    };
    let stdout = child.stdout.take();
    let stderr = child.stderr.take();
    *engine.child.lock().unwrap() = Some(child);

    if let Some(err) = stderr {
        std::thread::spawn(move || {
            for line in BufReader::new(err).lines().map_while(Result::ok) {
                eprintln!("[engine] {line}");
            }
        });
    }

    let app = app.clone();
    std::thread::spawn(move || {
        let mut announced = false;
        if let Some(out) = stdout {
            for line in BufReader::new(out).lines().map_while(Result::ok) {
                if announced {
                    continue;
                }
                let Ok(v) = serde_json::from_str::<serde_json::Value>(&line) else { continue };
                if v["event"] == "ready" {
                    let info = EngineInfo {
                        port: v["port"].as_u64().unwrap_or(0) as u16,
                        token: token.clone(),
                        pid: v["pid"].as_u64().unwrap_or(0) as u32,
                    };
                    set_state(&engine, EngineState::Ready(info.clone()));
                    let _ = app.emit("engine-ready", info);
                    announced = true;
                }
            }
        }
        // stdout closed: the engine has exited
        let msg = "The DiskTree engine stopped unexpectedly".to_string();
        set_state(&engine, EngineState::Failed(msg.clone()));
        let _ = app.emit("engine-failed", msg);
    });
}

fn stop_engine(engine: &Engine) {
    if let Some(mut child) = engine.child.lock().unwrap().take() {
        let _ = child.kill();
        let _ = child.wait();
    }
}

/// Port and token of the running engine; waits up to 15 s for start-up.
#[tauri::command]
fn engine_info(engine: State<'_, SharedEngine>) -> Result<EngineInfo, String> {
    let guard = engine.state.lock().unwrap();
    let (guard, _) = engine
        .changed
        .wait_timeout_while(guard, Duration::from_secs(15), |s| matches!(s, EngineState::Starting))
        .unwrap();
    match &*guard {
        EngineState::Ready(i) => Ok(i.clone()),
        EngineState::Failed(e) => Err(e.clone()),
        EngineState::Starting => Err("The engine did not start within 15 seconds".into()),
    }
}

/// Starts a fresh engine after a failure.
#[tauri::command]
fn restart_engine(app: AppHandle, engine: State<'_, SharedEngine>) {
    stop_engine(&engine);
    set_state(&engine, EngineState::Starting);
    start_engine(&app, engine.inner().clone());
}

/// "mica" on Windows 11 (the window is translucent), otherwise "opaque".
#[tauri::command]
fn glass_mode() -> &'static str {
    if mica_supported() { "mica" } else { "opaque" }
}

/// Restarts DiskTree with administrator rights so scans can read every
/// folder. `rescan` is scanned again as soon as the new window is up.
#[tauri::command]
fn restart_as_admin(app: AppHandle, rescan: Option<String>) -> Result<(), String> {
    #[cfg(windows)]
    {
        let exe = std::env::current_exe().map_err(|e| e.to_string())?;
        let mut params = format!("--wait-pid {}", std::process::id());
        // Windows paths can't contain quotes, so quoting is enough
        if let Some(p) = rescan.filter(|p| !p.contains('"')) {
            // A trailing backslash would escape the closing quote: "C:\" -> C:"
            let p = if p.ends_with('\\') { format!("{p}\\") } else { p };
            params.push_str(&format!(" --rescan \"{p}\""));
        }
        win::run_as_admin(&exe, &params)?;
        app.exit(0);
        Ok(())
    }
    #[cfg(not(windows))]
    {
        let _ = (app, rescan);
        Err("Restarting as administrator is only available on Windows".into())
    }
}

/// The folder to rescan after a restart as administrator, once.
#[tauri::command]
fn take_startup_scan(scan: State<'_, StartupScan>) -> Option<String> {
    scan.0.lock().unwrap().take()
}

#[tauri::command]
fn app_version(app: AppHandle) -> String {
    app.package_info().version.to_string()
}

#[cfg(windows)]
fn mica_supported() -> bool {
    windows_version::OsVersion::current().build >= 22000
}
#[cfg(not(windows))]
fn mica_supported() -> bool {
    false
}

fn show_main(app: &AppHandle) {
    if let Some(w) = app.get_webview_window("main") {
        let _ = w.unminimize();
        let _ = w.show();
        let _ = w.set_focus();
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let engine: SharedEngine = Arc::new(Engine::default());

    let launch = parse_args();
    // Started by "Restart as administrator": let the old window close first,
    // or the single-instance check would hand this launch back to it
    #[cfg(windows)]
    if let Some(pid) = launch.wait_pid {
        win::wait_for_exit(pid, 15_000);
    }

    let app = tauri::Builder::default()
        .plugin(tauri_plugin_single_instance::init(|app, _args, _cwd| show_main(app)))
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .manage(engine.clone())
        .manage(StartupScan(Mutex::new(launch.rescan)))
        .invoke_handler(tauri::generate_handler![
            engine_info,
            restart_engine,
            restart_as_admin,
            take_startup_scan,
            glass_mode,
            app_version
        ])
        .setup(move |app| {
            let handle = app.handle().clone();
            start_engine(&handle, engine.clone());

            if let Some(w) = app.get_webview_window("main") {
                let _ = w.set_theme(Some(tauri::Theme::Dark));
                if mica_supported() {
                    use tauri::window::{Effect, EffectState, EffectsBuilder};
                    let _ = w.set_effects(
                        EffectsBuilder::new().effect(Effect::Mica).state(EffectState::Active).build(),
                    );
                }
            }

            let open = MenuItem::with_id(app, "open", "Open DiskTree", true, None::<&str>)?;
            let quit = MenuItem::with_id(app, "quit", "Quit", true, None::<&str>)?;
            let menu = Menu::with_items(app, &[&open, &quit])?;
            TrayIconBuilder::with_id("main")
                .icon(app.default_window_icon().cloned().expect("app icon"))
                .tooltip("DiskTree")
                .menu(&menu)
                .show_menu_on_left_click(false)
                .on_menu_event(|app, ev| match ev.id().as_ref() {
                    "open" => show_main(app),
                    "quit" => app.exit(0),
                    _ => {}
                })
                .on_tray_icon_event(|tray, ev| {
                    if let TrayIconEvent::Click {
                        button: MouseButton::Left,
                        button_state: MouseButtonState::Up,
                        ..
                    } = ev
                    {
                        show_main(tray.app_handle());
                    }
                })
                .build(app)?;
            Ok(())
        })
        .build(tauri::generate_context!())
        .expect("error while building DiskTree");

    app.run(|handle, event| {
        if let RunEvent::Exit = event {
            stop_engine(&handle.state::<SharedEngine>());
        }
    });
}
