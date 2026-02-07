//! Safe Rust wrappers around the raw cwallet-sys FFI.

use std::ffi::{CStr, CString};
use std::fmt;

use cwallet_sys as sys;

// ---------------------------------------------------------------------------
// Error type
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub enum WalletError {
    NoCalc,
    NoCable,
    Alloc,
    NotReady,
    Io(String),
    Thread,
    Crypto,
    SolanaInvalidArgument,
    SolanaAlloc,
    SolanaCurl,
    SolanaHttp,
    JsonParse(String),
}

impl fmt::Display for WalletError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::NoCalc => write!(f, "No calculator detected"),
            Self::NoCable => write!(f, "No cable detected"),
            Self::Alloc => write!(f, "Memory allocation failed"),
            Self::NotReady => write!(f, "Calculator not ready"),
            Self::Io(msg) => write!(f, "I/O error: {msg}"),
            Self::Thread => write!(f, "Thread error"),
            Self::Crypto => write!(f, "Cryptographic error"),
            Self::SolanaInvalidArgument => write!(f, "Solana: invalid argument"),
            Self::SolanaAlloc => write!(f, "Solana: allocation failed"),
            Self::SolanaCurl => write!(f, "Solana: network error"),
            Self::SolanaHttp => write!(f, "Solana: HTTP error"),
            Self::JsonParse(msg) => write!(f, "JSON parse error: {msg}"),
        }
    }
}

fn app_result(code: i32) -> Result<(), WalletError> {
    match code {
        sys::APP_OK => Ok(()),
        sys::APP_ERR_NO_CALC => Err(WalletError::NoCalc),
        sys::APP_ERR_NO_CABLE => Err(WalletError::NoCable),
        sys::APP_ERR_ALLOC => Err(WalletError::Alloc),
        sys::APP_ERR_NOT_READY => Err(WalletError::NotReady),
        sys::APP_ERR_THREAD => Err(WalletError::Thread),
        sys::APP_ERR_CRYPTO => Err(WalletError::Crypto),
        _ => Err(WalletError::Io(format!("app error code {code}"))),
    }
}

fn solana_result(code: i32) -> Result<(), WalletError> {
    match code {
        sys::SOLANA_OK => Ok(()),
        sys::SOLANA_ERROR_INVALID_ARGUMENT => Err(WalletError::SolanaInvalidArgument),
        sys::SOLANA_ERROR_ALLOCATION_FAILED => Err(WalletError::SolanaAlloc),
        sys::SOLANA_ERROR_CURL => Err(WalletError::SolanaCurl),
        sys::SOLANA_ERROR_HTTP_STATUS => Err(WalletError::SolanaHttp),
        _ => Err(WalletError::Io(format!("solana error code {code}"))),
    }
}

// ---------------------------------------------------------------------------
// TI library initialization (call once at startup)
// ---------------------------------------------------------------------------

pub fn init_ti_libraries() {
    unsafe {
        sys::ticables_library_init();
        sys::tifiles_library_init();
        sys::ticalcs_library_init();
    }
}

// ---------------------------------------------------------------------------
// Calculator
// ---------------------------------------------------------------------------

/// Owns a `CalcSession`, cleaning up on drop.
pub struct Calculator {
    session: sys::CalcSession,
}

// All access goes through Arc<Mutex<>>, so this is safe.
unsafe impl Send for Calculator {}

impl Calculator {
    /// Open a connection to the calculator.
    pub fn open() -> Result<Self, WalletError> {
        let mut session = unsafe { std::mem::zeroed::<sys::CalcSession>() };
        // Default to port 1 (same as CLI)
        session.port_number = 1;
        app_result(unsafe { sys::calc_session_open(&mut session) })?;
        Ok(Self { session })
    }

    pub fn start_polling(&mut self, interval_ms: i32) -> Result<(), WalletError> {
        app_result(unsafe { sys::calc_session_start_polling(&mut self.session, interval_ms) })
    }

    pub fn stop_polling(&mut self) {
        unsafe { sys::calc_session_stop_polling(&mut self.session) };
    }

