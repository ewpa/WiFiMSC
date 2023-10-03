// Ewan Parker, created 22nd April 2020.
// USB Mass Storage, backed by sparse file on remote SSH host.
// SSH channel exec routines.
//
// Copyright (C) 2016–2023 Ewan Parker.
// https://www.ewan.cc

// Set local WiFi credentials below.
#include "wifimsc_ssh_config.h"

// Stack size needed to run SSH, etc.
const unsigned int configSTACK = 31200;

#include "Arduino.h"
#include "IPv6Address.h"
#include "WiFi.h"
#include "ipc.h"
// Include the Arduino library.
#include "libssh_esp32.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define HWSerial Serial0
#else
#define HWSerial Serial
#endif

#include <libssh/libssh.h>

volatile bool wifiPhyConnected;

// Timing and timeout configuration.
#define WIFI_TIMEOUT_S 10
#define NET_WAIT_MS 100

// Networking state of this esp32 device.
typedef enum
{
  STATE_NEW,
  STATE_PHY_CONNECTED,
  STATE_WAIT_IPADDR,
  STATE_GOT_IPADDR,
  STATE_OTA_UPDATING,
  STATE_OTA_COMPLETE,
  STATE_LISTENING,
  STATE_TCP_DISCONNECTED
} devState_t;

static volatile devState_t devState;
static volatile bool gotIpAddr, gotIp6Addr;

#include "SPIFFS.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"

void createFile(char *fileName, unsigned char *buffer, size_t len)
{
  File fDest = SPIFFS.open(fileName, "w");
  size_t w = fDest.write(buffer, len);
  fDest.close();
  HWSerial.printf("%%CFG-CREATE file=%s length=%d.\r\n", fileName, w);
}

/*
 * knownhosts.c
 * This file contains an example of how verify the identity of a
 * SSH server using libssh
 */

/*
Copyright 2003-2009 Aris Adamantiadis

This file is part of the SSH Library

You are free to copy this file, modify it in any way, consider it being public
domain. This does not apply to the rest of the library though, but it is
allowed to cut-and-paste working code from this file to any license of
program.
The goal is to show the API in action. It's not a reference on how terminal
clients must be made or how a client should react.
 */

#include "libssh_esp32_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libssh/priv.h"
#include <libssh/libssh.h>

#ifdef _WIN32
#define strncasecmp _strnicmp
#endif

int verify_knownhost(ssh_session session)
{
    enum ssh_known_hosts_e state;
    char buf[10];
    unsigned char *hash = NULL;
    size_t hlen;
    ssh_key srv_pubkey;
    int rc;

    rc = ssh_get_server_publickey(session, &srv_pubkey);
    if (rc < 0) {
        return -1;
    }

    rc = ssh_get_publickey_hash(srv_pubkey,
                                SSH_PUBLICKEY_HASH_SHA256,
                                &hash,
                                &hlen);
    ssh_key_free(srv_pubkey);
    if (rc < 0) {
        return -1;
    }

    state = ssh_session_is_known_server(session);

    switch(state) {
    case SSH_KNOWN_HOSTS_CHANGED:
        fprintf(stderr,"Host key for server changed : server's one is now :\n");
        ssh_print_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);
        ssh_clean_pubkey_hash(&hash);
        fprintf(stderr,"For security reason, connection will be stopped\n");
        return -1;
    case SSH_KNOWN_HOSTS_OTHER:
        fprintf(stderr,"The host key for this server was not found but an other type of key exists.\n");
        fprintf(stderr,"An attacker might change the default server key to confuse your client"
                "into thinking the key does not exist\n"
                "We advise you to rerun the client with -d or -r for more safety.\n");
        return -1;
    case SSH_KNOWN_HOSTS_NOT_FOUND:
        fprintf(stderr,"Could not find known host file. If you accept the host key here,\n");
        fprintf(stderr,"the file will be automatically created.\n");
        /* fallback to SSH_SERVER_NOT_KNOWN behavior */
        FALL_THROUGH;
    case SSH_SERVER_NOT_KNOWN:
        fprintf(stderr,
                "The server is unknown. Do you trust the host key (yes/no)?\n");
        ssh_print_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);

        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            ssh_clean_pubkey_hash(&hash);
            return -1;
        }
        if(strncasecmp(buf,"yes",3)!=0){
            ssh_clean_pubkey_hash(&hash);
            return -1;
        }
        fprintf(stderr,"This new key will be written on disk for further usage. do you agree ?\n");
        if (fgets(buf, sizeof(buf), stdin) == NULL) {
            ssh_clean_pubkey_hash(&hash);
            return -1;
        }
        if(strncasecmp(buf,"yes",3)==0){
            rc = ssh_session_update_known_hosts(session);
            if (rc != SSH_OK) {
                ssh_clean_pubkey_hash(&hash);
                fprintf(stderr, "error %s\n", strerror(errno));
                return -1;
            }
        }

        break;
    case SSH_KNOWN_HOSTS_ERROR:
        ssh_clean_pubkey_hash(&hash);
        fprintf(stderr,"%s",ssh_get_error(session));
        return -1;
    case SSH_KNOWN_HOSTS_OK:
        break; /* ok */
    }

    ssh_clean_pubkey_hash(&hash);

    return 0;
}

