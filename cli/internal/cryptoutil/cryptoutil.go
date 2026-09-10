// Package cryptoutil implements AES-256-GCM at-rest encryption for the CLI's
// locally-stored config secrets (Expo token, generated orchestrator
// MASTER_KEY). Mirrors the same scheme orchestrator/src/util/crypto.ts uses: a
// random 32-byte key (stored in ~/.config/ebl/machine.key, 0600) encrypts each
// value with a fresh random 12-byte IV; the stored blob is
// base64(iv || tag || ciphertext).
//
// The wire format is byte-for-byte compatible with cli/src/crypto.cpp's
// output, so a config.json written by the C++ CLI decrypts correctly here
// (and vice versa) - existing users don't lose their saved token/config across
// the migration. Go's cipher.AEAD.Seal appends the tag *after* the ciphertext
// rather than storing it separately, so Encrypt/Decrypt below rearrange bytes
// to/from the C++ layout explicitly.
package cryptoutil

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"encoding/base64"
	"errors"
	"fmt"
)

const (
	keyLen = 32
	ivLen  = 12
	tagLen = 16
)

// Key is a 256-bit AES key.
type Key [keyLen]byte

// GenerateKey returns a fresh random 256-bit key.
func GenerateKey() (Key, error) {
	var key Key
	if _, err := rand.Read(key[:]); err != nil {
		return Key{}, fmt.Errorf("failed to generate a random encryption key: %w", err)
	}
	return key, nil
}

func newGCM(key Key) (cipher.AEAD, error) {
	block, err := aes.NewCipher(key[:])
	if err != nil {
		return nil, err
	}
	return cipher.NewGCM(block)
}

// Encrypt returns base64(iv || tag || ciphertext) for plaintext under key.
func Encrypt(plaintext string, key Key) (string, error) {
	gcm, err := newGCM(key)
	if err != nil {
		return "", fmt.Errorf("AES-256-GCM encryption failed: %w", err)
	}

	iv := make([]byte, ivLen)
	if _, err := rand.Read(iv); err != nil {
		return "", fmt.Errorf("failed to generate a random IV: %w", err)
	}

	// Seal appends the tag after the ciphertext (ciphertext || tag) - split it
	// back out so the stored blob matches the C++ version's iv || tag || ciphertext.
	sealed := gcm.Seal(nil, iv, []byte(plaintext), nil)
	ciphertext := sealed[:len(sealed)-tagLen]
	tag := sealed[len(sealed)-tagLen:]

	blob := make([]byte, 0, ivLen+tagLen+len(ciphertext))
	blob = append(blob, iv...)
	blob = append(blob, tag...)
	blob = append(blob, ciphertext...)
	return base64.StdEncoding.EncodeToString(blob), nil
}

// Decrypt reverses Encrypt, verifying the GCM authentication tag.
func Decrypt(encoded string, key Key) (string, error) {
	blob, err := base64.StdEncoding.DecodeString(encoded)
	if err != nil {
		return "", fmt.Errorf("invalid base64 in encrypted value: %w", err)
	}
	if len(blob) < ivLen+tagLen {
		return "", errors.New("encrypted value is too short to be valid")
	}

	iv := blob[:ivLen]
	tag := blob[ivLen : ivLen+tagLen]
	ciphertext := blob[ivLen+tagLen:]

	gcm, err := newGCM(key)
	if err != nil {
		return "", fmt.Errorf("AES-256-GCM decryption failed: %w", err)
	}

	// Reassemble ciphertext || tag - the form gcm.Open expects.
	sealed := make([]byte, 0, len(ciphertext)+tagLen)
	sealed = append(sealed, ciphertext...)
	sealed = append(sealed, tag...)

	plaintext, err := gcm.Open(nil, iv, sealed, nil)
	if err != nil {
		return "", errors.New("failed to decrypt config value - the machine key may not match, or the value is corrupt")
	}
	return string(plaintext), nil
}
