#!/usr/bin/env bash
# Sends the committed source tree to a VPS and runs setup.sh there.
#   tools/vps/deploy.sh root@HOST "<server name>" "<description>" "<master|announce url|->" [ssh options...]
# The build runs detached on the VPS (log: /root/wok-setup.log), so a dropped SSH session does not stop it.
set -euo pipefail
TARGET="$1"; NAME="${2:-World of Kenshi}"; DESCRIPTION="${3:-Dedicated server}"; LISTING="${4:--}"; shift 4 || true
cd "$(dirname "$0")/../.."
git archive --format=tar.gz -o /tmp/wok-src.tar.gz HEAD
scp "$@" /tmp/wok-src.tar.gz "$TARGET:/root/wok-src.tar.gz"
# Steam API for Linux, from the local Steamworks SDK (steam/*.zip, ignored by git), if present.
SDK_ZIP=$(ls steam/steamworks_sdk_*.zip 2>/dev/null | head -1)
if [ -n "$SDK_ZIP" ]; then
  python3 -c "import sys,zipfile; open('/tmp/libsteam_api.so','wb').write(zipfile.ZipFile(sys.argv[1]).read('sdk/redistributable_bin/linux64/libsteam_api.so'))" "$SDK_ZIP" 2>/dev/null     || python -c "import sys,zipfile; open('/tmp/libsteam_api.so','wb').write(zipfile.ZipFile(sys.argv[1]).read('sdk/redistributable_bin/linux64/libsteam_api.so'))" "$SDK_ZIP"
  scp "$@" /tmp/libsteam_api.so "$TARGET:/root/libsteam_api.so"
fi
ssh "$@" "$TARGET" "rm -f /root/wok/vcpkg/buildtrees/vcpkg-running.lock; mkdir -p /root/wok && tar -xzf /root/wok-src.tar.gz -C /root/wok && nohup bash /root/wok/tools/vps/setup.sh '$NAME' '$DESCRIPTION' '$LISTING' > /root/wok-setup.log 2>&1 < /dev/null &"
echo "Build started on $TARGET (follow it with: ssh $TARGET tail -f /root/wok-setup.log)"