/*
 * authentication.c
 * This file contains an example of how to do an authentication to a
 * SSH server using libssh
 */

/*
Copyright 2003-2009 Aris Adamantiadis

This file is part of the SSH Library

You are free to copy this file, modify it in any way, consider it being public
domain. This does not apply to the rest of the library though, but it is
allowed to cut-and-paste working code from this file to any license of
program.
The goal is to show the API in action. It's not a reference on how terminal
clients must be made or how a client should react.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libssh/libssh.h>

static int auth_keyfile(ssh_session session, char* keyfile)
{
    ssh_key key = NULL;
    char pubkey[132] = {0}; // +".pub"
    int rc;

    snprintf(pubkey, sizeof(pubkey), "%s.pub", keyfile);

    rc = ssh_pki_import_pubkey_file( pubkey, &key);

    if (rc != SSH_OK)
        return SSH_AUTH_DENIED;

    rc = ssh_userauth_try_publickey(session, NULL, key);

    ssh_key_free(key);

    if (rc!=SSH_AUTH_SUCCESS)
        return SSH_AUTH_DENIED;

    rc = ssh_pki_import_privkey_file(keyfile, NULL, NULL, NULL, &key);

    if (rc != SSH_OK)
        return SSH_AUTH_DENIED;

    rc = ssh_userauth_publickey(session, NULL, key);
    ssh_key_free(key);

    return rc;
}


static void error(ssh_session session)
{
    fprintf(stderr,"Authentication failed: %s\n",ssh_get_error(session));
}

int authenticate_console(ssh_session session)
{
    int rc;
    int method;
    char *banner;

    // Try to authenticate
    rc = ssh_userauth_none(session, NULL);
    if (rc == SSH_AUTH_ERROR) {
        error(session);
        return rc;
    }

    method = ssh_userauth_list(session, NULL);
    while (rc != SSH_AUTH_SUCCESS) {
        if (method & SSH_AUTH_METHOD_GSSAPI_MIC){
            rc = ssh_userauth_gssapi(session);
            if(rc == SSH_AUTH_ERROR) {
                error(session);
                return rc;
            } else if (rc == SSH_AUTH_SUCCESS) {
                break;
            }
        }
        // Try to authenticate with public key first
        if (method & SSH_AUTH_METHOD_PUBLICKEY) {
            rc = ssh_userauth_publickey_auto(session, NULL, NULL);
            if (rc == SSH_AUTH_ERROR) {
                error(session);
                return rc;
            } else if (rc == SSH_AUTH_SUCCESS) {
                break;
            }
        }
        {
            char buffer[128] = {0};
            char *p = NULL;

            printf("Automatic pubkey failed. "
                   "Do you want to try a specific key? (y/n)\n");
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                break;
            }
            if ((buffer[0]=='Y') || (buffer[0]=='y')) {
                printf("private key filename: ");

                if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                    return SSH_AUTH_ERROR;
                }

                buffer[sizeof(buffer) - 1] = '\0';
                if ((p = strchr(buffer, '\n'))) {
                    *p = '\0';
                }

                rc = auth_keyfile(session, buffer);

                if(rc == SSH_AUTH_SUCCESS) {
                    break;
                }
                fprintf(stderr, "failed with key\n");
            }
        }

        //// Try to authenticate with keyboard interactive";
        //if (method & SSH_AUTH_METHOD_INTERACTIVE) {
        //}

        //// Try to authenticate with password
        //if (method & SSH_AUTH_METHOD_PASSWORD) {
        //}
    }

    return rc;
}

/*
 * connect_ssh.c
 * This file contains an example of how to connect to a
 * SSH server using libssh
 */

