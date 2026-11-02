#!/bin/sh
# Regenerates the TEST-ONLY TLS fixtures (EC-003). Keys here protect nothing and
# must never be trusted outside the tests. Valid ~100 years so CI never expires.
# Usage: sh generate.sh   (run inside tests/tls)
set -e
D=36500
openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days $D -subj "/CN=XerahS Test CA (not for production)" \
  -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign"
issue() { # name subjectAltName
  openssl req -newkey rsa:2048 -nodes -keyout "$1.key" -out "$1.csr" -subj "/CN=$1"
  printf "subjectAltName=$2\nbasicConstraints=CA:FALSE\nextendedKeyUsage=serverAuth\nkeyUsage=digitalSignature,keyEncipherment\n" > "$1.ext"
  openssl x509 -req -in "$1.csr" -CA ca.pem -CAkey ca.key -CAcreateserial -out "$1.pem" -days $D -extfile "$1.ext"
  rm -f "$1.csr" "$1.ext"
}
issue server "IP:127.0.0.1,DNS:localhost"
issue mismatch "DNS:other.example"
openssl req -x509 -newkey rsa:2048 -nodes -keyout selfsigned.key -out selfsigned.pem -days $D -subj "/CN=selfsigned" \
  -addext "subjectAltName=IP:127.0.0.1,DNS:localhost"
rm -f ca.key ca.srl   # CA key is discarded: the fixtures cannot be extended, only regenerated
