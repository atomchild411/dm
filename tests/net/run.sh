#!/bin/sh
# How the client treats Discord: tested against a stand-in for its gateway
# and API (fake_discord.py), never the real one.  Builds dm-cli for Linux in
# a container (podman or docker) and runs the tests there.
#
#   tests/net/run.sh [TEST...]      (the names: fake_discord.py --list)
set -eu
cd "$(dirname "$0")/../.."
engine=$(command -v podman || command -v docker)
vol=; [ "$(basename $engine)" = podman ] && vol=:Z
$engine build -q -t dm-net-tests tests/net > /dev/null
$engine run --rm -v "$PWD":/src$vol dm-net-tests sh -c '
	set -e
	mkdir -p /tmp/b && cd /src
	make -j16 FRONTEND=cli PREFIX_DEPS=/usr BUILD_DIR=/tmp/b/obj TARGET=/tmp/b/dm-cli > /tmp/b/make.log 2>&1 ||
		{ tail -20 /tmp/b/make.log; exit 1; }
	cd /tmp/b
	# a CA of the tests own, and a certificate from it for localhost
	openssl req -x509 -newkey rsa:2048 -nodes -keyout ca.key -out ca.pem -days 2 -subj /CN=dm-test-ca 2>/dev/null
	openssl req -newkey rsa:2048 -nodes -keyout server.key -out server.csr -subj /CN=localhost 2>/dev/null
	printf "subjectAltName=DNS:localhost\n" > san.ext
	openssl x509 -req -in server.csr -CA ca.pem -CAkey ca.key -CAcreateserial -out server.pem -days 2 -extfile san.ext 2>/dev/null
	python3 /src/tests/net/fake_discord.py --client /tmp/b/dm-cli --ca /tmp/b/ca.pem \
		--cert /tmp/b/server.pem --key /tmp/b/server.key '"$*"'
'
