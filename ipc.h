// Ewan Parker, created 17th September 2023.
// USB Mass Storage, backed by sparse file on remote SSH host.
// Inter-task communication.
//
// Copyright (C) 2023 Ewan Parker.
// https://www.ewan.cc

#include "FreeRTOS.h"
#include "freertos/message_buffer.h"

extern MessageBufferHandle_t usb_to_ssh, ssh_to_usb;

enum host_cmds { CREATE_BACKING_FILE, USB_READ, USB_WRITE };

struct ipc_msg
{
  enum host_cmds host_cmd;
  uint32_t secsz;
  uint32_t lba;
  uint16_t dlen;
  unsigned char data[4096];
};

void init_ipc(void);
