#!/usr/bin/env bash
# Builds and installs the World of Kenshi dedicated server on a fresh Ubuntu/Debian VPS.
# Run as root from the unpacked source tree:
#   bash tools/vps/setup.sh "<server name>" "<description>" [master | http://MASTER:27801]
# "master": this server also hosts the master server list (and lists itself); a URL: announce to it.
# Opens UDP 27800 (game) and TCP 27801 (Steam login page) and installs a systemd service.
set -euo pipefail
NAME="${1:-World of Kenshi}"
DESCRIPTION="${2:-Dedicated server}"
LISTING="${3:-}"
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
PUBLIC_IP="$(curl -4 -s https://api.ipify.org || hostname -I | awk '{print $1}')"
case "$LISTING" in
  master) LIST_ARGS="--master --announce http://${PUBLIC_IP}:27801" ;;
  http*)  LIST_ARGS="--announce ${LISTING}" ;;
  *)      LIST_ARGS="" ;;
esac

export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q build-essential cmake ninja-build git curl zip unzip tar pkg-config \
  autoconf automake libtool linux-libc-dev python3

# vcpkg at the baseline pinned in vcpkg.json (same layout as on Windows: ./vcpkg).
if [ ! -x "$SRC/vcpkg/vcpkg" ]; then
  git clone -q https://github.com/microsoft/vcpkg.git "$SRC/vcpkg"
  "$SRC/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
fi

cd "$SRC"
cmake --preset linux-release
cmake --build --preset linux-release --target wok_server

install -d /opt/wok /var/lib/wok
install -m 755 build/linux-release/server/wok_server /opt/wok/wok_server
install -m 644 docs/re/towns-1.0.68.txt /opt/wok/towns.txt
id -u wok >/dev/null 2>&1 || useradd --system --home /var/lib/wok --shell /usr/sbin/nologin wok
chown -R wok:wok /var/lib/wok

cat > /etc/systemd/system/wok-server.service <<UNIT
[Unit]
Description=World of Kenshi dedicated server
After=network-online.target
Wants=network-online.target

[Service]
User=wok
WorkingDirectory=/var/lib/wok
ExecStart=/opt/wok/wok_server --db /var/lib/wok/world.sqlite --public-url http://${PUBLIC_IP}:27801 \
  --name "${NAME}" --description "${DESCRIPTION}" --spawn-file /opt/wok/towns.txt --default-spawn "The Hub" ${LIST_ARGS}
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
UNIT

if command -v ufw >/dev/null; then
  ufw allow 22/tcp >/dev/null; ufw allow 27800/udp >/dev/null; ufw allow 27801/tcp >/dev/null
fi
systemctl daemon-reload
systemctl enable wok-server
systemctl restart wok-server  # also picks up a new binary on redeploys
sleep 2
systemctl --no-pager --lines 5 status wok-server
echo "World of Kenshi server ready: ${PUBLIC_IP}:27800"