/*
Copyright 2009 Aris Adamantiadis

This file is part of the SSH Library

You are free to copy this file, modify it in any way, consider it being public
domain. This does not apply to the rest of the library though, but it is
allowed to cut-and-paste working code from this file to any license of
program.
The goal is to show the API in action. It's not a reference on how terminal
clients must be made or how a client should react.
 */

#include <libssh/libssh.h>
#include <stdio.h>

ssh_session connect_ssh(const char *host, const char *user,int verbosity){
  ssh_session session;
  int auth=0;

  session=ssh_new();
  if (session == NULL) {
    return NULL;
  }

  if(user != NULL){
    if (ssh_options_set(session, SSH_OPTIONS_USER, user) < 0) {
      ssh_free(session);
      return NULL;
    }
  }

  if (ssh_options_set(session, SSH_OPTIONS_HOST, host) < 0) {
    ssh_free(session);
    return NULL;
  }
  ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
  if(ssh_connect(session)){
    fprintf(stderr,"Connection failed : %s\n",ssh_get_error(session));
    ssh_disconnect(session);
    ssh_free(session);
    return NULL;
  }
  if(verify_knownhost(session)<0){
    ssh_disconnect(session);
    ssh_free(session);
    return NULL;
  }
  auth=authenticate_console(session);
  if(auth==SSH_AUTH_SUCCESS){
    return session;
  } else if(auth==SSH_AUTH_DENIED){
    fprintf(stderr,"Authentication failed\n");
  } else {
    fprintf(stderr,"Error while authenticating : %s\n",ssh_get_error(session));
  }
  ssh_disconnect(session);
  ssh_free(session);
  return NULL;
}

