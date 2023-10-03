#!/bin/sh
# create_config.sh
# Ewan Parker, created 8th February 2018.
# Configure the SSH keys and other secrets for the firmware.
# Copyright (C) 2018, 2023 Ewan Parker.

if [ $# -eq 0 ]; then
  PROFILE=0
else
  PROFILE="$1"
fi

umask 077
mkdir -p $(dirname $0)/data/$PROFILE
cd $(dirname $0)/data/$PROFILE

if [ ! -f DEV_ID ]; then
  cat /dev/random | xxd -g0 -c8 -l8 -p -u >DEV_ID
fi

if [ ! -f DISK_SECTOR_SIZE ]; then
  echo "512" >DISK_SECTOR_SIZE
fi

if [ ! -f DISK_SECTOR_COUNT ]; then
  echo "(2 * 1024 * 32)" >DISK_SECTOR_COUNT
fi

if [ ! -f SSID ]; then
  echo "YourWiFiSSID" >SSID
  echo "YourWiFiPSK" >PSK
fi

if [ ! -f SERVER ]; then
  echo "ssh-server.example.com" >SERVER
  echo "sshuser" >USER_NAME
  rm -f SERVER_HASH
fi

if [ -f SERVER_HASH ]; then
  if [ $(date -r SERVER_HASH +%s) -lt $(date -r SERVER +%s) ]; then
    rm -f SERVER_HASH
  fi
fi


if [ ! -f BACKING_FILE ]; then
  DEV_ID=$(cat DEV_ID)
  echo "/var/tmp/WiFiMSC.$DEV_ID" >BACKING_FILE
fi

if [ ! -f ID ]; then
  DEV_USER=WiFiMSC
  DEV_ID=$(cat DEV_ID)
  USER_NAME=$(cat USER_NAME)
  SERVER=$(cat SERVER)
  KEYNAME="${DEV_USER}@$DEV_ID"
  echo "Generating key $KEYNAME" >&2
  ssh-keygen -C "$KEYNAME" -t ed25519 -f ID
  echo "To transfer key $KEYNAME to ${USER_NAME}@$SERVER please run" >&2
  echo "  ssh-copy-id -i $(dirname $0)/data/$PROFILE/ID.pub ${USER_NAME}@$SERVER" >&2
fi

if [ ! -f SERVER_HASH ]; then
  SERVER=$(cat SERVER)
  ssh-keyscan $SERVER >SERVER_HASH 2>/dev/null
fi

for CONF in DEV_ID SSID PSK SERVER USER_NAME BACKING_FILE ID.pub SERVER_HASH;
do
  grep -qPz "\n" $CONF && cat $CONF | tr "\n" "\0" >$CONF$$ && mv $CONF$$ $CONF
done


exec 1>../../../wifimsc_disk_config.h

echo "// Target-specific mass storage configuration for WiFiMSC."
echo "// Copyright (C) 2018, 2023 Ewan Parker."
echo "// Generated: `date`"
echo "// Command line: $0 $*"
echo "// Profile: $PROFILE"
echo "// Configuration script version: `git describe --always --dirty`"
echo

echo "// Use a sector size of 2048 and count of -1 for maximum size of 8 TByte."
DISK_SECTOR_SIZE=$(cat DISK_SECTOR_SIZE)
DISK_SECTOR_COUNT=$(cat DISK_SECTOR_COUNT)
echo "static const uint16_t DISK_SECTOR_SIZE = $DISK_SECTOR_SIZE;"
echo "static const uint32_t DISK_SECTOR_COUNT = $DISK_SECTOR_COUNT;"


exec 1>../../../wifimsc_ssh_config.h

echo "// Target-specific SSH keys and other secrets for WiFiMSC."
echo "// Copyright (C) 2018, 2023 Ewan Parker."
echo "// Generated: `date`"
echo "// Command line: $0 $*"
echo "// Profile: $PROFILE"
echo "// Configuration script version: `git describe --always --dirty`"
echo

xxd -i -C DEV_ID
echo ""
xxd -i -C SSID
echo ""
xxd -i -C PSK
echo ""
xxd -i -C SERVER
echo ""
xxd -i -C USER_NAME
echo ""
xxd -i -C SERVER_HASH
echo ""
xxd -i -C BACKING_FILE
echo ""
xxd -i -C ID
#echo ""
#xxd -i -C ID.pub
