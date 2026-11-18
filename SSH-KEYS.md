# SSH keys — XSSH v11.6.29

Based on v11.6.28; MP3 streaming and fast rewind code is unchanged.

1. Create a dedicated RSA PEM key on your computer:

   ssh-keygen -t rsa -b 3072 -m PEM -f xbox_rsa

   Choose a passphrase when prompted (an empty passphrase is also supported).
2. Install xbox_rsa.pub in the remote account's ~/.ssh/authorized_keys, with the
   usual server-side directory/file permissions. Keep the private key private.
3. Copy xbox_rsa to the Xbox, for example beside default.xbe as D:\xbox_rsa.
4. Enter D:\xbox_rsa in KEY FILE, or set private_key=D:\xbox_rsa in xssh.cfg.
5. Enter the key passphrase in PASSPHRASE, leaving it empty for an unencrypted
   key. Verify the server fingerprint as usual, then connect.

Leave KEY FILE empty to use the existing PASSWORD login. With a key selected,
XSSH never falls back to password authentication. A .pub file is not needed on
Xbox: libssh2 derives the public key from the private RSA key. The private key
itself is not transmitted. Passwords/passphrases are masked, not saved to config
or logs, and cleared after successful authentication or failed/cancelled login.

Supported scope: RSA PEM files, including passphrase-encrypted RSA PEM generated
by the command above. This pinned libssh2 Mbed TLS backend derives RSA public
keys only. Ed25519 and modern OPENSSH PRIVATE KEY containers are not supported
by this login path. Do not rename an OpenSSH key and expect that to convert it.
To convert a COPY of an existing RSA key, use ssh-keygen -p -m PEM -f COPY.
RSA keys can use RSA-SHA2 signatures; this release does not enable RSA/SHA-1.

The login form now has five rows; KEY FILE is between USERNAME and the masked
secret. B/Enter advances and arrow keys navigate. The path is limited to 159
characters; config values are unquoted. Backspace can clear a configured path
when switching to password login. Config prefill remains optional.

Validation: changed main.c and connect_form.c compiled for the Xbox target with
actual nxdk/libssh2 headers, -Wall -Wextra -Werror. Native auth branch tests cover
password/key dispatch, missing/rejected key files, no fallback, empty passphrase,
secret clearing, and form input. Form/host-trust, keyfile build-config, and release
marker regression checks passed. Full XBE linking and real SSH key login on Xbox
remain unverified; mocked transport tests do not prove server interoperability.
