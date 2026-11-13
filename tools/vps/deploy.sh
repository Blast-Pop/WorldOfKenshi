#!/usr/bin/env bash
# Sends the committed source tree to a VPS and runs setup.sh there.
#   tools/vps/deploy.sh root@HOST "<server name>" "<description>" [ssh options...]
set -euo pipefail
TARGET="$1"; NAME="${2:-World of Kenshi}"; DESCRIPTION="${3:-Dedicated server}"; shift 3 || true
cd "$(dirname "$0")/../.."
git archive --format=tar.gz -o /tmp/wok-src.tar.gz HEAD
scp "$@" /tmp/wok-src.tar.gz "$TARGET:/root/wok-src.tar.gz"
ssh "$@" "$TARGET" "mkdir -p /root/wok && tar -xzf /root/wok-src.tar.gz -C /root/wok && bash /root/wok/tools/vps/setup.sh '$NAME' '$DESCRIPTION'"
