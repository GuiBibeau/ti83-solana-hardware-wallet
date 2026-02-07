use dioxus::prelude::*;

use crate::ffi;
use crate::state::{ConnectionStatus, SharedCalculator, WalletState};

#[component]
pub fn CreateKeypairPage() -> Element {
    let wallet = use_context::<Signal<WalletState>>();
    let calc = use_context::<SharedCalculator>();

    let mut slot = use_signal(|| "Str0".to_string());
    let mut password = use_signal(String::new);
    let mut confirm = use_signal(String::new);
    let mut status_msg = use_signal(|| None::<String>);
    let mut error_msg = use_signal(|| None::<String>);
    let mut busy = use_signal(|| false);

    let connected = matches!(
        wallet.read().connection_status,
        ConnectionStatus::Connected
    );

    let on_submit = move |_| {
        let pw = password.read().clone();
        let cf = confirm.read().clone();
        let slot_val = slot.read().clone();
        let calc = calc.clone();

        if pw.is_empty() {
            error_msg.set(Some("Password cannot be empty".into()));
            return;
        }
        if pw != cf {
            error_msg.set(Some("Passwords do not match".into()));
            return;
        }

        error_msg.set(None);
        status_msg.set(None);
        busy.set(true);

        spawn(async move {
            let result = tokio::task::spawn_blocking(move || {
                // 1. Generate keypair
                let (public_key, mut private_key) = ffi::create_keypair()?;

                // 2. Encrypt private key
                let blob = ffi::encrypt_private_key(&pw, &private_key)?;

                // 3. Secure-zero private key
                ffi::secure_zero(&mut private_key);

                // 4. Store on calculator
                let mut guard = calc.lock().unwrap();
                let calculator = guard
                    .as_mut()
                    .ok_or_else(|| ffi::WalletError::NoCalc)?;
                calculator.store_keypair(&slot_val, &public_key, &blob)?;

                // 5. Return pubkey for display
                ffi::base58_encode(&public_key)
            })
            .await
            .unwrap();

            busy.set(false);

            match result {
                Ok(pubkey_b58) => {
                    status_msg.set(Some(format!("Keypair created! Public key: {pubkey_b58}")));
                    password.set(String::new());
                    confirm.set(String::new());
                }
                Err(e) => {
                    error_msg.set(Some(e.to_string()));
                }
            }
        });
    };

    rsx! {
        div { class: "page",
            h1 { "Create Keypair" }
            p { class: "subtitle", "Generate an Ed25519 keypair, encrypt it, and store on calculator." }

            div { class: "form-group",
                label { "Target slot" }
                select {
                    class: "input",
                    value: "{slot}",
                    onchange: move |e| slot.set(e.value()),
                    for i in 0..=9 {
                        option { value: "Str{i}", "Str{i}" }
                    }
                }
            }

            div { class: "form-group",
                label { "Password" }
                input {
                    class: "input",
                    r#type: "password",
                    placeholder: "Encryption password",
                    value: "{password}",
                    oninput: move |e| password.set(e.value()),
                }
            }

            div { class: "form-group",
                label { "Confirm password" }
                input {
                    class: "input",
                    r#type: "password",
                    placeholder: "Confirm password",
                    value: "{confirm}",
                    oninput: move |e| confirm.set(e.value()),
                }
            }

            button {
                class: "btn btn-primary",
                disabled: *busy.read() || !connected,
                onclick: on_submit,
                if *busy.read() { "Creating..." } else { "Create & Store" }
            }

            if !connected {
                p { class: "hint", "Connect your calculator first." }
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
