
#!/bin/bash

set -e

trap cleanup EXIT INT TERM

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info() {
	echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
	echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
	echo -e "${RED}[ERROR]${NC} $1"
}

check_deps() {
	if ! command -v curl &> /dev/null; then
		error "curl is required, but not installed"
		exit 1
	fi		
}

cleanup() {
	info "Cleaning up..."

	rm -rf "$TMPDIR"
}

main() {
	info "Starting ErrorStringFinder runner..."

	check_deps

	URL="https://github.com/notscreenshare/ErrorStringFinder/releases/download/v1.0.0/error-string-finder"
	
	TMPDIR=$(mktemp -d)
	
	FILE_NAME=."$(basename $URL)"
	FILE="$TMPDIR/$FILE_NAME"
	
	info "Downloading from $URL to $FILE"
	curl -sL $URL -o $FILE
	
	chmod +x "$FILE" 2>/dev/null || true

	warn "Password is required, because user-level has no access to processes memory"
	if ! sudo -E "$FILE"; then
		error "ErrorStringFinder exited with error code $?"
	else
		info "ErrorStringFinder executed successfully"
	fi
}

main