int ex_main(){
    ssh_session session;
    ssh_channel channel;
    // Increase the '108' to a higher value if you get assertion failures.
    char cmd[BACKING_FILE_LEN * 3 + 108];
    int cmdlen, rc;

    session = connect_ssh((char*)SERVER, (char*)USER_NAME, 0);
    if (session == NULL) {
        ssh_finalize();
        return 1;
    }

    //printf("%%IPC SSH Signalling MSC\n");
    char dummy;
    xMessageBufferSend(ssh_to_usb, &dummy, 1, portMAX_DELAY);

    struct ipc_msg msg;
    int ipc_num = 1;
    while (1)
    {
      int rbytes, total = 0;

      channel = ssh_channel_new(session);
      if (channel == NULL) {
          ssh_disconnect(session);
          ssh_free(session);
          ssh_finalize();
          return 1;
      }

      rc = ssh_channel_open_session(channel);
      if (rc < 0) {
          goto failed;
      }

      //printf("%%IPC SSH Wait for MSC signal start %d\n", ipc_num);
      rc = xMessageBufferReceive(usb_to_ssh, &msg, sizeof msg, portMAX_DELAY);
      //printf("%%IPC SSH Wait for MSC signal finish %d, sz=%d\n", ipc_num++, rc); ipc_num++;
      if (msg.host_cmd == CREATE_BACKING_FILE)
      {
        long long size = (0LL + msg.lba) * (0LL + msg.secsz);
        cmdlen = snprintf(cmd, sizeof cmd, "test -f %s || truncate --size %lld %s", BACKING_FILE, size, BACKING_FILE);
      }
      else if (msg.host_cmd == USB_READ)
      {
        cmdlen = snprintf(cmd, sizeof cmd, "dd if=%s bs=%d skip=%lld count=%d of=/dev/shm/$(basename %s).buf 2>/dev/null; cat /dev/shm/$(basename %s).buf", BACKING_FILE, msg.secsz, 0LL + msg.lba, msg.dlen/msg.secsz, BACKING_FILE, BACKING_FILE);
      }
      else if (msg.host_cmd == USB_WRITE)
      {
        cmdlen = snprintf(cmd, sizeof cmd, "dd of=%s conv=notrunc bs=%d seek=%lld count=%d 2>/dev/null", BACKING_FILE, msg.secsz, 0LL + msg.lba, msg.dlen/msg.secsz);
      }
      else strcpy(cmd, "false");
      //printf("%%SSH CMD %s\n", cmd);
      assert(cmdlen < sizeof cmd);
      rc = ssh_channel_request_exec(channel, cmd);
      if (msg.host_cmd == USB_WRITE)
      {
        ssh_channel_write(channel, msg.data, msg.dlen);
      }
      if (rc < 0) {
          HWSerial.printf("Fail 1\r\n");
          goto failed;
      }

      rbytes = ssh_channel_read(channel, msg.data + total, msg.dlen, 0);
      if (rbytes == SSH_ERROR) {
        HWSerial.printf("Fail 2\r\n");
        goto failed;
      }

      if (rbytes) do {
          total += rbytes;

          rbytes = ssh_channel_read(channel, msg.data + total, msg.dlen - total, 0);
          total = 0;
      } while (rbytes > 0);

      if (rbytes < 0) {
        HWSerial.printf("Fail 4\r\n");
        goto failed;
      }

      //printf("%%IPC SSH Signalling MSC\n");
      xMessageBufferSend(ssh_to_usb, &msg, sizeof msg, portMAX_DELAY);

      ssh_channel_send_eof(channel);
      ssh_channel_close(channel);
      ssh_channel_free(channel);
    } // while (1)

    ssh_disconnect(session);
    ssh_free(session);
    ssh_finalize();

    return 0;
failed:
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    ssh_disconnect(session);
    ssh_free(session);
    ssh_finalize();

    return 1;
}

#define newDevState(s) (devState = s)

esp_err_t event_cb(void *ctx, system_event_t *event)
{
  switch(event->event_id)
  {
    case SYSTEM_EVENT_STA_START:
      //#if ESP_IDF_VERSION_MAJOR < 4
      //WiFi.setHostname("libssh_esp32");
      //#endif
      HWSerial.print("%NET WiFi enabled with SSID=");
      HWSerial.println((char*)SSID);
      break;
    case SYSTEM_EVENT_STA_CONNECTED:
      WiFi.enableIpV6();
      wifiPhyConnected = true;
      if (devState < STATE_PHY_CONNECTED) newDevState(STATE_PHY_CONNECTED);
      break;
    case SYSTEM_EVENT_GOT_IP6:
      if (event->event_info.got_ip6.ip6_info.ip.addr[0] != htons(0xFE80)
      && !gotIp6Addr)
      {
        gotIp6Addr = true;
      }
      HWSerial.print("%NET IPv6 Address: ");
      HWSerial.println(IPv6Address(event->event_info.got_ip6.ip6_info.ip.addr));
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      gotIpAddr = true;
      HWSerial.print("%NET IPv4 Address: ");
      HWSerial.println(IPAddress(event->event_info.got_ip.ip_info.ip.addr));
      break;
    case SYSTEM_EVENT_STA_LOST_IP:
      //gotIpAddr = false;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      if (devState < STATE_WAIT_IPADDR) newDevState(STATE_NEW);
      if (wifiPhyConnected)
      {
        wifiPhyConnected = false;
      }
      WiFi.begin((char*)SSID, (char*)PSK);
      break;
    default:
      break;
  }
  return ESP_OK;
}

