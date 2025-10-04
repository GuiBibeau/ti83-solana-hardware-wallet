#include "ed25519.h"
#include "ge.h"

/* see http://crypto.stackexchange.com/a/6215/4697 */
void ed25519_add_scalar(unsigned char *public_key, unsigned char *private_key, const unsigned char *scalar) {
    unsigned char n[32];
    ge_p3 nB;
    ge_p1p1 A_p1p1;
    ge_p3 A;
    ge_p3 public_key_unpacked;
    ge_cached T;
    int i;

    /* copy the scalar and clear highest bit */
    for (i = 0; i < 31; ++i) {
        n[i] = scalar[i];
    }
    n[31] = scalar[31] & 127;

    /* private key updates are not supported with the 32-byte seed representation */
    if (private_key) {
        return;
    }

    /* public key: A = nB + T */
    if (public_key) {
        /* unpack public key into T */
        ge_frombytes_negate_vartime(&public_key_unpacked, public_key);
        fe_neg(public_key_unpacked.X, public_key_unpacked.X); /* undo negate */
        fe_neg(public_key_unpacked.T, public_key_unpacked.T); /* undo negate */
        ge_p3_to_cached(&T, &public_key_unpacked);

        /* calculate n*B */
        ge_scalarmult_base(&nB, n);

        /* A = n*B + T */
        ge_add(&A_p1p1, &nB, &T);
        ge_p1p1_to_p3(&A, &A_p1p1);

        /* pack public key */
        ge_p3_tobytes(public_key, &A);
    }
}
