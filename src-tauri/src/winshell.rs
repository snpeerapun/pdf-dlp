//! Windows Explorer right-click integration (per-user, no admin): HKCU\Software\Classes\SystemFileAssociations\.pdf\shell\PDFDLP.*
use serde::Deserialize;

#[derive(Deserialize, Clone)]
#[allow(dead_code)]
pub struct MenuItem { pub action: String, pub label: String }

#[cfg(windows)]
fn base() -> &'static str { r"Software\Classes\SystemFileAssociations\.pdf\shell" }

#[cfg(windows)]
pub fn register(items: &[MenuItem]) -> Result<(), String> {
    use winreg::enums::*; use winreg::RegKey;
    let exe = std::env::current_exe().map_err(|e| e.to_string())?.display().to_string();
    let hkcu = RegKey::predef(HKEY_CURRENT_USER);
    for (i, it) in items.iter().enumerate() {
        let key = format!("{}\\PDFDLP.{}", base(), it.action);
        let (k, _) = hkcu.create_subkey(&key).map_err(|e| e.to_string())?;
        k.set_value("", &it.label).map_err(|e| e.to_string())?;
        k.set_value("Icon", &format!("\"{exe}\",0")).map_err(|e| e.to_string())?;
        k.set_value("Position", &(if i == 0 { "Top" } else { "Middle" }).to_string()).ok();
        let (c, _) = k.create_subkey("command").map_err(|e| e.to_string())?;
        c.set_value("", &format!("\"{exe}\" --action {} \"%1\"", it.action)).map_err(|e| e.to_string())?;
    }
    Ok(())
}

#[cfg(windows)]
pub fn unregister() -> Result<(), String> {
    use winreg::enums::*; use winreg::RegKey;
    let hkcu = RegKey::predef(HKEY_CURRENT_USER);
    if let Ok(shell) = hkcu.open_subkey_with_flags(base(), KEY_ALL_ACCESS) {
        let names: Vec<String> = shell.enum_keys().flatten().filter(|n| n.starts_with("PDFDLP.")).collect();
        for n in names { shell.delete_subkey_all(&n).map_err(|e| e.to_string())?; }
    }
    Ok(())
}

#[cfg(windows)]
pub fn is_registered() -> bool {
    use winreg::enums::*; use winreg::RegKey;
    RegKey::predef(HKEY_CURRENT_USER).open_subkey(format!("{}\\PDFDLP.open", base())).is_ok()
}

#[cfg(not(windows))]
pub fn register(_items: &[MenuItem]) -> Result<(), String> { Err("เมนูคลิกขวาของ Explorer มีเฉพาะบน Windows (บน macOS ใช้ Finder → Open With)".into()) }
#[cfg(not(windows))]
pub fn unregister() -> Result<(), String> { Ok(()) }
#[cfg(not(windows))]
pub fn is_registered() -> bool { false }
