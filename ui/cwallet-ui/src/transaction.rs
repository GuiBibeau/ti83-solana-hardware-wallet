//! Solana transaction building — Rust reimplementation of
//! `solana_build_transfer_transaction()` from main.c.

use crate::ffi::{self, WalletError};

const SYSTEM_PROGRAM_ID: [u8; 32] = [0u8; 32];
const MEMO_PROGRAM_BASE58: &str = "MemoSq4gqABAXKb96qnH8TysNcWxMyWCqXgDLGmfcHr";

const MAX_MESSAGE_LEN: usize = 512;
const MAX_TRANSACTION_LEN: usize = 1024;
const MAX_MEMO_LENGTH: usize = 120;

/// Result of building and signing a transaction.
pub struct SignedTransaction {
    pub base64: String,
    pub signature_base58: String,
}

/// Build, sign, and encode a SOL transfer transaction with an optional memo.
///
/// This mirrors the C implementation exactly — same byte layout, same signing
/// order — to ensure on-chain compatibility.
pub fn build_transfer(
    from_public_key: &[u8; 32],
    to_public_key: &[u8; 32],
    lamports: u64,
    recent_blockhash_base58: &str,
    private_key: &[u8; 64],
    memo: Option<&str>,
) -> Result<SignedTransaction, WalletError> {
    let memo = memo.filter(|m| !m.is_empty());
    if let Some(m) = memo {
        if m.len() > MAX_MEMO_LENGTH {
            return Err(WalletError::Io(format!(
                "memo too long: {} > {MAX_MEMO_LENGTH}",
                m.len()
            )));
        }
    }

    // Decode blockhash
    let blockhash_bytes = ffi::base58_decode(recent_blockhash_base58)?;
    if blockhash_bytes.len() != 32 {
        return Err(WalletError::Io("invalid blockhash length".into()));
    }
    let blockhash: [u8; 32] = blockhash_bytes.try_into().unwrap();

    // Decode memo program ID if needed
    let memo_program_id = if memo.is_some() {
        let bytes = ffi::base58_decode(MEMO_PROGRAM_BASE58)?;
        if bytes.len() != 32 {
            return Err(WalletError::Io("invalid memo program ID".into()));
        }
        Some(<[u8; 32]>::try_from(bytes).unwrap())
    } else {
        None
    };

    let include_memo = memo.is_some();
    let account_key_count: usize = if include_memo { 4 } else { 3 };
    let instruction_count: usize = if include_memo { 2 } else { 1 };
    let readonly_unsigned: u8 = if include_memo { 2 } else { 1 };

    // --- Build message ---
    let mut msg = Vec::with_capacity(MAX_MESSAGE_LEN);

    // Header
    msg.push(1); // num_required_signatures
    msg.push(0); // num_readonly_signed
    msg.push(readonly_unsigned);

    // Account keys
    append_shortvec(&mut msg, account_key_count);
    msg.extend_from_slice(from_public_key);
    msg.extend_from_slice(to_public_key);
    msg.extend_from_slice(&SYSTEM_PROGRAM_ID);
    if let Some(ref memo_id) = memo_program_id {
        msg.extend_from_slice(memo_id);
    }

    // Recent blockhash
    msg.extend_from_slice(&blockhash);

    // Instruction count
    append_shortvec(&mut msg, instruction_count);

    // --- Transfer instruction ---
    msg.push(2); // program_id index (System Program)
    append_shortvec(&mut msg, 2); // 2 account indexes
    msg.push(0); // from account index
    msg.push(1); // to account index

    // Instruction data: 4-byte type (2 = Transfer) + 8-byte lamports LE
    let mut instruction_data = [0u8; 12];
    instruction_data[0] = 2; // Transfer instruction type
    instruction_data[4..12].copy_from_slice(&lamports.to_le_bytes());
    append_shortvec(&mut msg, instruction_data.len());
    msg.extend_from_slice(&instruction_data);

    // --- Optional memo instruction ---
    if let Some(memo_text) = memo {
        let memo_bytes = memo_text.as_bytes();
        let memo_program_index = (account_key_count - 1) as u8;
        msg.push(memo_program_index);
        append_shortvec(&mut msg, 0); // 0 account indexes
        append_shortvec(&mut msg, memo_bytes.len());
        msg.extend_from_slice(memo_bytes);
    }

    // --- Sign the message ---
    let signature = ffi::ed25519_sign(&msg, from_public_key, private_key);

    // --- Build transaction: [sig_count][signature][message] ---
    let mut tx = Vec::with_capacity(MAX_TRANSACTION_LEN);
    append_shortvec(&mut tx, 1); // 1 signature
    tx.extend_from_slice(&signature);
    tx.extend_from_slice(&msg);

    // --- Encode ---
    let base64 = ffi::base64_encode(&tx)?;
    let signature_base58 = ffi::base58_encode(&signature)?;

    // Secure-zero sensitive data
    // (msg and tx contain the message but not the private key — signature is public)

    Ok(SignedTransaction {
        base64,
        signature_base58,
    })
}

/// Append a compact-u16 encoded value (Solana "shortvec").
fn append_shortvec(buf: &mut Vec<u8>, mut value: usize) {
    loop {
        let mut byte = (value & 0x7F) as u8;
        value >>= 7;
        if value != 0 {
            byte |= 0x80;
        }
        buf.push(byte);
        if value == 0 {
            break;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_shortvec_encoding() {
        let mut buf = Vec::new();
        append_shortvec(&mut buf, 0);
        assert_eq!(buf, vec![0]);

        buf.clear();
        append_shortvec(&mut buf, 1);
        assert_eq!(buf, vec![1]);

        buf.clear();
        append_shortvec(&mut buf, 127);
        assert_eq!(buf, vec![127]);

        buf.clear();
        append_shortvec(&mut buf, 128);
        assert_eq!(buf, vec![0x80, 0x01]);

        buf.clear();
        append_shortvec(&mut buf, 0x3FFF);
        assert_eq!(buf, vec![0xFF, 0x7F]);
    }
}
