// Ewan Parker, created 17th September 2023.
// USB Mass Storage, backed by sparse file on remote SSH host.
// Arduino entry point and USB code.
//
// Copyright (C) 2023 Ewan Parker.
// https://www.ewan.cc

#if ARDUINO_USB_MODE
#warning This sketch should be used when USB is in OTG mode
void setup(){}
void loop(){}
#else
#include "USB.h"
#include "USBMSC.h"
#include "ssh_exec.h"
#include "ipc.h"
#include "cache.h"
#include "esp32-hal-psram.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define HWSerial Serial0
#define USBSerial Serial
#else
#define HWSerial Serial
USBCDC USBSerial;
#endif

USBMSC MSC;
struct ipc_msg msg;
//int ipc_num = 0;

// Set local disk sector configuration below.
#include "wifimsc_disk_config.h"

#if defined CONFIG_IDF_TARGET_ESP32S2
short ledPins[] = { 15, 16, 17, 18, 21, 33, 34 };
#elif defined CONFIG_IDF_TARGET_ESP32S3
// NB. GPIO 26 and 33 to 37 not available on S3 with Octal SPI.
short ledPins[] = { 47, 15, 16, 17, 18, 21, 43 };
#else
#error Please configure LED pin array
#endif

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize){
  digitalWrite(ledPins[4], HIGH);
  //HWSerial.printf("%%MSC-WRITE lba=%u offset=%u bufsize=%u\r\n", lba, offset, bufsize);
  assert(!offset);
  assert(!(bufsize%DISK_SECTOR_SIZE));

  //HWSerial.printf(
  //  "%%MEM fheap=%u lrg=%u lwm=%u fps=%u\r\n", xPortGetFreeHeapSize(),
  //  heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
  //  xPortGetMinimumEverFreeHeapSize(), ESP.getFreePsram());

  size_t b, r, total = 0; 
  while (total < bufsize)
  {
    if (bufsize - total > sizeof msg.data) b = sizeof msg.data;
    else b = bufsize - total;
    msg.host_cmd = USB_WRITE;
    msg.secsz = DISK_SECTOR_SIZE;
    msg.lba = lba;
    msg.dlen = b;
    memcpy(msg.data, buffer + total, b);
    //HWSerial.printf("%%IPC MSC Signalling SSH\r\n");
    xMessageBufferSend(usb_to_ssh, &msg, sizeof msg, portMAX_DELAY);
    //HWSerial.printf("%%IPC MSC Wait for SSH signal start %d\r\n", ipc_num);
    r = xMessageBufferReceive(ssh_to_usb, &msg, sizeof msg, portMAX_DELAY);
    //HWSerial.printf("%%IPC MSC Wait for SSH signal finish %d, sz=%d\r\n", ipc_num++, r); ipc_num++;

    total += b;
  }

  for (int l = bufsize/DISK_SECTOR_SIZE - 1; l >= 0; l--)
    put_cache_block(lba + l, buffer + DISK_SECTOR_SIZE * l);

  digitalWrite(ledPins[4], LOW);
  return bufsize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize){
  digitalWrite(ledPins[4], HIGH);
  //HWSerial.printf("%%MSC-READ lba=%u offset=%u bufsize=%u\r\n", lba, offset, bufsize);
  assert(!offset);
  assert(!(bufsize%DISK_SECTOR_SIZE));

  //HWSerial.printf(
  //  "%%MEM fheap=%u lrg=%u lwm=%u fps=%u\r\n", xPortGetFreeHeapSize(),
  //  heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
  //  xPortGetMinimumEverFreeHeapSize(), ESP.getFreePsram());

  bool allCached = true; void *data = NULL;
  for (int l = 0; l < bufsize/DISK_SECTOR_SIZE; l++)
  {
    data = get_cache_block(lba + l);
    if (!data) allCached = false;
    else memcpy(buffer + l * DISK_SECTOR_SIZE, data, DISK_SECTOR_SIZE);
  }

  size_t b, r, total = 0; 
  while (total < bufsize)
  {
    if (bufsize - total > sizeof msg.data) b = sizeof msg.data;
    else b = bufsize - total;

    if (!allCached)
    {
      msg.host_cmd = USB_READ;
      msg.secsz = DISK_SECTOR_SIZE;
      msg.lba = lba;
      msg.dlen = b;
      //HWSerial.printf("%%IPC MSC Signalling SSH\r\n");
      xMessageBufferSend(usb_to_ssh, &msg, sizeof msg, portMAX_DELAY);
      //HWSerial.printf("%%IPC MSC Wait for SSH signal start %d\r\n", ipc_num);
      r = xMessageBufferReceive(ssh_to_usb, &msg, sizeof msg, portMAX_DELAY);
      //HWSerial.printf("%%IPC MSC Wait for SSH signal finish %d, sz=%d\r\n", ipc_num++, r); ipc_num++;
      memcpy(buffer + total, msg.data, b);
    }
    total += b;
  }

  if (!allCached)
    for (int l = bufsize/DISK_SECTOR_SIZE - 1; l >= 0; l--)
      if (!get_cache_block(lba + l))
        put_cache_block(lba + l, buffer + DISK_SECTOR_SIZE * l);

  digitalWrite(ledPins[4], LOW);
  return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject){
  HWSerial.printf("%%MSC-START/STOP power=%u start=%u eject=%u\r\n", power_condition, start, load_eject);
  return true;
}

