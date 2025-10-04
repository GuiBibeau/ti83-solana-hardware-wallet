#include <string.h>

#include "ed25519.h"
#include "sha512.h"
#include "ge.h"

static void ed25519_expand_seed(unsigned char expanded[64], const unsigned char seed[32]) {
    sha512(seed, 32, expanded);
    expanded[0] &= 248;
    expanded[31] &= 63;
    expanded[31] |= 64;
}

void ed25519_create_keypair(unsigned char *public_key, unsigned char *private_key, const unsigned char *seed) {
    unsigned char expanded_seed[64];
    ge_p3 A;

    ed25519_expand_seed(expanded_seed, seed);
    ge_scalarmult_base(&A, expanded_seed);
    ge_p3_tobytes(public_key, &A);

    if (private_key != NULL) {
        memcpy(private_key, seed, 32);
    }
}

void ed25519_derive_public_key(unsigned char *public_key, const unsigned char *private_key) {
    unsigned char expanded_seed[64];
    ge_p3 A;

    ed25519_expand_seed(expanded_seed, private_key);
    ge_scalarmult_base(&A, expanded_seed);
    ge_p3_tobytes(public_key, &A);
}
