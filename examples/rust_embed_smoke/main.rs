use std::ffi::{c_char, c_void, CString};
use std::process;

#[repr(C)]
struct BrowserdSession {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Copy, Clone, PartialEq, Eq)]
enum BrowserdStatusCode {
    Ok = 0,
}

#[repr(C)]
#[derive(Copy, Clone)]
struct BrowserdStatus {
    code: BrowserdStatusCode,
    message: *const c_char,
    message_len: usize,
}

#[repr(C)]
struct BrowserdProcessResult {
    handled: bool,
    exit_code: i32,
}

const BROWSERD_SWITCH_BROWSER: u32 = 1 << 0;
const BROWSERD_SWITCH_GPU_CHILD: u32 = 1 << 1;

#[repr(C)]
struct BrowserdSwitch {
    size: u32,
    name: *const c_char,
    value: *const c_char,
    scope: u32,
}

#[repr(C)]
struct BrowserdConfig {
    size: u32,
    gui: bool,
    user_data_dir: *const c_char,
    switches: *const BrowserdSwitch,
    switches_len: usize,
}

#[repr(C)]
struct BrowserdString {
    data: *const c_char,
    len: usize,
}

type ReadyCallback =
    unsafe extern "C" fn(*mut BrowserdSession, BrowserdStatus, *mut c_void);
type StatusCallback =
    unsafe extern "C" fn(*mut BrowserdSession, BrowserdStatus, *mut c_void);
type StringCallback = unsafe extern "C" fn(
    *mut BrowserdSession,
    BrowserdStatus,
    BrowserdString,
    *mut c_void,
);

#[link(name = "browserd_embed")]
extern "C" {
    fn browserd_process_main(argc: i32, argv: *const *const c_char) -> BrowserdProcessResult;
    fn browserd_run(config: *const BrowserdConfig, ready: ReadyCallback, user_data: *mut c_void)
        -> i32;
    fn browserd_shutdown(session: *mut BrowserdSession);
    fn browserd_navigate(
        session: *mut BrowserdSession,
        target_id: *const c_char,
        url: *const c_char,
        callback: StatusCallback,
        user_data: *mut c_void,
    ) -> i32;
    fn browserd_evaluate(
        session: *mut BrowserdSession,
        target_id: *const c_char,
        expression: *const c_char,
        callback: StringCallback,
        user_data: *mut c_void,
    ) -> i32;
}

unsafe fn status_ok(status: BrowserdStatus) -> bool {
    status.code == BrowserdStatusCode::Ok
}

unsafe extern "C" fn on_evaluate(
    session: *mut BrowserdSession,
    status: BrowserdStatus,
    value: BrowserdString,
    _user_data: *mut c_void,
) {
    if status_ok(status) {
        let bytes = std::slice::from_raw_parts(value.data as *const u8, value.len);
        eprintln!("document.title = {}", String::from_utf8_lossy(bytes));
    }
    browserd_shutdown(session);
}

unsafe extern "C" fn on_navigate(
    session: *mut BrowserdSession,
    status: BrowserdStatus,
    user_data: *mut c_void,
) {
    if !status_ok(status) {
        browserd_shutdown(session);
        return;
    }

    let expression = CString::new("document.title").unwrap();
    let rc = browserd_evaluate(
        session,
        std::ptr::null(),
        expression.as_ptr(),
        on_evaluate,
        user_data,
    );
    if rc != 0 {
        browserd_shutdown(session);
    }
}

unsafe extern "C" fn on_ready(
    session: *mut BrowserdSession,
    status: BrowserdStatus,
    user_data: *mut c_void,
) {
    if session.is_null() || !status_ok(status) {
        return;
    }

    let url = CString::new(
        "data:text/html,<html><head><title>browserd rust smoke</title></head><body>ok</body></html>",
    )
    .unwrap();
    let rc = browserd_navigate(
        session,
        std::ptr::null(),
        url.as_ptr(),
        on_navigate,
        user_data,
    );
    if rc != 0 {
        browserd_shutdown(session);
    }
}

fn main() {
    let args: Vec<CString> = std::env::args()
        .map(|arg| CString::new(arg).unwrap())
        .collect();
    let argv: Vec<*const c_char> = args.iter().map(|arg| arg.as_ptr()).collect();

    let process_result = unsafe { browserd_process_main(argv.len() as i32, argv.as_ptr()) };
    if process_result.handled {
        process::exit(process_result.exit_code);
    }

    let disable_features = CString::new("disable-features").unwrap();
    let fallback_feature = CString::new("FallbackToSWIfGLES3NotSupported").unwrap();
    let switches = [BrowserdSwitch {
        size: std::mem::size_of::<BrowserdSwitch>() as u32,
        name: disable_features.as_ptr(),
        value: fallback_feature.as_ptr(),
        scope: BROWSERD_SWITCH_BROWSER | BROWSERD_SWITCH_GPU_CHILD,
    }];

    let config = BrowserdConfig {
        size: std::mem::size_of::<BrowserdConfig>() as u32,
        gui: false,
        user_data_dir: std::ptr::null(),
        switches: switches.as_ptr(),
        switches_len: switches.len(),
    };
    let exit_code = unsafe { browserd_run(&config, on_ready, std::ptr::null_mut()) };
    process::exit(exit_code);
}
