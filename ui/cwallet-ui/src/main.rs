#![allow(non_snake_case)]

mod components;
mod ffi;
mod state;
mod transaction;

use std::sync::{Arc, Mutex};

use dioxus::prelude::*;

use state::{ConnectionStatus, SharedCalculator, SharedSolanaClient, WalletState};

const STYLE: &str = include_str!("../assets/style.css");

#[derive(Routable, Clone, PartialEq)]
enum Route {
    #[layout(Layout)]
    #[route("/")]
    Home {},
    #[route("/create")]
    CreateKeypair {},
    #[route("/load")]
    LoadKeypair {},
    #[route("/airdrop")]
    Airdrop {},
    #[route("/balance")]
    Balance {},
    #[route("/send")]
    SendSol {},
}

fn main() {
    // Initialize TI calculator libraries (must happen before any calc calls)
    ffi::init_ti_libraries();

    dioxus::launch(App);
}

#[component]
fn App() -> Element {
    // Provide shared state to all components
    use_context_provider(|| Signal::new(WalletState::default()));
    use_context_provider::<SharedCalculator>(|| Arc::new(Mutex::new(None)));
    use_context_provider::<SharedSolanaClient>(|| Arc::new(Mutex::new(None)));

    rsx! {
        document::Style { {STYLE} }
        Router::<Route> {}
    }
}

// ---------------------------------------------------------------------------
// Layout — sidebar + content
// ---------------------------------------------------------------------------

#[component]
fn Layout() -> Element {
    rsx! {
        div { class: "app-container",
            components::layout::Sidebar {}
            div { class: "main-panel",
                components::layout::TopBar {}
                div { class: "main-content",
                    Outlet::<Route> {}
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Route components — thin wrappers around the real components
// ---------------------------------------------------------------------------

#[component]
fn Home() -> Element {
    let wallet = use_context::<Signal<WalletState>>();
    let connected = matches!(
        wallet.read().connection_status,
        ConnectionStatus::Connected
    );

    rsx! {
        div { class: "page",
            h1 { "TI-83+ Wallet" }
            p { class: "subtitle", "Solana Hardware Wallet" }
            if !connected {
                div { class: "connect-nudge",
                    p { class: "empty-desc",
                        "Turn on your TI-83+ and plug it in, then hit Connect."
                    }
                }
            } else {
                p { class: "hint",
                    "Select a keypair slot from the dropdown above to get started."
                }
            }
        }
    }
}

#[component]
fn CreateKeypair() -> Element {
    rsx! { components::create_keypair::CreateKeypairPage {} }
}

#[component]
fn LoadKeypair() -> Element {
    rsx! { components::load_keypair::LoadKeypairPage {} }
}

#[component]
fn Airdrop() -> Element {
    rsx! { components::airdrop::AirdropPage {} }
}

#[component]
fn Balance() -> Element {
    rsx! { components::balance::BalancePage {} }
}

#[component]
fn SendSol() -> Element {
    rsx! { components::send_sol::SendSolPage {} }
}