static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
  if(event_base == ARDUINO_USB_EVENTS){
    arduino_usb_event_data_t * data = (arduino_usb_event_data_t*)event_data;
    switch (event_id){
      case ARDUINO_USB_STARTED_EVENT:
        digitalWrite(ledPins[4], LOW);
        HWSerial.println("%USB PLUGGED");
        break;
      case ARDUINO_USB_STOPPED_EVENT:
        digitalWrite(ledPins[4], HIGH);
        HWSerial.println("%USB UNPLUGGED");
        break;
      case ARDUINO_USB_SUSPEND_EVENT:
        HWSerial.printf("%%USB SUSPENDED remote_wakeup_en=%u\r\n",
          data->suspend.remote_wakeup_en);
        break;
      case ARDUINO_USB_RESUME_EVENT:
        HWSerial.println("%USB RESUMED");
        break;
      
      default:
        break;
    }
  }
}

void init_comms_and_sync(void)
{
  init_ipc();

  ssh_exec_setup();
  //HWSerial.printf("%%IPC MSC Wait for SSH signal start %d\r\n", ipc_num);
  char dummy;
  xMessageBufferReceive(ssh_to_usb, &dummy, 1, portMAX_DELAY);
  //HWSerial.printf("%%IPC MSC Wait for SSH signal finish %d\r\n", ipc_num++); ipc_num++;

  // Create backing file.
  msg.host_cmd = CREATE_BACKING_FILE;
  msg.lba = DISK_SECTOR_COUNT;
  msg.secsz = DISK_SECTOR_SIZE;
  //HWSerial.printf("%%IPC MSC Signalling SSH\r\n");
  xMessageBufferSend(usb_to_ssh, &msg, sizeof msg, portMAX_DELAY);
  //HWSerial.printf("%%IPC MSC Wait for SSH signal start %d\r\n", ipc_num);
  xMessageBufferReceive(ssh_to_usb, &msg, sizeof msg, portMAX_DELAY);
  //HWSerial.printf("%%IPC MSC Wait for SSH signal finish %d\r\n", ipc_num++); ipc_num++;
}

void setup()
{
  for (short pin = 0; pin < sizeof ledPins / sizeof ledPins[0]; pin++)
  {
    digitalWrite(ledPins[pin], HIGH);
    pinMode(ledPins[pin], OUTPUT);
  }

  HWSerial.begin(115200);
  HWSerial.setDebugOutput(true);

  if (psramInit()) HWSerial.println("%CFG PSRAM found and enabled");
  init_comms_and_sync();
  uint16_t cached_sectors = init_cache(DISK_SECTOR_SIZE, DISK_SECTOR_COUNT);
  if (cached_sectors)
    HWSerial.printf("%%MEM-CACHE sectors=%u bytes=%u\r\n", cached_sectors,
      cached_sectors * DISK_SECTOR_SIZE);
  HWSerial.printf(
    "%%MEM fheap=%u lrg=%u lwm=%u fps=%u\r\n", xPortGetFreeHeapSize(),
    heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT),
    xPortGetMinimumEverFreeHeapSize(), ESP.getFreePsram());

  USB.onEvent(usbEventCallback);
  MSC.vendorID("Ewan.CC");//max 8 chars
  MSC.productID("WiFi.MSC");//max 16 chars
  MSC.productRevision("000C");//max 4 chars
  MSC.onStartStop(onStartStop);
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.begin(DISK_SECTOR_COUNT, DISK_SECTOR_SIZE);
  USBSerial.begin();
  USB.begin();
  digitalWrite(ledPins[2], LOW);
}

void loop()
{
  // Nothing to do here since controlTask has taken over.
  vTaskDelay(60000 / portTICK_PERIOD_MS);
}

#endif /* ARDUINO_USB_MODE */
