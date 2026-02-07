use dioxus::prelude::*;

use crate::ffi;
use crate::state::{SharedSolanaClient, WalletState};

const LAMPORTS_PER_SOL: u64 = 1_000_000_000;

#[component]
pub fn AirdropPage() -> Element {
    let mut wallet = use_context::<Signal<WalletState>>();
    let solana = use_context::<SharedSolanaClient>();

    let mut amount_str = use_signal(|| "1".to_string());
    let mut status_msg = use_signal(|| None::<String>);
    let mut error_msg = use_signal(|| None::<String>);
    let mut busy = use_signal(|| false);

    let loaded = wallet.read().loaded_keypair.clone();

    let on_request = move |_| {
        let pubkey = match &wallet.read().loaded_keypair {
            Some(kp) => kp.public_key_base58.clone(),
            None => return,
        };
        let rpc_url = wallet.read().rpc_url.clone();
        let solana = solana.clone();

        let amount_text = amount_str.read().clone();
        let lamports = match parse_lamports(&amount_text) {
            Some(l) => l,
            None => {
                error_msg.set(Some("Invalid amount. Enter a number in SOL.".into()));
                return;
            }
        };

        error_msg.set(None);
        status_msg.set(None);
        busy.set(true);

        spawn(async move {
            // Request airdrop
            let sig_result = tokio::task::spawn_blocking({
                let solana = solana.clone();
                let pubkey = pubkey.clone();
                let rpc_url = rpc_url.clone();
                move || {
                    let mut guard = solana.lock().unwrap();
                    if guard.is_none() {
                        *guard = Some(ffi::SolanaClient::new(&rpc_url)?);
                    }
                    let client = guard.as_mut().unwrap();
                    client.request_airdrop(&pubkey, lamports)
                }
            })
            .await
            .unwrap();

            match sig_result {
                Ok(signature) => {
                    status_msg.set(Some(format!("Airdrop requested. Signature: {signature}")));

                    // Poll for confirmation
                    let confirm_result = poll_confirmation(solana.clone(), rpc_url, &signature).await;
                    busy.set(false);

                    match confirm_result {
                        Ok(()) => {
                            status_msg.set(Some(format!(
                                "Airdrop confirmed! Signature: {signature}"
                            )));
                            // Clear cached balance so it refreshes
                            wallet.write().balance_lamports = None;
                        }
                        Err(e) => {
                            error_msg.set(Some(format!("Confirmation failed: {e}")));
                        }
                    }
                }
                Err(e) => {
                    busy.set(false);
                    error_msg.set(Some(e.to_string()));
                }
            }
        });
    };

    rsx! {
        div { class: "page",
            h1 { "Request Airdrop" }
            p { class: "subtitle", "Request devnet SOL from the faucet." }

            if let Some(kp) = &loaded {
                p { class: "label", "Wallet: " span { class: "mono", "{kp.public_key_base58}" } }

                div { class: "form-group",
                    label { "Amount (SOL)" }
                    input {
                        class: "input",
                        r#type: "text",
                        placeholder: "1",
                        value: "{amount_str}",
                        oninput: move |e| amount_str.set(e.value()),
                    }
                }

                button {
                    class: "btn btn-primary",
                    disabled: *busy.read(),
                    onclick: on_request,
                    if *busy.read() { "Requesting..." } else { "Request Airdrop" }
                }
            } else {
                p { class: "hint", "Load a keypair first." }
            }

            if let Some(msg) = status_msg.read().as_ref() {
                p { class: "success-text", "{msg}" }
            }
            if let Some(msg) = error_msg.read().as_ref() {
                p { class: "error-text", "{msg}" }
            }
        }
    }
}

fn parse_lamports(input: &str) -> Option<u64> {
    let trimmed = input.trim();
    // If it contains a dot, treat as SOL
    if trimmed.contains('.') {
        let sol: f64 = trimmed.parse().ok()?;
        if sol <= 0.0 {
            return None;
        }
        Some((sol * LAMPORTS_PER_SOL as f64) as u64)
    } else {
        // Try as integer SOL first (most common for airdrop)
        let val: u64 = trimmed.parse().ok()?;
        if val == 0 {
            return None;
        }
        // Assume SOL for small numbers, lamports for large
        if val <= 100 {
            Some(val * LAMPORTS_PER_SOL)
        } else {
            Some(val)
        }
    }
}

async fn poll_confirmation(
    solana: crate::state::SharedSolanaClient,
    rpc_url: String,
    signature: &str,
) -> Result<(), ffi::WalletError> {
    let sig = signature.to_string();
    for _ in 0..30 {
        tokio::time::sleep(std::time::Duration::from_secs(1)).await;

        let solana = solana.clone();
        let rpc_url = rpc_url.clone();
        let sig = sig.clone();

        let status = tokio::task::spawn_blocking(move || {
            let mut guard = solana.lock().unwrap();
            if guard.is_none() {
                *guard = Some(ffi::SolanaClient::new(&rpc_url)?);
            }
            let client = guard.as_mut().unwrap();
            client.get_signature_status(&sig)
        })
        .await
        .unwrap()?;

        if let Some(confirmation) = status {
            if confirmation == "finalized" || confirmation == "confirmed" {
                return Ok(());
            }
        }
    }
    Err(ffi::WalletError::Io("airdrop confirmation timed out".into()))
}
