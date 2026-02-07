use dioxus::prelude::*;

use crate::ffi;
use crate::state::{SharedSolanaClient, WalletState};
use crate::transaction;

const LAMPORTS_PER_SOL: u64 = 1_000_000_000;

#[component]
pub fn SendSolPage() -> Element {
    let mut wallet = use_context::<Signal<WalletState>>();
    let solana = use_context::<SharedSolanaClient>();

    let mut recipient = use_signal(String::new);
    let mut amount_str = use_signal(String::new);
    let mut memo = use_signal(|| "sent from my ti83+".to_string());
    let mut password = use_signal(String::new);
    let mut status_msg = use_signal(|| None::<String>);
    let mut error_msg = use_signal(|| None::<String>);
    let mut busy = use_signal(|| false);

    let loaded = wallet.read().loaded_keypair.clone();

    let on_send = move |_| {
        let kp = match wallet.read().loaded_keypair.clone() {
            Some(kp) => kp,
            None => return,
        };
        let rpc_url = wallet.read().rpc_url.clone();
        let solana = solana.clone();

        let recipient_str = recipient.read().clone();
        let amount_text = amount_str.read().clone();
        let memo_text = memo.read().clone();
        let pw = password.read().clone();

        // Validate recipient
        let to_bytes = match ffi::base58_decode(&recipient_str) {
            Ok(b) if b.len() == 32 => {
                let mut arr = [0u8; 32];
                arr.copy_from_slice(&b);
                arr
            }
            _ => {
                error_msg.set(Some("Invalid recipient address".into()));
                return;
            }
        };

        // Parse amount
        let lamports = match parse_lamports(&amount_text) {
            Some(l) => l,
            None => {
                error_msg.set(Some("Invalid amount".into()));
                return;
            }
        };

        if pw.is_empty() {
            error_msg.set(Some("Password required to sign transaction".into()));
            return;
        }

        error_msg.set(None);
        status_msg.set(None);
        busy.set(true);

        spawn(async move {
            // Step 1: Decrypt private key + get blockhash + build + send
            let send_result = tokio::task::spawn_blocking({
                let solana = solana.clone();
                let rpc_url = rpc_url.clone();
                move || {
                    // Decrypt
                    let mut private_key = ffi::decrypt_private_key(&pw, &kp.encrypted_blob)?;

                    // Get latest blockhash
                    let mut guard = solana.lock().unwrap();
                    if guard.is_none() {
                        *guard = Some(ffi::SolanaClient::new(&rpc_url)?);
                    }
                    let client = guard.as_mut().unwrap();
                    let blockhash = client.get_latest_blockhash()?;

                    // Build transaction
                    let memo_opt = if memo_text.is_empty() {
                        None
                    } else {
                        Some(memo_text.as_str())
                    };

                    let signed = transaction::build_transfer(
                        &kp.public_key,
                        &to_bytes,
                        lamports,
                        &blockhash,
                        &private_key,
                        memo_opt,
                    )?;

                    // Secure-zero private key
                    ffi::secure_zero(&mut private_key);

                    // Send
                    let signature = client.send_transaction(&signed.base64)?;
                    Ok::<_, ffi::WalletError>(signature)
                }
            })
            .await
            .unwrap();

            match send_result {
                Ok(signature) => {
                    status_msg.set(Some(format!("Transaction sent! Signature: {signature}")));
                    password.set(String::new());

                    // Poll for confirmation
                    let confirm_result =
                        poll_confirmation(solana.clone(), rpc_url.clone(), &signature).await;
                    busy.set(false);

                    match confirm_result {
                        Ok(()) => {
                            status_msg.set(Some(format!(
                                "Transaction confirmed! Signature: {signature}"
                            )));
                            wallet.write().balance_lamports = None;
                        }
                        Err(e) => {
                            error_msg.set(Some(format!("Confirmation polling: {e}")));
                        }
                    }
                }
                Err(e) => {
                    busy.set(false);
                    error_msg.set(Some(e.to_string()));
                    password.set(String::new());
                }
            }
        });
    };

    rsx! {
        div { class: "page",
            h1 { "Send SOL" }
            p { class: "subtitle", "Transfer SOL to another wallet." }

            if let Some(kp) = &loaded {
                p { class: "label", "From: " span { class: "mono", "{kp.public_key_base58}" } }

                div { class: "form-group",
                    label { "Recipient (base58 public key)" }
                    input {
                        class: "input input-wide",
                        r#type: "text",
                        placeholder: "Recipient public key",
                        value: "{recipient}",
                        oninput: move |e| recipient.set(e.value()),
                    }
                }

                div { class: "form-group",
                    label { "Amount (SOL)" }
                    input {
                        class: "input",
                        r#type: "text",
                        placeholder: "0.001",
                        value: "{amount_str}",
                        oninput: move |e| amount_str.set(e.value()),
                    }
                }

                div { class: "form-group",
                    label { "Memo (optional, max 120 chars)" }
                    input {
                        class: "input input-wide",
                        r#type: "text",
                        maxlength: "120",
                        value: "{memo}",
                        oninput: move |e| memo.set(e.value()),
                    }
                }

                div { class: "form-group",
                    label { "Password (to decrypt private key)" }
                    input {
                        class: "input",
                        r#type: "password",
                        placeholder: "Password",
                        value: "{password}",
                        oninput: move |e| password.set(e.value()),
                    }
                }

                button {
                    class: "btn btn-primary",
                    disabled: *busy.read(),
                    onclick: on_send,
                    if *busy.read() { "Sending..." } else { "Sign & Send" }
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
    if trimmed.contains('.') {
        let sol: f64 = trimmed.parse().ok()?;
        if sol <= 0.0 {
            return None;
        }
        Some((sol * LAMPORTS_PER_SOL as f64) as u64)
    } else {
        let val: u64 = trimmed.parse().ok()?;
        if val == 0 {
            return None;
        }
        // For send, small integers are SOL
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
    for _ in 0..60 {
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
    Err(ffi::WalletError::Io(
        "transaction confirmation timed out".into(),
    ))
}
