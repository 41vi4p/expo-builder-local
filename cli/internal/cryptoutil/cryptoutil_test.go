package cryptoutil

import "testing"

func testKey() Key {
	var key Key
	for i := range key {
		key[i] = byte(i)
	}
	return key
}

func TestEncryptDecryptRoundTrip(t *testing.T) {
	key := testKey()
	plaintext := "a saved expo token, or a master key blob"

	encoded, err := Encrypt(plaintext, key)
	if err != nil {
		t.Fatalf("Encrypt: %v", err)
	}
	decoded, err := Decrypt(encoded, key)
	if err != nil {
		t.Fatalf("Decrypt: %v", err)
	}
	if decoded != plaintext {
		t.Errorf("decoded = %q, want %q", decoded, plaintext)
	}
}

func TestEncryptProducesDifferentCiphertextEachTime(t *testing.T) {
	// A fresh random IV per call means encrypting the same plaintext twice
	// must not produce the same blob (this also guards against an
	// accidentally-zeroed/reused IV, a real GCM security requirement).
	key := testKey()
	a, err := Encrypt("same plaintext", key)
	if err != nil {
		t.Fatalf("Encrypt: %v", err)
	}
	b, err := Encrypt("same plaintext", key)
	if err != nil {
		t.Fatalf("Encrypt: %v", err)
	}
	if a == b {
		t.Error("expected two encryptions of the same plaintext to differ (fresh IV each time)")
	}
}

func TestDecryptFailsWithWrongKey(t *testing.T) {
	key := testKey()
	var wrongKey Key // all zeros - different from testKey()

	encoded, err := Encrypt("secret", key)
	if err != nil {
		t.Fatalf("Encrypt: %v", err)
	}
	if _, err := Decrypt(encoded, wrongKey); err == nil {
		t.Error("expected an error decrypting with the wrong key")
	}
}

func TestDecryptFailsOnTamperedCiphertext(t *testing.T) {
	key := testKey()
	encoded, err := Encrypt("secret", key)
	if err != nil {
		t.Fatalf("Encrypt: %v", err)
	}
	tampered := []byte(encoded)
	// Flip a byte well past the iv+tag prefix, inside the base64 ciphertext
	// portion, so the GCM tag check catches the tamper.
	tampered[len(tampered)-1] = tampered[len(tampered)-1] ^ 1
	if _, err := Decrypt(string(tampered), key); err == nil {
		t.Error("expected an error decrypting tampered ciphertext")
	}
}

func TestDecryptFailsOnTooShortBlob(t *testing.T) {
	key := testKey()
	if _, err := Decrypt("dG9vc2hvcnQ=", key); err == nil { // base64("tooshort"), well under iv+tag length
		t.Error("expected an error for a too-short blob")
	}
}

func TestGenerateKeyProducesDistinctKeys(t *testing.T) {
	a, err := GenerateKey()
	if err != nil {
		t.Fatalf("GenerateKey: %v", err)
	}
	b, err := GenerateKey()
	if err != nil {
		t.Fatalf("GenerateKey: %v", err)
	}
	if a == b {
		t.Error("expected two generated keys to differ")
	}
}

// TestCrossCompatibleWithCppImplementation locks in wire compatibility with
// cli/src/crypto.cpp's stored blob format (base64(iv || tag || ciphertext)) -
// verified bidirectionally by hand against the actual C++ implementation
// before this test was written (encrypt in C++ / decrypt in Go and vice
// versa, both round-tripped correctly). This fixed blob, produced by the C++
// binary for plaintext "hello from cpp" under the all-sequential test key
// (0,1,2,...,31), must keep decrypting to the same plaintext here - a failure
// means an existing user's ~/.config/ebl/config.json would stop decrypting
// after upgrading from the C++ CLI to this one.
func TestCrossCompatibleWithCppImplementation(t *testing.T) {
	key := testKey()
	const cppBlob = "w+TUsgmFmDGd1M/GQcfxcvJseG8LEzGTDskOGs09DCAWoprb4wXOxgZv"
	const wantPlaintext = "hello from cpp"

	got, err := Decrypt(cppBlob, key)
	if err != nil {
		t.Fatalf("Decrypt(cppBlob): %v", err)
	}
	if got != wantPlaintext {
		t.Errorf("got %q, want %q", got, wantPlaintext)
	}
}
