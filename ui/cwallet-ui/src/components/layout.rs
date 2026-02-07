use dioxus::prelude::*;

use super::connection_status::ConnectionStatusIndicator;
use crate::ffi;
use crate::state::{ConnectionStatus, LoadedKeypair, SharedCalculator, WalletState};
use crate::Route;

const LAMPORTS_PER_SOL: f64 = 1_000_000_000.0;

#[component]
pub fn Sidebar() -> Element {
    rsx! {
        nav { class: "sidebar",
            div { class: "sidebar-brand",
                span { class: "brand-icon", "◈" }
                span { class: "brand-text", "TI-83+ Wallet" }
            }
            div { class: "sidebar-nav",
                NavSection { label: "Wallet" }
                NavLink { to: Route::Home {}, label: "Overview", icon: "⌂" }
                NavLink { to: Route::CreateKeypair {}, label: "Create Keypair", icon: "+" }
                NavSection { label: "Solana" }
                NavLink { to: Route::Balance {}, label: "Balance", icon: "◎" }
                NavLink { to: Route::Airdrop {}, label: "Airdrop", icon: "✦" }
                NavLink { to: Route::SendSol {}, label: "Send SOL", icon: "→" }
            }
            div { class: "sidebar-footer",
                span { class: "sidebar-footer-text", "devnet" }
            }
        }
    }
}

#[component]
pub fn TopBar() -> Element {
    let wallet = use_context::<Signal<WalletState>>();
    let balance = wallet.read().balance_lamports;

    rsx! {
        header { class: "topbar",
            div { class: "topbar-left",
                KeypairDropdown {}
                if let Some(lamports) = balance {
                    div { class: "topbar-divider" }
                    div { class: "topbar-balance",
                        span { class: "topbar-label", "Balance" }
                        span { class: "topbar-value", "{format_sol(lamports)} SOL" }
                    }
                }
            }
            div { class: "topbar-right",
                ConnectionStatusIndicator {}
            }
        }
    }
}

#[component]
fn KeypairDropdown() -> Element {
    let mut wallet = use_context::<Signal<WalletState>>();
    let calc = use_context::<SharedCalculator>();

    let mut open = use_signal(|| false);
    let mut loading_slot = use_signal(|| None::<String>);
    let mut error_msg = use_signal(|| None::<String>);

    let connected = matches!(
        wallet.read().connection_status,
        ConnectionStatus::Connected
    );

    let loaded = wallet.read().loaded_keypair.clone();
    let is_loading = loading_slot.read().is_some();

    // The trigger button
    let trigger_label = if is_loading {
        "Loading...".to_string()
    } else if let Some(kp) = &loaded {
        format!("{} · {}", kp.slot, truncate_pubkey(&kp.public_key_base58))
    } else {
        "Select keypair".to_string()
    };

    rsx! {
        div { class: "kp-dropdown",
            button {
                class: if loaded.is_some() { "kp-trigger kp-trigger-active" } else { "kp-trigger" },
                disabled: !connected,
                onclick: move |_| { let cur = *open.read(); open.set(!cur); },
                if is_loading {
                    span { class: "spinner" }
                }
                span { "{trigger_label}" }
                span { class: "kp-chevron", "▾" }
            }

            if *open.read() && connected {
                div { class: "kp-menu",
                    for i in 0..=9 {
                        {
                            let slot = format!("Str{i}");
                            let is_current = loaded.as_ref().map_or(false, |kp| kp.slot == slot);
                            let is_empty_err = error_msg.read().as_ref().map_or(false, |s| *s == slot);
                            let slot_for_click = slot.clone();
                            let calc_for_click = calc.clone();
                            rsx! {
                                button {
                                    class: if is_current { "kp-menu-item kp-menu-item-active" } else { "kp-menu-item" },
                                    onclick: move |_| {
                                        if !connected || is_loading { return; }
                                        let calc = calc_for_click.clone();
                                        let slot_val = slot_for_click.clone();
                                        let slot_err = slot_for_click.clone();
                                        loading_slot.set(Some(slot_for_click.clone()));
                                        error_msg.set(None);
                                        open.set(false);
                                        spawn(async move {
                                            let result = tokio::task::spawn_blocking(move || {
                                                let mut guard = calc.lock().unwrap();
                                                let calculator = guard
                                                    .as_mut()
                                                    .ok_or_else(|| ffi::WalletError::NoCalc)?;
                                                let (public_key, blob) = calculator.fetch_keypair(&slot_val)?;
                                                let pubkey_b58 = ffi::base58_encode(&public_key)?;
                                                Ok::<_, ffi::WalletError>((slot_val, public_key, pubkey_b58, blob))
                                            })
                                            .await
                                            .unwrap();
                                            loading_slot.set(None);
                                            match result {
                                                Ok((s, public_key, pubkey_b58, blob)) => {
                                                    wallet.write().loaded_keypair = Some(LoadedKeypair {
                                                        slot: s,
                                                        public_key,
                                                        public_key_base58: pubkey_b58,
                                                        encrypted_blob: blob,
                                                    });
                                                    wallet.write().balance_lamports = None;
                                                }
                                                Err(_e) => {
                                                    error_msg.set(Some(slot_err));
                                                }
                                            }
                                        });
                                    },
                                    span { class: "kp-slot-name", "{slot}" }
                                    if is_current {
                                        if let Some(kp) = &loaded {
                                            span { class: "kp-slot-pubkey", "{truncate_pubkey(&kp.public_key_base58)}" }
                                        }
                                    }
                                    if is_empty_err {
                                        span { class: "kp-slot-empty", "Empty" }
                                    }
                                }
                            }
                        }
                    }
                }
                // Invisible backdrop to close dropdown
                div {
                    class: "kp-backdrop",
                    onclick: move |_| open.set(false),
                }
            }

            if !connected {
                // tooltip-style hint
            }
        }
    }
}

#[component]
fn NavSection(label: &'static str) -> Element {
    rsx! {
        div { class: "nav-section-label", "{label}" }
    }
}

#[component]
fn NavLink(to: Route, label: &'static str, icon: &'static str) -> Element {
    rsx! {
        Link { class: "nav-link", to: to,
            span { class: "nav-icon", "{icon}" }
            span { "{label}" }
        }
    }
}

fn truncate_pubkey(pubkey: &str) -> String {
    if pubkey.len() > 12 {
        format!("{}...{}", &pubkey[..6], &pubkey[pubkey.len() - 4..])
    } else {
        pubkey.to_string()
    }
}

fn format_sol(lamports: u64) -> String {
    let sol = lamports as f64 / LAMPORTS_PER_SOL;
    if sol == 0.0 {
        "0".to_string()
    } else if sol < 0.001 {
        format!("{sol:.9}")
    } else {
        format!("{sol:.4}")
    }
}
