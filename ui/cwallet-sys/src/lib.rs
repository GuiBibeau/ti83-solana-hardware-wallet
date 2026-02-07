//! Raw FFI bindings for c_wallet.
//!
//! All TI library types (CableHandle, CalcHandle, FileContent, VarEntry, …)
//! are opaque — we only ever pass them through pointers, so they are
//! represented as `*mut c_void`.

#![allow(non_camel_case_types)]

use std::os::raw::{c_char, c_int, c_long, c_uchar, c_void};

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// App error codes (calc_session.h)
pub const APP_OK: c_int = 0;
pub const APP_ERR_NO_CALC: c_int = 1;
pub const APP_ERR_NO_CABLE: c_int = 2;
pub const APP_ERR_ALLOC: c_int = 3;
pub const APP_ERR_NOT_READY: c_int = 4;
pub const APP_ERR_IO: c_int = 5;
pub const APP_ERR_THREAD: c_int = 6;
pub const APP_ERR_CRYPTO: c_int = 7;

// Solana error codes (solana_client.h)
pub const SOLANA_OK: c_int = 0;
pub const SOLANA_ERROR_INVALID_ARGUMENT: c_int = -1;
pub const SOLANA_ERROR_ALLOCATION_FAILED: c_int = -2;
pub const SOLANA_ERROR_CURL: c_int = -3;
pub const SOLANA_ERROR_HTTP_STATUS: c_int = -4;

// Wallet crypto sizes (wallet_crypto.h)
pub const WALLET_BLOB_VERSION: u32 = 1;
pub const WALLET_SALT_LEN: usize = 16;
pub const WALLET_NONCE_LEN: usize = 12;
pub const WALLET_MAC_LEN: usize = 32;
pub const WALLET_PUBLIC_KEY_LEN: usize = 32;
pub const WALLET_PRIVATE_KEY_LEN: usize = 64;
pub const WALLET_SEED_LEN: usize = 32;
pub const WALLET_BLOB_LEN: usize = 1 + WALLET_SALT_LEN + WALLET_NONCE_LEN + WALLET_PRIVATE_KEY_LEN + WALLET_MAC_LEN;

/// 32-byte public key + 125-byte encrypted blob
pub const STORED_KEY_PAYLOAD_LEN: usize = WALLET_PUBLIC_KEY_LEN + WALLET_BLOB_LEN;

// ---------------------------------------------------------------------------
// CalcSession — laid out identically to the C struct so we can stack-allocate
// and pass `&mut` to C functions.
// ---------------------------------------------------------------------------

/// Mirrors `CalcSession` from calc_session.h.
///
/// Fields use opaque pointers for TI library handles.  The struct is
/// zero-initialized on the Rust side, matching the C convention.
#[repr(C)]
pub struct CalcSession {
    pub cable: *mut c_void,       // CableHandle*
    pub calc: *mut c_void,        // CalcHandle*
    pub cable_model: c_int,       // CableModel (enum → int)
    pub calc_model: c_int,        // CalcModel  (enum → int)
    pub port_number: c_int,       // CablePort  (enum → int)
    pub poll_interval_ms: c_int,
    pub poll_active: c_int,       // volatile int
    pub poll_thread_started: c_int,
    pub poll_thread: u64,         // pthread_t (opaque, 8 bytes on 64-bit)
}

// ---------------------------------------------------------------------------
// solana_client_t — mirrors the C struct
// ---------------------------------------------------------------------------

#[repr(C)]
pub struct solana_client_t {
    pub rpc_url: *mut c_char,
    pub timeout_ms: c_long,
    pub next_request_id: u64,
}

// ---------------------------------------------------------------------------
// Extern "C" declarations
// ---------------------------------------------------------------------------

