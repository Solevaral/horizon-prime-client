#pragma once
#include <string>

// ─── Single-shot password obfuscation ────────────────────────────────────────
// Not a substitute for a real secret store: the key is derived locally, so a
// determined attacker with this source and the user's machine can recover it.
// What it buys us:
//   * the saved blob is NOT human-readable (no plaintext password on disk),
//   * it is machine-bound (copying login.cfg to another PC yields garbage),
//   * deriving the key is deliberately CPU-expensive (a work factor of many
//     hashing rounds), so brute-forcing the obfuscation is costly even for a
//     multi-core machine — done once per save/load, imperceptible to the user.
//
// Format on disk: "HP1$" + <hex salt(16B)> + "$" + <hex ciphertext>.

namespace obf {

// Returns the obfuscated, hex-encoded blob for `plain` (empty in -> empty out).
std::string encrypt(const std::string& plain);

// Reverses encrypt(). Returns "" if the blob is malformed or from another
// machine. Plain (legacy, non-prefixed) strings are returned unchanged so old
// login.cfg files still work.
std::string decrypt(const std::string& blob);

} // namespace obf