    /// Store a keypair payload (32-byte pubkey + 125-byte blob) in a Str slot.
    pub fn store_keypair(
        &mut self,
        slot: &str,
        public_key: &[u8; 32],
        blob: &[u8; sys::WALLET_BLOB_LEN],
    ) -> Result<(), WalletError> {
        let var_name = CString::new(slot).map_err(|_| WalletError::Io("invalid slot".into()))?;
        let mut payload = [0u8; sys::STORED_KEY_PAYLOAD_LEN];
        payload[..32].copy_from_slice(public_key);
        payload[32..].copy_from_slice(blob);
        app_result(unsafe {
            sys::calc_store_binary_string(
                &mut self.session,
                var_name.as_ptr(),
                payload.as_ptr(),
                payload.len(),
            )
        })
    }

    /// Fetch a keypair from a calculator Str slot.
    /// Returns (public_key, encrypted_blob).
    pub fn fetch_keypair(
        &mut self,
        slot: &str,
    ) -> Result<([u8; 32], [u8; sys::WALLET_BLOB_LEN]), WalletError> {
        let var_name = CString::new(slot).map_err(|_| WalletError::Io("invalid slot".into()))?;
        let mut buf = [0u8; 256];
        let mut out_len: usize = 0;

        app_result(unsafe {
            sys::calc_fetch_binary_string(
                &mut self.session,
                var_name.as_ptr(),
                buf.as_mut_ptr(),
                buf.len(),
                &mut out_len,
            )
        })?;

        if out_len != sys::STORED_KEY_PAYLOAD_LEN {
            return Err(WalletError::Io(format!(
                "unexpected payload length: {out_len} (expected {})",
                sys::STORED_KEY_PAYLOAD_LEN
            )));
        }

        let mut public_key = [0u8; 32];
        let mut blob = [0u8; sys::WALLET_BLOB_LEN];
        public_key.copy_from_slice(&buf[..32]);
        blob.copy_from_slice(&buf[32..sys::STORED_KEY_PAYLOAD_LEN]);

        // Secure-zero the temporary buffer
        secure_zero(&mut buf);

        Ok((public_key, blob))
    }
}

impl Drop for Calculator {
    fn drop(&mut self) {
        unsafe { sys::calc_session_cleanup(&mut self.session) };
    }
}

// ---------------------------------------------------------------------------
// SolanaClient
// ---------------------------------------------------------------------------

pub struct SolanaClient {
    client: sys::solana_client_t,
}

unsafe impl Send for SolanaClient {}

impl SolanaClient {
    pub fn new(rpc_url: &str) -> Result<Self, WalletError> {
        let url = CString::new(rpc_url).map_err(|_| WalletError::SolanaInvalidArgument)?;
        let mut client = unsafe { std::mem::zeroed::<sys::solana_client_t>() };
        solana_result(unsafe { sys::solana_client_init(&mut client, url.as_ptr()) })?;
        Ok(Self { client })
    }

    pub fn get_balance(&mut self, pubkey_base58: &str) -> Result<u64, WalletError> {
        let pubkey = CString::new(pubkey_base58).map_err(|_| WalletError::SolanaInvalidArgument)?;
        let mut resp: *mut i8 = std::ptr::null_mut();
        solana_result(unsafe {
            sys::solana_client_get_balance(&mut self.client, pubkey.as_ptr(), &mut resp)
        })?;
        let json = unsafe { consume_response(resp) }?;
        parse_balance_response(&json)
    }

    pub fn request_airdrop(
        &mut self,
        pubkey_base58: &str,
        lamports: u64,
    ) -> Result<String, WalletError> {
        let pubkey = CString::new(pubkey_base58).map_err(|_| WalletError::SolanaInvalidArgument)?;
        let mut resp: *mut i8 = std::ptr::null_mut();
        solana_result(unsafe {
            sys::solana_client_request_airdrop(
                &mut self.client,
                pubkey.as_ptr(),
                lamports,
                &mut resp,
            )
        })?;
        let json = unsafe { consume_response(resp) }?;
        parse_string_result(&json)
    }