void controlTask(void *pvParameter)
{
  // Mount the file system.
  boolean fsGood = SPIFFS.begin();
  if (!fsGood)
  {
    HWSerial.println("%%CFG No formatted SPIFFS filesystem found to mount.");
    HWSerial.println("%%CFG Formatting...");
    fsGood = SPIFFS.format();
    if (fsGood)
    {
      SPIFFS.begin();
      createFile("/.ssh/known_hosts", SERVER_HASH, SERVER_HASH_LEN);
      createFile("/.ssh/id_ed25519", ID, ID_LEN);
      // On the ESP32-S2 (not S3) the following is required to write SPIFFS.
      SPIFFS.end(); SPIFFS.begin();
    }
  }
  if (!fsGood)
  {
    HWSerial.println("%%CFG Aborting now.");
    while (1) vTaskDelay(60000 / portTICK_PERIOD_MS);
  }
  HWSerial.printf(
    "%%CFG Mounted SPIFFS used=%d total=%d\r\n", SPIFFS.usedBytes(),
    SPIFFS.totalBytes());

  wifiPhyConnected = false;
  WiFi.disconnect(true);
  WiFi.mode(WIFI_MODE_STA);
  gotIpAddr = false; gotIp6Addr = false;
  WiFi.begin((char*)SSID, (char*)PSK);

  TickType_t xStartTime;
  xStartTime = xTaskGetTickCount();
  const TickType_t xTicksTimeout = WIFI_TIMEOUT_S*1000/portTICK_PERIOD_MS;
  bool aborting;

  while (1)
  {
    switch (devState)
    {
      case STATE_NEW :
        vTaskDelay(NET_WAIT_MS / portTICK_PERIOD_MS);
        break;
      case STATE_PHY_CONNECTED :
        newDevState(STATE_WAIT_IPADDR);
        // Set the initial time, where timeout will be started
        xStartTime = xTaskGetTickCount();
        break;
      case STATE_WAIT_IPADDR :
        if (gotIpAddr && gotIp6Addr)
          newDevState(STATE_GOT_IPADDR);
        else
        {
          // Check the timeout.
          if (xTaskGetTickCount() >= xStartTime + xTicksTimeout)
          {
            HWSerial.println("%%NET Timeout waiting for IP address");
            if (gotIpAddr || gotIp6Addr)
              newDevState(STATE_GOT_IPADDR);
            else
              newDevState(STATE_NEW);
          }
          else
          {
            vTaskDelay(NET_WAIT_MS / portTICK_PERIOD_MS);
          }
        }
        break;
      case STATE_GOT_IPADDR :
        newDevState(STATE_OTA_UPDATING);
        break;
      case STATE_OTA_UPDATING :
        // No OTA for this sketch.
        newDevState(STATE_OTA_COMPLETE);
        break;
      case STATE_OTA_COMPLETE :
        aborting = false;
        // Initialize the Arduino library.
        libssh_begin();

        // Run the main code.
        {
          int ex_rc = ex_main();
          HWSerial.printf("\n%%MSC Execution completed prematurely: rc=%d\r\n", ex_rc);
        }
        while (1) vTaskDelay(60000 / portTICK_PERIOD_MS);
        if (!aborting)
          newDevState(STATE_LISTENING);
        else
          newDevState(STATE_TCP_DISCONNECTED);
        break;
      case STATE_LISTENING :
        aborting = false;
        newDevState(STATE_TCP_DISCONNECTED);
        break;
      case STATE_TCP_DISCONNECTED :
        // This would be the place to free net resources, if needed,
        newDevState(STATE_LISTENING);
        break;
      default :
        break;
    }
  }
}

void ssh_exec_setup()
{
  devState = STATE_NEW;

  // Use the expected blocking I/O behavior.
  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  #if ESP_IDF_VERSION_MAJOR >= 4
  HWSerial.begin(115200);
  uart_driver_install
    ((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0);
  esp_vfs_dev_uart_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
  #else
  uart_driver_install((uart_port_t)CONFIG_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0);
  esp_vfs_dev_uart_use_driver(CONFIG_CONSOLE_UART_NUM);
  HWSerial.begin(115200);
  #endif

  #if ESP_IDF_VERSION_MAJOR >= 4
  //WiFi.setHostname("libssh_esp32");
  esp_netif_init();
  #else
  tcpip_adapter_init();
  #endif
  esp_event_loop_init(event_cb, NULL);

  // Stack size needs to be larger, so continue in a new task.
  xTaskCreatePinnedToCore(controlTask, "ctl", configSTACK, NULL,
    (tskIDLE_PRIORITY + 3), NULL, portNUM_PROCESSORS - 1);
}
