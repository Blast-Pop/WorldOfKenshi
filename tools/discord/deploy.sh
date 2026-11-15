#!/usr/bin/env bash
# Installs or updates WOK Bot on a VPS (Ubuntu): code in /opt/wok-bot, secrets in /etc/wok-bot.env.
#   tools/discord/deploy.sh root@HOST [ssh options...]
# The first time, write /etc/wok-bot.env on the VPS (see wok-bot.env.example), then run setup_server.py.
set -euo pipefail
TARGET="$1"; shift
cd "$(dirname "$0")"
scp "$@" wok_bot.py setup_server.py requirements.txt wok-bot.service "$TARGET:/tmp/"
ssh "$@" "$TARGET" 'set -e
mkdir -p /opt/wok-bot
install -m 644 /tmp/wok_bot.py /tmp/setup_server.py /tmp/requirements.txt /opt/wok-bot/
install -m 644 /tmp/wok-bot.service /etc/systemd/system/wok-bot.service
command -v python3 >/dev/null; dpkg -s python3-venv >/dev/null 2>&1 || apt-get install -y -qq python3-venv >/dev/null
[ -x /opt/wok-bot/venv/bin/python ] || python3 -m venv /opt/wok-bot/venv
/opt/wok-bot/venv/bin/pip install -q -r /opt/wok-bot/requirements.txt
if command -v ufw >/dev/null; then ufw allow 27810/tcp >/dev/null; fi
systemctl daemon-reload
if [ -f /etc/wok-bot.env ]; then systemctl enable --now wok-bot >/dev/null 2>&1; systemctl restart wok-bot; systemctl is-active wok-bot; else echo "write /etc/wok-bot.env first"; fi'