    pub fn get_latest_blockhash(&mut self) -> Result<String, WalletError> {
        let mut resp: *mut i8 = std::ptr::null_mut();
        solana_result(unsafe {
            sys::solana_client_get_latest_blockhash(&mut self.client, &mut resp)
        })?;
        let json = unsafe { consume_response(resp) }?;
        // Response: {"result":{"value":{"blockhash":"...","lastValidBlockHeight":N}}}
        let v: serde_json::Value =
            serde_json::from_str(&json).map_err(|e| WalletError::JsonParse(e.to_string()))?;
        v["result"]["value"]["blockhash"]
            .as_str()
            .map(String::from)
            .ok_or_else(|| WalletError::JsonParse("missing blockhash".into()))
    }

    pub fn send_transaction(&mut self, tx_base64: &str) -> Result<String, WalletError> {
        let tx = CString::new(tx_base64).map_err(|_| WalletError::SolanaInvalidArgument)?;
        let mut resp: *mut i8 = std::ptr::null_mut();
        solana_result(unsafe {
            sys::solana_client_send_transaction(&mut self.client, tx.as_ptr(), &mut resp)
        })?;
        let json = unsafe { consume_response(resp) }?;
        parse_string_result(&json)
    }

    pub fn get_signature_status(&mut self, signature: &str) -> Result<Option<String>, WalletError> {
        let sig = CString::new(signature).map_err(|_| WalletError::SolanaInvalidArgument)?;
        let mut resp: *mut i8 = std::ptr::null_mut();
        solana_result(unsafe {
            sys::solana_client_get_signature_status(&mut self.client, sig.as_ptr(), &mut resp)
        })?;
        let json = unsafe { consume_response(resp) }?;
        // Response: {"result":{"value":[{"confirmationStatus":"finalized",...}]}}
        let v: serde_json::Value =
            serde_json::from_str(&json).map_err(|e| WalletError::JsonParse(e.to_string()))?;
        let status = &v["result"]["value"][0];
        if status.is_null() {
            return Ok(None);
        }
        Ok(status["confirmationStatus"].as_str().map(String::from))
    }
}

impl Drop for SolanaClient {
    fn drop(&mut self) {
        unsafe { sys::solana_client_cleanup(&mut self.client) };
    }
}

// ---------------------------------------------------------------------------
// Stateless crypto / encoding helpers
// ---------------------------------------------------------------------------

pub fn create_keypair() -> Result<([u8; 32], [u8; 64]), WalletError> {
    let mut seed = [0u8; 32];
    let mut public_key = [0u8; 32];
    let mut private_key = [0u8; 64];

    let rc = unsafe { sys::ed25519_create_seed(seed.as_mut_ptr()) };
    if rc != 0 {
        return Err(WalletError::Crypto);
    }

    unsafe {
        sys::ed25519_create_keypair(
            public_key.as_mut_ptr(),
            private_key.as_mut_ptr(),
            seed.as_ptr(),
        );
        sys::wallet_secure_zero(seed.as_mut_ptr() as *mut _, seed.len());
    }

    Ok((public_key, private_key))
}

pub fn encrypt_private_key(
    password: &str,
    private_key: &[u8; 64],
) -> Result<[u8; sys::WALLET_BLOB_LEN], WalletError> {
    let pw = CString::new(password).map_err(|_| WalletError::Crypto)?;
    let mut blob = [0u8; sys::WALLET_BLOB_LEN];
    app_result(unsafe {
        sys::wallet_encrypt_private_key(
            pw.as_ptr(),
            private_key.as_ptr(),
            private_key.len(),
            blob.as_mut_ptr(),
            blob.len(),
        )
    })?;
    Ok(blob)
}

