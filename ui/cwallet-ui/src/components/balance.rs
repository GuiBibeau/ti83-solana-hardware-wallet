use dioxus::prelude::*;

use crate::ffi;
use crate::state::{SharedSolanaClient, WalletState};

const LAMPORTS_PER_SOL: f64 = 1_000_000_000.0;

fn fetch_balance(
    mut wallet: Signal<WalletState>,
    solana: SharedSolanaClient,
    mut busy: Signal<bool>,
    mut error_msg: Signal<Option<String>>,
) {
    let pubkey = match &wallet.read().loaded_keypair {
        Some(kp) => kp.public_key_base58.clone(),
        None => return,
    };
    let rpc_url = wallet.read().rpc_url.clone();

    error_msg.set(None);
    busy.set(true);

    spawn(async move {
        let result = tokio::task::spawn_blocking(move || {
            let mut guard = solana.lock().unwrap();
            if guard.is_none() {
                *guard = Some(ffi::SolanaClient::new(&rpc_url)?);
            }
            let client = guard.as_mut().unwrap();
            client.get_balance(&pubkey)
        })
        .await
        .unwrap();

        busy.set(false);

        match result {
            Ok(lamports) => {
                wallet.write().balance_lamports = Some(lamports);
            }
            Err(e) => {
                error_msg.set(Some(e.to_string()));
            }
        }
    });
}

#[component]
pub fn BalancePage() -> Element {
    let wallet = use_context::<Signal<WalletState>>();
    let solana = use_context::<SharedSolanaClient>();

    let mut error_msg = use_signal(|| None::<String>);
    let mut busy = use_signal(|| false);

    let loaded = wallet.read().loaded_keypair.clone();
    let balance = wallet.read().balance_lamports;

    // Auto-fetch on mount when a keypair is loaded
    {
        let solana = solana.clone();
        use_effect(move || {
            if wallet.read().loaded_keypair.is_some() && wallet.read().balance_lamports.is_none() {
                fetch_balance(wallet, solana.clone(), busy, error_msg);
            }
        });
    }

    let on_refresh = {
        let solana = solana.clone();
        move |_| {
            fetch_balance(wallet, solana.clone(), busy, error_msg);
        }
    };

    rsx! {
        div { class: "page",
            h1 { "Balance" }

            if let Some(kp) = &loaded {
                p { class: "label", "Wallet: " span { class: "mono", "{kp.public_key_base58}" } }

                match balance {
                    Some(lamports) => rsx! {
                        div { class: "result-card",
                            p { class: "balance-large",
                                "{lamports_to_sol(lamports)} SOL"
                            }
                            p { class: "balance-small",
                                "{lamports} lamports"
                            }
                        }
                    },
                    None => rsx! {
                        p { class: "hint", "No balance fetched yet." }
                    },
                }

                button {
                    class: "btn btn-primary",
                    disabled: *busy.read(),
                    onclick: on_refresh,
                    if *busy.read() { "Fetching..." } else { "Refresh" }
                }
            } else {
                p { class: "hint", "Load a keypair first." }
            }

            if let Some(msg) = error_msg.read().as_ref() {
                p { class: "error-text", "{msg}" }
            }
        }
    }
}

fn lamports_to_sol(lamports: u64) -> String {
    let sol = lamports as f64 / LAMPORTS_PER_SOL;
    format!("{sol:.9}")
}
