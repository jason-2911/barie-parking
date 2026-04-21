#!/bin/bash
# deploy.sh — Deploy parking_client to Orange Pi
# Usage: ./deploy.sh [user@host]

TARGET=${1:-pi@192.168.1.50}
REMOTE_DIR=/home/pi/parking

echo "=== Deploying to $TARGET ==="

# Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc) || exit 1
cd ..

# Copy binary + config
ssh "$TARGET" "mkdir -p $REMOTE_DIR"
scp build/parking_client "$TARGET:$REMOTE_DIR/"
scp config.json          "$TARGET:$REMOTE_DIR/"
scp -r resources/        "$TARGET:$REMOTE_DIR/"
scp deploy/parking-client.service "$TARGET:/tmp/"

# Install
ssh "$TARGET" "
    sudo cp /tmp/parking-client.service /etc/systemd/system/
    sudo mkdir -p /etc/parking_client
    sudo cp $REMOTE_DIR/config.json /etc/parking_client/
    sudo cp $REMOTE_DIR/parking_client /usr/bin/
    sudo systemctl daemon-reload
    sudo systemctl enable parking-client
    sudo systemctl restart parking-client
    systemctl status parking-client --no-pager
"
echo "=== Deploy done ==="
