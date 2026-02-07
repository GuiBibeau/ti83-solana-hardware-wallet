use dioxus::prelude::*;

use crate::ffi::Calculator;
use crate::state::{ConnectionStatus, SharedCalculator, WalletState};

#[component]
pub fn ConnectionStatusIndicator() -> Element {
    let mut wallet = use_context::<Signal<WalletState>>();
    let calc = use_context::<SharedCalculator>();

    let status = wallet.read().connection_status.clone();

    let (dot_class, label) = match &status {
        ConnectionStatus::Disconnected => ("dot disconnected", "Disconnected"),
        ConnectionStatus::Connecting => ("dot connecting", "Connecting"),
        ConnectionStatus::Connected => ("dot connected", "Connected"),
        ConnectionStatus::Error(_) => ("dot error", "Error"),
    };

    let is_connected = matches!(status, ConnectionStatus::Connected);

    let calc_for_disconnect = calc.clone();
    let connect = move |_| {
        let calc = calc.clone();
        let mut wallet = wallet.clone();
        spawn(async move {
            wallet.write().connection_status = ConnectionStatus::Connecting;
            wallet.write().last_error = None;

            let result = tokio::task::spawn_blocking(move || {
                Calculator::open()
            })
            .await
            .unwrap();

            match result {
                Ok(calculator) => {
                    *calc.lock().unwrap() = Some(calculator);
                    wallet.write().connection_status = ConnectionStatus::Connected;
                }
                Err(e) => {
                    let msg = e.to_string();
                    wallet.write().connection_status = ConnectionStatus::Error(msg.clone());
                    wallet.write().last_error = Some(msg);
                }
            }
        });
    };

    let disconnect = move |_| {
        *calc_for_disconnect.lock().unwrap() = None;
        wallet.write().connection_status = ConnectionStatus::Disconnected;
        wallet.write().loaded_keypair = None;
        wallet.write().balance_lamports = None;
    };

    rsx! {
        div { class: "conn-indicator",
            span { class: dot_class }
            span { class: "conn-label", "{label}" }
            if is_connected {
                button { class: "conn-btn conn-btn-disconnect", onclick: disconnect, "Disconnect" }
            } else {
                button {
                    class: "conn-btn conn-btn-connect",
                    disabled: matches!(status, ConnectionStatus::Connecting),
                    onclick: connect,
                    "Connect"
                }
            }
        }
    }
}
