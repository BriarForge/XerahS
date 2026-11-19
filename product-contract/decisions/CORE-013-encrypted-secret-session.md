# CORE-013: encrypted Secret Service session negotiation

Decision: CORE-013-ENCRYPTED-SECRET-SESSION-2026-11-19, resolved by the loop
under the delegated owner-block policy on 2026-11-19. This is review fix A3 of
the completed ROOT-SECURITY-001 review (PASS WITH FIXES) of the Secret Service
credential store.
Contract: 0.4.0; CORE-PLATFORM-001: 0.6.0; SETTINGS-CATALOG-001: 0.1.0.
Requirements: CORE-013, SETCAT-007.

## Problem

The store negotiated only the plain session algorithm, so upload secrets
crossed the user's session bus unencrypted - readable by any process attached
to that bus. The review required negotiating the encrypted algorithm first and
falling back to plain only when the service does not support it. The Secret
Service wire algorithm (`dh-ietf1024-sha256-aes128-cbc-pkcs7`) requires
Diffie-Hellman, HKDF-SHA256 and AES-128-CBC primitives no Qt module provides,
so a crypto dependency had to be chosen.

## Choice

libgcrypt, dynamically linked, optional at build time. It is the same library
the reference implementation family uses (libsecret links it for this exact
algorithm), it is LGPL-2.1+ (verified on the project's own COPYING.LIB, GPL-v3
compatible for dynamic linking), free, account-free and packaged in Arch
(`libgcrypt`, checked 1.12.4) and Debian. The exchange follows the published
reference byte for byte so a stock GNOME Keyring or KeePPassXC daemon
interoperates:

- the ietf-ike-grp-modp-1024 group (generator 2) from libsecret's egg-dh.c;
- public keys as unsigned big-endian MPI bytes (GCRYMPI_FMT_USG), the shared
  secret zero-left-padded to the 128-byte prime length;
- the session key as RFC 5869 HKDF-SHA256 with no salt and no info, 16 bytes;
- secret payloads as AES-128-CBC with PKCS#7 padding, the random IV carried in
  the secret structure's parameters field;
- plain only on an explicit org.freedesktop.DBus.Error.NotSupported.

Sources (fetched and followed 2026-11-19):
https://raw.githubusercontent.com/GNOME/libsecret/master/libsecret/secret-session.c,
https://raw.githubusercontent.com/GNOME/libsecret/master/egg/egg-dh.c,
https://raw.githubusercontent.com/GNOME/libsecret/master/egg/egg-dh-libgcrypt.c,
https://raw.githubusercontent.com/GNOME/gnome-keyring/master/egg/egg-hkdf.c,
https://raw.githubusercontent.com/gpg/libgcrypt/master/COPYING.LIB.

## Impact

No SPEC text changes: the contract mandates the Secret Service store, not a
session algorithm, so the contract version stays 0.4.0; the dependency record
of authority is `platforms/linux/linux-qt/DEPENDENCIES.md`. A build without
libgcrypt keeps the plain-only transport with everything else working; that is
not a platform waiver, and distributions qualify the dependency for encrypted
sessions. The fake service in `SecretServiceCredentialStoreTest` implements the
daemon side of the exchange independently, so the tests prove both
implementations of the published algorithm agree (encrypted-first negotiation,
ciphertext-only transport, plain fallback on NotSupported, corrupt-ciphertext
refusal); live interoperation with a real daemon remains desktop qualification.
The new crypto code is security-sensitive and requires the ROOT-SECURITY-001
independent second-model review before any release claim.
