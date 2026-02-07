//! Shared reactive state for the wallet UI.

use std::sync::{Arc, Mutex};

use crate::ffi::{Calculator, SolanaClient};

/// Current calculator connection state.
#[derive(Clone, Debug, PartialEq)]
pub enum ConnectionStatus {
    Disconnected,
    Connecting,
    Connected,
    Error(String),
}

/// A loaded keypair (fetched from the calculator).
#[derive(Clone, Debug)]
pub struct LoadedKeypair {
    pub slot: String,
    pub public_key: [u8; 32],
    pub public_key_base58: String,
    pub encrypted_blob: [u8; cwallet_sys::WALLET_BLOB_LEN],
}

/// Top-level reactive state, stored in a Dioxus `Signal`.
#[derive(Clone, Debug)]
pub struct WalletState {
    pub connection_status: ConnectionStatus,
    pub loaded_keypair: Option<LoadedKeypair>,
    pub balance_lamports: Option<u64>,
    pub rpc_url: String,
    pub last_error: Option<String>,
    pub pending_operation: Option<String>,
}

impl Default for WalletState {
    fn default() -> Self {
        Self {
            connection_status: ConnectionStatus::Disconnected,
            loaded_keypair: None,
            balance_lamports: None,
            rpc_url: "https://api.devnet.solana.com".to_string(),
            last_error: None,
            pending_operation: None,
        }
    }
}

/// Thread-safe handle to the calculator session.
pub type SharedCalculator = Arc<Mutex<Option<Calculator>>>;

/// Thread-safe handle to the Solana RPC client.
pub type SharedSolanaClient = Arc<Mutex<Option<SolanaClient>>>;
