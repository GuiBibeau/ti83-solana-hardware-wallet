use dioxus::prelude::*;

use crate::state::{ConnectionStatus, WalletState};
use crate::Route;

#[component]
pub fn LoadKeypairPage() -> Element {
    let wallet = use_context::<Signal<WalletState>>();

    let connected = matches!(
        wallet.read().connection_status,
        ConnectionStatus::Connected
    );
    let has_keypair = wallet.read().loaded_keypair.is_some();

    rsx! {
        div { class: "page",
            if has_keypair {
                // Show current keypair details
                if let Some(kp) = wallet.read().loaded_keypair.as_ref() {
                    h1 { "Loaded Keypair" }
                    p { class: "subtitle", "Currently active keypair from your calculator." }
                    div { class: "result-card",
                        h3 { "Details" }
                        p { class: "label", "Slot" }
                        p { class: "mono", "{kp.slot}" }
                        p { class: "label", style: "margin-top: 10px;", "Public Key" }
                        p { class: "mono", "{kp.public_key_base58}" }
                    }
                    p { class: "hint", "Use the keypair selector in the top bar to switch slots." }
                }
            } else if !connected {
                // Not connected — guide to connect
                div { class: "empty-state",
                    div { class: "empty-icon", "⊘" }
                    h2 { class: "empty-title", "No calculator connected" }
                    p { class: "empty-desc",
                        "Turn on your TI-83+ and plug it in."
                    }
                    p { class: "empty-desc", style: "margin-top: 4px;",
                        "Then hit Connect in the top-right corner."
                    }
                }
            } else {
                // Connected but no keypair — guide to load or create
                div { class: "empty-state",
                    div { class: "empty-icon", "◇" }
                    h2 { class: "empty-title", "No keypair loaded" }
                    p { class: "empty-desc",
                        "Select a slot from the keypair dropdown in the top bar to load an existing keypair."
                    }
                    p { class: "empty-desc", style: "margin-top: 12px;",
                        "Don't have a keypair yet?"
                    }
                    Link { class: "btn btn-primary", to: Route::CreateKeypair {},
                        "Create a new keypair"
                    }
                }
            }
        }
    }
}
