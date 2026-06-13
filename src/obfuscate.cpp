#include "obfuscate.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include <random>

#ifdef _WIN32
#include <windows.h>
#endif

namespace obf {
namespace {

constexpr char    PREFIX[]   = "HP1$";
constexpr int     SALT_LEN   = 16;       // bytes
// Work factor: number of mixing rounds when deriving the key. Tuned so the
// derivation costs a few hundred ms on a modern core — negligible for a
// once-per-save operation, but expensive to brute-force at scale.
constexpr uint64_t KDF_ROUNDS = 1u << 20;  // ~1.05M rounds

// Compiled-in application secret. Combined with the machine id so the blob is
// useless without both this binary and the originating machine.
constexpr char APP_SECRET[] =
    "HorizonPrime//obf//v1//do-not-reuse//7f3a91c4d28e";

// ── A small 256-bit sponge-ish mixing state (FNV/xxhash-flavoured) ────────────
struct State { uint64_t h[4]; };

void absorb(State& s, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        int lane = i & 3;
        s.h[lane] ^= data[i];
        s.h[lane] *= 0x100000001b3ull;          // FNV prime
        s.h[lane] ^= (s.h[lane] >> 29);
        s.h[lane] *= 0xbf58476d1ce4e5b9ull;      // splitmix64 constant
        s.h[lane] ^= (s.h[lane] >> 32);
        // cross-lane diffusion
        s.h[(lane + 1) & 3] += s.h[lane] ^ 0x9e3779b97f4a7c15ull;
    }
}

void absorb(State& s, const std::string& str) {
    absorb(s, reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

State init_state() {
    State s{ { 0xcbf29ce484222325ull, 0x84222325cbf29ce4ull,
               0x9e3779b97f4a7c15ull, 0xff51afd7ed558ccdull } };
    return s;
}

// ── Machine-bound identifier (Windows MachineGuid; fallback to volume serial) ─
std::string machine_id() {
#ifdef _WIN32
    char buf[256] = {};
    DWORD sz = sizeof(buf);
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
            "SOFTWARE\\Microsoft\\Cryptography", 0,
            KEY_READ | KEY_WOW64_64KEY, &key) == ERROR_SUCCESS) {
        DWORD type = 0;
        LONG r = RegQueryValueExA(key, "MachineGuid", nullptr, &type,
                                  reinterpret_cast<BYTE*>(buf), &sz);
        RegCloseKey(key);
        if (r == ERROR_SUCCESS && type == REG_SZ)
            return std::string(buf);
    }
    // Fallback: system volume serial number.
    DWORD serial = 0;
    if (GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        char tmp[32];
        std::snprintf(tmp, sizeof(tmp), "vol-%08lx", (unsigned long)serial);
        return std::string(tmp);
    }
    return "unknown-machine";
#else
    return "non-windows";
#endif
}

// ── Expensive key derivation ──────────────────────────────────────────────────
// Mixes app secret + machine id + salt, then iterates the sponge KDF_ROUNDS
// times. This is the deliberately costly step ("чего стоит процессорам").
std::array<uint8_t, 32> derive_key(const uint8_t* salt, size_t salt_len) {
    State s = init_state();
    absorb(s, std::string(APP_SECRET));
    std::string mid = machine_id();
    absorb(s, mid);
    absorb(s, salt, salt_len);

    // Iterated mixing — each round folds the current state back into itself.
    for (uint64_t i = 0; i < KDF_ROUNDS; ++i) {
        uint8_t blk[32];
        std::memcpy(blk, s.h, 32);
        absorb(s, blk, 32);
        s.h[0] += i;               // counter to prevent short cycles
    }

    std::array<uint8_t, 32> key;
    std::memcpy(key.data(), s.h, 32);
    return key;
}

// ── Keystream: counter-mode reseeding of the sponge from the derived key ──────
void apply_keystream(const std::array<uint8_t, 32>& key,
                     const uint8_t* salt, size_t salt_len,
                     std::string& data) {
    uint64_t counter = 0;
    size_t   off     = 0;
    uint8_t  block[32];
    size_t   block_used = sizeof(block);  // force first refill

    for (char& c : data) {
        if (block_used == sizeof(block)) {
            State s = init_state();
            absorb(s, key.data(), key.size());
            absorb(s, salt, salt_len);
            absorb(s, reinterpret_cast<uint8_t*>(&counter), sizeof(counter));
            ++counter;
            std::memcpy(block, s.h, sizeof(block));
            block_used = 0;
        }
        c = (char)((uint8_t)c ^ block[block_used++]);
    }
    (void)off;
}

// ── Hex helpers ───────────────────────────────────────────────────────────────
std::string to_hex(const uint8_t* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out += H[p[i] >> 4];
        out += H[p[i] & 0xF];
    }
    return out;
}

bool from_hex(const std::string& hex, std::string& out) {
    if (hex.size() & 1) return false;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    out.clear();
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out += (char)((hi << 4) | lo);
    }
    return true;
}

} // namespace

// ─── Public API ───────────────────────────────────────────────────────────────
std::string encrypt(const std::string& plain) {
    if (plain.empty()) return "";

    uint8_t salt[SALT_LEN];
    std::random_device rd;
    for (int i = 0; i < SALT_LEN; ++i) salt[i] = (uint8_t)(rd() & 0xFF);

    auto key = derive_key(salt, SALT_LEN);
    std::string buf = plain;
    apply_keystream(key, salt, SALT_LEN, buf);

    return std::string(PREFIX)
         + to_hex(salt, SALT_LEN) + "$"
         + to_hex(reinterpret_cast<const uint8_t*>(buf.data()), buf.size());
}

std::string decrypt(const std::string& blob) {
    if (blob.empty()) return "";
    const std::string prefix = PREFIX;
    // Legacy / plaintext blobs (no prefix) are returned as-is for back-compat.
    if (blob.compare(0, prefix.size(), prefix) != 0) return blob;

    size_t p1 = prefix.size();
    size_t dollar = blob.find('$', p1);
    if (dollar == std::string::npos) return "";

    std::string salt_hex = blob.substr(p1, dollar - p1);
    std::string ct_hex   = blob.substr(dollar + 1);

    std::string salt_bytes, ct_bytes;
    if (!from_hex(salt_hex, salt_bytes) || salt_bytes.size() != SALT_LEN) return "";
    if (!from_hex(ct_hex, ct_bytes)) return "";

    auto key = derive_key(reinterpret_cast<const uint8_t*>(salt_bytes.data()),
                          salt_bytes.size());
    apply_keystream(key,
                    reinterpret_cast<const uint8_t*>(salt_bytes.data()),
                    salt_bytes.size(), ct_bytes);
    return ct_bytes;
}

} // namespace obf