pub fn decrypt_private_key(
    password: &str,
    blob: &[u8; sys::WALLET_BLOB_LEN],
) -> Result<[u8; 64], WalletError> {
    let pw = CString::new(password).map_err(|_| WalletError::Crypto)?;
    let mut private_key = [0u8; 64];
    app_result(unsafe {
        sys::wallet_decrypt_private_key(
            pw.as_ptr(),
            blob.as_ptr(),
            blob.len(),
            private_key.as_mut_ptr(),
            private_key.len(),
        )
    })?;
    Ok(private_key)
}

pub fn base58_encode(data: &[u8]) -> Result<String, WalletError> {
    let mut buf = [0u8; 128];
    let len = unsafe {
        sys::solana_base58_encode(
            data.as_ptr(),
            data.len(),
            buf.as_mut_ptr() as *mut i8,
            buf.len(),
        )
    };
    if len <= 0 {
        return Err(WalletError::Io("base58 encode failed".into()));
    }
    let s = std::str::from_utf8(&buf[..len as usize])
        .map_err(|e| WalletError::Io(e.to_string()))?;
    Ok(s.to_string())
}

pub fn base58_decode(input: &str) -> Result<Vec<u8>, WalletError> {
    let c_input = CString::new(input).map_err(|_| WalletError::Io("invalid base58 input".into()))?;
    let mut buf = [0u8; 64];
    let len = unsafe { sys::solana_base58_decode(c_input.as_ptr(), buf.as_mut_ptr(), buf.len()) };
    if len <= 0 {
        return Err(WalletError::Io("base58 decode failed".into()));
    }
    Ok(buf[..len as usize].to_vec())
}

pub fn base64_encode(data: &[u8]) -> Result<String, WalletError> {
    let mut buf = vec![0u8; data.len() * 2 + 4];
    let len = unsafe {
        sys::solana_base64_encode(
            data.as_ptr(),
            data.len(),
            buf.as_mut_ptr() as *mut i8,
            buf.len(),
        )
    };
    if len == 0 {
        return Err(WalletError::Io("base64 encode failed".into()));
    }
    let s = std::str::from_utf8(&buf[..len])
        .map_err(|e| WalletError::Io(e.to_string()))?;
    Ok(s.to_string())
}

pub fn ed25519_sign(message: &[u8], public_key: &[u8; 32], private_key: &[u8; 64]) -> [u8; 64] {
    let mut signature = [0u8; 64];
    unsafe {
        sys::ed25519_sign(
            signature.as_mut_ptr(),
            message.as_ptr(),
            message.len(),
            public_key.as_ptr(),
            private_key.as_ptr(),
        );
    }
    signature
}

pub fn secure_zero(buf: &mut [u8]) {
    unsafe {
        sys::wallet_secure_zero(buf.as_mut_ptr() as *mut _, buf.len());
    }
}

// ---------------------------------------------------------------------------
// Internal JSON helpers
// ---------------------------------------------------------------------------

unsafe fn consume_response(resp: *mut i8) -> Result<String, WalletError> {
    if resp.is_null() {
        return Err(WalletError::Io("null RPC response".into()));
    }
    let s = CStr::from_ptr(resp)
        .to_string_lossy()
        .into_owned();
    sys::solana_client_free_response(resp);
    Ok(s)
}

fn parse_balance_response(json: &str) -> Result<u64, WalletError> {
    let v: serde_json::Value =
        serde_json::from_str(json).map_err(|e| WalletError::JsonParse(e.to_string()))?;
    // getBalance returns: {"result":{"value": <lamports>}}
    v["result"]["value"]
        .as_u64()
        .ok_or_else(|| WalletError::JsonParse("missing balance value".into()))
}

fn parse_string_result(json: &str) -> Result<String, WalletError> {
    let v: serde_json::Value =
        serde_json::from_str(json).map_err(|e| WalletError::JsonParse(e.to_string()))?;
    if let Some(err) = v.get("error") {
        let msg = err["message"].as_str().unwrap_or("unknown RPC error");
        return Err(WalletError::Io(msg.to_string()));
    }
    v["result"]
        .as_str()
        .map(String::from)
        .ok_or_else(|| WalletError::JsonParse("missing result string".into()))
}