extern "C" {
    // -- TI library initialization ------------------------------------------
    pub fn ticables_library_init() -> c_int;
    pub fn tifiles_library_init() -> c_int;
    pub fn ticalcs_library_init() -> c_int;

    // -- Calculator session -------------------------------------------------
    pub fn calc_session_open(session: *mut CalcSession) -> c_int;
    pub fn calc_session_cleanup(session: *mut CalcSession);
    pub fn calc_session_start_polling(session: *mut CalcSession, interval_ms: c_int) -> c_int;
    pub fn calc_session_stop_polling(session: *mut CalcSession);

    // -- Calculator string storage ------------------------------------------
    pub fn calc_store_persistent_string(
        session: *mut CalcSession,
        var_name: *const c_char,
        payload: *const c_char,
    ) -> c_int;

    pub fn calc_fetch_string(
        session: *mut CalcSession,
        var_name: *const c_char,
        out_content: *mut *mut c_void, // FileContent**
    ) -> c_int;

    pub fn calc_store_binary_string(
        session: *mut CalcSession,
        var_name: *const c_char,
        payload: *const u8,
        payload_len: usize,
    ) -> c_int;

    pub fn calc_fetch_binary_string(
        session: *mut CalcSession,
        var_name: *const c_char,
        out_data: *mut u8,
        out_size: usize,
        out_len: *mut usize,
    ) -> c_int;

    // -- Wallet crypto ------------------------------------------------------
    pub fn wallet_random_bytes(buffer: *mut u8, length: usize) -> c_int;
    pub fn wallet_secure_zero(ptr: *mut c_void, length: usize);

    pub fn wallet_encrypt_private_key(
        password: *const c_char,
        private_key: *const u8,
        private_key_len: usize,
        out_blob: *mut u8,
        out_blob_len: usize,
    ) -> c_int;

    pub fn wallet_decrypt_private_key(
        password: *const c_char,
        blob: *const u8,
        blob_len: usize,
        out_private_key: *mut u8,
        private_key_len: usize,
    ) -> c_int;

    // -- Ed25519 ------------------------------------------------------------
    pub fn ed25519_create_seed(seed: *mut c_uchar) -> c_int;

    pub fn ed25519_create_keypair(
        public_key: *mut c_uchar,
        private_key: *mut c_uchar,
        seed: *const c_uchar,
    );

    pub fn ed25519_derive_public_key(
        public_key: *mut c_uchar,
        private_key: *const c_uchar,
    );

    pub fn ed25519_sign(
        signature: *mut c_uchar,
        message: *const c_uchar,
        message_len: usize,
        public_key: *const c_uchar,
        private_key: *const c_uchar,
    );

    pub fn ed25519_verify(
        signature: *const c_uchar,
        message: *const c_uchar,
        message_len: usize,
        public_key: *const c_uchar,
    ) -> c_int;

    // -- Solana client ------------------------------------------------------
    pub fn solana_client_init(
        client: *mut solana_client_t,
        rpc_url: *const c_char,
    ) -> c_int;

    pub fn solana_client_cleanup(client: *mut solana_client_t);

    pub fn solana_client_set_timeout(client: *mut solana_client_t, timeout_ms: c_long);

    pub fn solana_client_rpc_request(
        client: *mut solana_client_t,
        method: *const c_char,
        params_json: *const c_char,
        out_response: *mut *mut c_char,
    ) -> c_int;

    pub fn solana_client_get_latest_blockhash(
        client: *mut solana_client_t,
        out_response: *mut *mut c_char,
    ) -> c_int;

    pub fn solana_client_request_airdrop(
        client: *mut solana_client_t,
        public_key_base58: *const c_char,
        lamports: u64,
        out_response: *mut *mut c_char,
    ) -> c_int;

    pub fn solana_client_get_balance(
        client: *mut solana_client_t,
        public_key_base58: *const c_char,
        out_response: *mut *mut c_char,
    ) -> c_int;

    pub fn solana_client_get_signature_status(
        client: *mut solana_client_t,
        signature: *const c_char,
        out_response: *mut *mut c_char,
    ) -> c_int;

    pub fn solana_client_send_transaction(
        client: *mut solana_client_t,
        signed_transaction_base64: *const c_char,
        out_response: *mut *mut c_char,
    ) -> c_int;

    pub fn solana_client_free_response(response: *mut c_char);

    // -- Solana encoding ----------------------------------------------------
    pub fn solana_base58_encode(
        data: *const u8,
        data_len: usize,
        out: *mut c_char,
        out_size: usize,
    ) -> c_int;

    pub fn solana_base58_decode(
        input: *const c_char,
        out: *mut u8,
        out_size: usize,
    ) -> c_int;

    pub fn solana_base64_encode(
        data: *const u8,
        data_len: usize,
        out: *mut c_char,
        out_size: usize,
    ) -> usize;

    // -- tifiles helpers (for FileContent cleanup) --------------------------
    pub fn tifiles_content_delete_regular(content: *mut c_void);
}
