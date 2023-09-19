// Ewan Parker, created 17th September 2023.
// USB Mass Storage, backed by sparse file on remote SSH host.
// Inter-task communication.
//
// Copyright (C) 2023 Ewan Parker.
// https://www.ewan.cc

#include "ipc.h"

MessageBufferHandle_t usb_to_ssh, ssh_to_usb;

void init_ipc(void)
{
  usb_to_ssh = xMessageBufferCreate(sizeof (struct ipc_msg) + 4);
  ssh_to_usb = xMessageBufferCreate(sizeof (struct ipc_msg) + 4);
}
