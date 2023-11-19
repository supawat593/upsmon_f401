/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "upsmon_rtos.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "./TimerTPL5010/TimerTPL5010.h"
// #include "USBSerial.h"

#include "./ExtStorage/ExtStorage.h"
#include "FATFileSystem.h"
#include "SPIFBlockDevice.h"

#define FLAG_PPS (1UL << 0)
#define FLAG_UPLOAD (1UL << 1)
#define FLAG_CFGCHECK (1UL << 2)
#define FLAG_CONNECTION (1UL << 3)
#define FLAG_FWCHECK (1UL << 4)

#if TARGET_NUCLEO_F401RE
#define LED2 PA_6
#endif

DigitalOut myled(LED1, 1);
DigitalOut netstat(LED2, 0);

FATFileSystem fs(SPIF_MOUNT_PATH);
SPIFBlockDevice bd(MBED_CONF_SPIF_DRIVER_SPI_MOSI,
                   MBED_CONF_SPIF_DRIVER_SPI_MISO,
                   MBED_CONF_SPIF_DRIVER_SPI_CLK, MBED_CONF_SPIF_DRIVER_SPI_CS);
ExtStorage ext(&bd, &fs);

// static UnbufferedSerial pc(USBTX, USBRX);
static BufferedSerial mdm(MDM_TXD_PIN, MDM_RXD_PIN, 115200);
ATCmdParser *_parser;
// Sim7600Cellular *modem;
CellularService *modem;
TimerTPL5010 tpl5010(WDT_WAKE_PIN, WDT_DONE_PIN);

BufferedSerial rs232(ECARD_TX_PIN, ECARD_RX_PIN, 2400);
ATCmdParser *xtc232;

DigitalOut vrf_en(MDM_VRF_EN_PIN);
DigitalOut mdm_rst(MDM_RST_PIN);
// DigitalOut mdm_pwr(MDM_PWR_PIN, 1);
DigitalOut mdm_pwr(MDM_PWR_PIN);
DigitalOut mdm_flight(MDM_FLIGHT_PIN);
DigitalIn mdm_status(MDM_STATUS_PIN);

DigitalOut mdm_dtr(MDM_DTR_PIN);
DigitalIn mdm_ri(MDM_RI_PIN);

DigitalIn usb_det(USB_DET_PIN);

BusIn dipsw(DIPSW_P4_PIN, DIPSW_P3_PIN, DIPSW_P2_PIN, DIPSW_P1_PIN);

volatile bool is_script_read = false;
volatile bool is_usb_plug = false;
bool mdmOK = false;
bool mdmAtt = false;
bool bmqtt_start = false;
bool bmqtt_cnt = false;
bool bmqtt_sub = false;

int last_utc_rtc_sync = 0;
int last_utc_update_stat = 0;
unsigned int last_rtc_check_NW = 0;
unsigned int rtc_uptime = 0;

char str_sub_topic[128];

// Thread blink_thread(osPriorityNormal, 0x100, nullptr, "blink_thread"),
//     netstat_thread(osPriorityNormal, 0x100, nullptr, "netstat_thread"),
//     capture_thread(osPriorityNormal, 0x500, nullptr, "data_capture_thread"),
//     mdm_notify_thread(osPriorityNormal, 0x0c00, nullptr,
//     "mdm_notify_thread");

Thread blink_thread(osPriorityNormal, 0x100, nullptr, "blink_thread"),
    netstat_thread(osPriorityNormal, 0x100, nullptr, "netstat_thread"),
    capture_thread(osPriorityNormal, 0x500, nullptr, "data_capture_thread"),
    mdm_notify_thread(osPriorityNormal, 0x0c00, nullptr, "mdm_notify_thread"),
    cellular_thread(osPriorityNormal, 0x2000, nullptr, "cellular_thread");

Thread *usb_thread;
Thread isr_thread(osPriorityAboveNormal, 0x400, nullptr, "isr_queue_thread");

EventQueue isr_queue;
EventFlags mdm_evt_flags;
Semaphore mdm_sem(1);

init_script_t init_script, iap_init_script;
unique_stat_t mydata;

mqttPayload *mqtt_obj = NULL;

unsigned int *uid = (unsigned int *)0x801bffc;

int i_cmd = 0;
int len_mqttpayload = 0;

void cellular_task();
void maintain_connection();

uint8_t read_dipsw() { return (~dipsw.read()) & 0x0f; }

void usb_passthrough() {

  printf("---> start usb_passthrough Thread ...\r\n");

  char str_usb_ret[128];
  USBSerial pc2;

  uint8_t a, b;
  // char *a = new char[1];
  while (true) {

    while (pc2.connected()) {

      if (!get_usb_cnnt()) {
        set_usb_cnnt(true);
        debug("usb serial connected\r\n");
        debug("used/free stack : %d / %d\r\n", usb_thread->used_stack(),
              usb_thread->free_stack());
      }

      mail_t *xmail = ret_usb_mail.try_get_for(Kernel::Clock::duration(100));
      if (xmail != nullptr) {
        printf("usb_resp : %s" CRLF, xmail->resp);
        pc2.printf("%s\r", xmail->resp);
        ret_usb_mail.free(xmail);
      }

      if (get_idle_rs232()) {
        //   if (is_idle_rs232) {
        //   while (is_idle_rs232) {

        if (pc2.readable()) {
          a = pc2.getc();
          rs232.write(&a, 1);
        }
        if (rs232.readable()) {

          //   b = rs232.read(&b, 1);
          //   pc2.write(&b, 1);
          memset(str_usb_ret, 0, 128);
          //   read_xtc_to_char(str_usb_ret, 128, '\r');
          read_xparser_to_char(str_usb_ret, 128, '\r', xtc232);
          pc2.printf("%s\r", str_usb_ret);
        }
      }
    }

    while (!pc2.connected()) {

      if (get_usb_cnnt()) {
        set_usb_cnnt(false);
        debug("usb serial disconnected\r\n");
        debug("used/free stack : %d / %d\r\n", usb_thread->used_stack(),
              usb_thread->free_stack());
      }
      ThisThread::sleep_for(chrono::milliseconds(1000));
    }
    ThisThread::sleep_for(chrono::milliseconds(3000));
  }
}

void capture_thread_routine() {

  char ret_rs232[128];
  char str_cmd[6][20];

  char full_cmd[64];
  strcpy(full_cmd, init_script.full_cmd);

  char delim[] = ",";
  char *ptr;
  //   ptr = strtok(init_script.full_cmd, delim);
  ptr = strtok(full_cmd, delim);
  int n_cmd = 0;
  Timer duration_tme;

  while ((ptr != 0) && (n_cmd < 6)) {
    // strcpy(str_cmd_buff[k], ptr);
    sscanf(ptr, "\"%[^\"]\"", str_cmd[n_cmd]);
    n_cmd++;
    ptr = strtok(NULL, delim);
  }

  i_cmd = n_cmd;
  printf("---> start Capture Thread <---\r\n");

  while (true) {
    // printf("*********************************" CRLF);
    // printf("capture---> timestamp: %d" CRLF, (unsigned int)rtc_read());
    // printf("*********************************" CRLF);

    // is_idle_rs232 = false;
    set_idle_rs232(false);
    duration_tme.reset();
    duration_tme.start();

    for (int j = 0; j < n_cmd; j++) {
      ThisThread::sleep_for(1s);
      mail_t *mail = mail_box.try_alloc();

      memset(ret_rs232, 0, 128);
      memset(mail->cmd, 0, 16);
      memset(mail->resp, 0, 128);

      mail->utc = (unsigned int)rtc_read();
      strcpy(mail->cmd, str_cmd[j]);
      xtc232->flush();

      debug_if(xtc232->send(mail->cmd), "send command: %s\r\n", mail->cmd);
      read_xparser_to_char(ret_rs232, 128, '\r', xtc232);

      if (strlen(ret_rs232) > 0) {

        strcpy(mail->resp, ret_rs232);

        // <---------- mail for usb return msg ------------->
        mail_t *xmail = ret_usb_mail.try_alloc();
        memset(xmail->resp, 0, 128);
        strcpy(xmail->resp, mail->resp);

        if (get_usb_cnnt()) {
          // if (is_usb_cnnt) {
          ret_usb_mail.put(xmail);
        } else {
          ret_usb_mail.free(xmail);
        }
        // <------------------------------------------------>

        // printf("cmd=%s : ret=%s" CRLF, mail->cmd, mail->resp);
        mail_box.put(mail);

      } else {
        // debug("command: %s [NO RESP.]\r\n", mail->cmd);
        // mail_box.free(mail);

        strcpy(mail->resp, "NO_RESP");
        debug("command: %s [NO_RESP.]\r\n", mail->cmd);
        mail_box.put(mail);
      }
    }

    //   mail_box.put(mail);
    // is_idle_rs232 = true;
    set_idle_rs232(true);

    period_min = read_dipsw();

    static int cmd_period =
        duration_cast<chrono::seconds>(duration_tme.elapsed_time()).count();
    duration_tme.stop();

    // ThisThread::sleep_for(chrono::minutes(period_min));
    ThisThread::sleep_for(chrono::seconds(60 * period_min - cmd_period));
  }
}

uint32_t hex2int(char *hex) {
  uint32_t val = 0;
  while (*hex) {
    // get current character then increment
    uint8_t byte = *hex++;
    // transform hex character to the 4bit equivalent number, using the ascii
    // table indexes
    if (byte >= '0' && byte <= '9')
      byte = byte - '0';
    else if (byte >= 'a' && byte <= 'f')
      byte = byte - 'a' + 10;
    else if (byte >= 'A' && byte <= 'F')
      byte = byte - 'A' + 10;
    // shift 4 to make space for new digit, and add the 4 bits of the new digit
    val = (val << 4) | (byte & 0xF);
  }
  return val;
}

void mdm_notify_routine() {
  printf("mdm_notify_routine() started --->\r\n");
  char mdm_xbuf[512];
  int st = 0, end = 0;

  memcpy(&script_bkp, &init_script, sizeof(init_script_t));
  volatile bool is_rx_mqtt = false;
  oob_notify_t oob_msg;

  while (true) {

    if (get_notify_ready() && (!get_mdm_busy())) {

      if (mdm.readable()) {

        set_mdm_busy(true);
        memset(oob_msg.rxtopic_msg, 0, 512);
        read_xparser_to_char(mdm_xbuf, 512, '\r', _parser);

        if (detection_notify("+CMQTTRXSTART:", mdm_xbuf, oob_msg.rxtopic_msg)) {

          printf("\r\nNotify msg: %s\r\n", oob_msg.rxtopic_msg);

          int len_buf = 512;
          if (sscanf(oob_msg.rxtopic_msg, "+CMQTTRXSTART: %*d,%d,%d",
                     &oob_msg.mqttsub.len_topic,
                     &oob_msg.mqttsub.len_payload) == 2) {

            len_buf = (oob_msg.mqttsub.len_topic + oob_msg.mqttsub.len_payload)
                      << 1;
          }
          //   debug_if(len_buf < 512, "len_buf=%d\r\n", len_buf);

          memset(mdm_xbuf, 0, len_buf);
          memset(oob_msg.rxtopic_msg, 0, len_buf);

          _parser->set_timeout(500);
          _parser->read(mdm_xbuf, len_buf);
          _parser->set_timeout(8000);
          st = 0;
          while ((strncmp(&mdm_xbuf[st], "+CMQTTRXTOPIC", 13) != 0) &&
                 (st < (len_buf - 13))) {
            st++;
          }

          if (st < (len_buf - 13)) {

            memcpy(&oob_msg.rxtopic_msg[0], &mdm_xbuf[st],
                   sizeof(mdm_xbuf) - st);
            oob_msg.mqttsub = {"\0", "\0", 0, 0, 0};

            if (sscanf(oob_msg.rxtopic_msg, mqtt_sub_topic_pattern,
                       &oob_msg.mqttsub.len_topic, oob_msg.mqttsub.sub_topic,
                       &oob_msg.mqttsub.len_payload,
                       oob_msg.mqttsub.sub_payload,
                       &oob_msg.mqttsub.client_idx) == 5) {

              printout_mqttsub_notify(&oob_msg.mqttsub);
              is_rx_mqtt = true;
            }
          }

        }

        else if (detection_notify("+CEREG:", mdm_xbuf, oob_msg.rxtopic_msg)) {
          printf("\r\nNotify msg: %s\r\n", oob_msg.rxtopic_msg);

          char data1[5], data2[10];
          int int1, int2;
          uint32_t u32_x1, u32_x2;
          if (sscanf(oob_msg.rxtopic_msg, "+CEREG: %d,%[^,],%[^,],%d", &int1,
                     data1, data2, &int2) == 4) {
            u32_x1 = hex2int(data1);
            u32_x2 = hex2int(data2);
            // debug("int1= %d\tint2= %d\r\nu32_x1= %04X\tu32_x2= %08X\r\n",
            // int1,
            //       int2, u32_x1, u32_x2);
          }
        } else if (detection_notify("+CREG:", mdm_xbuf, oob_msg.rxtopic_msg)) {
          printf("\r\nNotify msg: %s\r\n", oob_msg.rxtopic_msg);
        } else if (detection_notify("+CMQTTCONNLOST:", mdm_xbuf,
                                    oob_msg.rxtopic_msg)) {
          printf("\r\nNotify msg: %s\r\n", oob_msg.rxtopic_msg);
          bmqtt_cnt = false;
          bmqtt_sub = false;

          int int_cause;
          if (sscanf(oob_msg.rxtopic_msg, "+CMQTTCONNLOST: %*d,%d",
                     &int_cause) == 1) {
            debug_if(int_cause == 3, "cause=3 -> Network is closed.\r\e");
            // bmqtt_cnt = false;
            // bmqtt_sub = false;
          }
        } else if (detection_notify("+CMQTTNONET", mdm_xbuf,
                                    oob_msg.rxtopic_msg)) {
          printf("\r\nNotify msg: %s\r\n", oob_msg.rxtopic_msg);
          bmqtt_start = false;
          bmqtt_cnt = false;
          bmqtt_sub = false;
        } else {
        }

        if (is_rx_mqtt) {
          //   debug("is_rx_mqtt: true\r\n");

          if (script_config_process(oob_msg.mqttsub.sub_payload) > 0) {

            debug("before write script file\r\n");
            ext.write_init_script(&script_bkp, FULL_SCRIPT_FILE_PATH);
            ext.deinit();
            system_reset();
          }
          is_rx_mqtt = false;
        }

        set_mdm_busy(false);
      }
    }
  }
}

int main() {

  // Initialise the digital pin LED1 as an output

  rtc_init();
  period_min = (read_dipsw() < 1) ? 1 : read_dipsw();

  printf("\r\n\n----------------------------------------\r\n");
  printf("| Hello,I am UPSMON_STM32F401RE_A7672E |\r\n");
  printf("----------------------------------------\r\n");
  printf("Firmware Version: %s" CRLF, firmware_vers);
  printf("SystemCoreClock : %.3f MHz.\r\n", SystemCoreClock / 1000000.0);
  //   printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  printf("Serial Number: UPS%d\r\n", *uid);
  printf("timestamp : %d\r\n", (unsigned int)time(NULL));
  printf("----------------------------------------\r\n");
  printf("\r\ncapture period : %d minutes\r\n", period_min);

  ext.init();
  ext.read_init_script(&init_script, FULL_SCRIPT_FILE_PATH);

  //   initial_FlashIAPBlockDevice(&iap);

  //   iap_to_script(&iap, &iap_init_script);
  //   if (iap_init_script.topic_path[0] == 0xff) {
  //     printf("iap is blank!!!\r\n");
  //     script_to_iap(&iap, &init_script);
  //   } else {
  //     printf("topic: %s\r\n", iap_init_script.topic_path);
  //     // printHEX((unsigned char *)&iap_init_script, sizeof(init_script_t));
  //   }

  isr_thread.start(callback(&isr_queue, &EventQueue::dispatch_forever));
  tpl5010.init(&isr_queue);

  blink_thread.start(mbed::callback(blink_routine, &myled));
  netstat_thread.start(callback(blip_netstat, &netstat));

  if (!ext.get_script_flag()) {
    blink_led(NOFILE);
  }

  _parser = new ATCmdParser(&mdm, "\r\n", 256, 8000);
  modem = new CellularService(_parser, mdm_pwr, mdm_rst);

  xtc232 = new ATCmdParser(&rs232, "\r", 256, 1000);
  ThisThread::sleep_for(500ms);

  mqtt_obj = new mqttPayload(&init_script, modem);

  vrf_en = 1;
  modem->powerkey_trig_mode(1);

  modem->ctrl_timer(1);
  int sys_time_ms = modem->read_systime_ms();

  while (mdm_status.read() && (modem->read_systime_ms() - sys_time_ms < 18000))
    ThisThread::sleep_for(250ms);

  debug_if(modem->read_systime_ms() - sys_time_ms > 18000,
           "MDM_STAT Timeout : %d sec.\r\n",
           modem->read_systime_ms() - sys_time_ms);

  sys_time_ms = modem->read_systime_ms();
  modem->ctrl_timer(0);

  //   modem->check_at_ready();
  //   modem->check_modem_status(3) ? netstat_led(IDLE) : netstat_led(OFF);
  modem->check_at_ready() ? netstat_led(IDLE) : netstat_led(OFF);

  last_rtc_check_NW = (unsigned int)rtc_read();
  if (ext.get_script_flag() && modem->initial_NW()) {
    netstat_led(CONNECTED);
  }

  char msg_ati[128] = {0};
  modem->get_ati(msg_ati);
  debug_if(strlen(msg_ati) > 0,
           "\r\n------ ATI Return mesg. "
           "------\r\n%s\r\n------------------------------\r\n\r\n",
           msg_ati);

  modem->ntp_setup();

  if (modem->get_cclk(modem->cell_info.cclk_msg)) {
    modem->sync_rtc(modem->cell_info.cclk_msg);
    printf("timestamp : %d\r\n", (unsigned int)rtc_read());
  }

  last_utc_rtc_sync = (int)rtc_read();

  bmqtt_start = modem->mqtt_start();
  debug_if(bmqtt_start, "MQTT Started\r\n");

  if (bmqtt_start) {

    modem->mqtt_accquire_client(modem->cell_info.imei);

    if (modem->dns_resolve(init_script.broker, modem->cell_info.dns_ip) < 0) {
      memset(modem->cell_info.dns_ip, 0, 16);
      strcpy(modem->cell_info.dns_ip, mqtt_broker_ip);
    }

    bmqtt_cnt = modem->mqtt_connect(modem->cell_info.dns_ip, init_script.usr,
                                    init_script.pwd, init_script.port);
    debug_if(bmqtt_cnt, "MQTT Connected\r\n");
    device_stat_update(modem, mqtt_obj, "RESTART");
    last_utc_update_stat = (int)rtc_read();
  }

  if (ext.get_script_flag()) {
    blink_led(NORMAL); //  normal Mode
    set_mdm_busy(false);
  }

  if (bmqtt_cnt) {
    memset(str_sub_topic, 0, 128);
    sprintf(str_sub_topic, "%s/config/%s", init_script.topic_path,
            modem->cell_info.imei);

    bmqtt_sub = modem->mqtt_sub(str_sub_topic);

    // if (bmqtt_sub) {
    //   set_notify_ready(true);
    // }
  }

  mdm_notify_thread.start(callback(mdm_notify_routine));
  capture_thread.start(callback(capture_thread_routine));
  //   usb_thread.start(callback(usb_passthrough));
  cellular_thread.start(callback(cellular_task));

  while (true) {

    if (tpl5010.get_wdt()) {
      tpl5010.set_wdt(false);
    }

    if ((usb_det.read() > 0) && (!is_usb_plug)) {

      usb_thread = new Thread(osPriorityNormal, 0x700, nullptr, "usb_thread");
      usb_thread->start(callback(usb_passthrough));
      is_usb_plug = true;
    }

    if ((usb_det.read() < 1) && (is_usb_plug)) {

      debug_if(usb_thread->terminate() == osOK, "usb_thread Terminated\r\n");
      delete usb_thread;
      is_usb_plug = false;
    }

    mail_t *mail = mail_box.try_get_for(Kernel::Clock::duration(500));
    if (mail != nullptr) {
      printf("utc : %d" CRLF, mail->utc);
      printf("cmd : %s" CRLF, mail->cmd);
      printf("resp : %s" CRLF, mail->resp);

      mqtt_obj->make_mqttPubTopic();
      mqtt_obj->make_mqttPayload(mail);

      // test logging
      len_mqttpayload = strlen(mqtt_obj->mqtt_payload);
      ext.write_data_log(mqtt_obj->mqtt_payload, (char *)FULL_LOG_FILE_PATH);
      debug("logsize %S = %d bytes.\r\n", FULL_LOG_FILE_PATH,
            ext.check_filesize(FULL_LOG_FILE_PATH, "r"));
      // end test logging

      mail_box.free(mail);
    }
    //  else NullPtr

    else {

      if (!get_mdm_busy()) {

        if (((unsigned int)rtc_read() - get_rtc_pub()) >
            ((unsigned int)(0.5 * 60 * period_min))) {

          int log_len = ext.check_filesize(FULL_LOG_FILE_PATH, "r");
          if (log_len > ((int)((i_cmd - 0.5) * len_mqttpayload))) {

            set_rtc_pub((unsigned int)rtc_read());
            mdm_evt_flags.set(FLAG_UPLOAD);
          }
          // mdm_evt_flags.set(FLAG_UPLOAD);
        } else if ((((unsigned int)rtc_read() - last_rtc_check_NW) > 50)) {

          mdm_evt_flags.set(FLAG_CONNECTION);

        } // end last_rtc_check_NW > 55
        else {
        }
      }

    } // end else NullPtr

  } // end while(true)
} // end main()

void cellular_task() {

  unsigned long flags_read = 0;
  printf("Starting cellular_task()        : %p\r\n", ThisThread::get_id());

  while (true) {

    flags_read = mdm_evt_flags.wait_any(FLAG_UPLOAD | FLAG_CFGCHECK |
                                            FLAG_CONNECTION | FLAG_FWCHECK,
                                        osWaitForever, false);

    switch (flags_read) {
    case FLAG_UPLOAD:
      mdm_sem.acquire();

      if (bmqtt_cnt) {

        printf("\r\n<----- mqttpub task ----->\r\n");
        set_notify_ready(false);
        set_mdm_busy(true);

        FILE *textfile;
        char line[256];

        debug("logsize %S = %d bytes.\r\n", FULL_LOG_FILE_PATH,
              ext.check_filesize(FULL_LOG_FILE_PATH, "r"));

        textfile = fopen(FULL_LOG_FILE_PATH, "r");
        if (textfile != NULL) {
          volatile bool pub_complete = false;
          volatile bool tflag = true;

          //   while (fgets(line, 256, textfile)) {
          while ((fgets(line, 256, textfile) != NULL) && tflag) {
            // printf("\r\n%s\r\n", line);
            int len = strlen(line);
            line[len - 2] = '\0';
            // modem->mqtt_publish(mqtt_obj->mqttpub_topic, line);

            pub_complete =
                tflag && modem->mqtt_publish(mqtt_obj->mqttpub_topic, line);
            tflag = pub_complete;
          }

          fclose(textfile);

          if (pub_complete) {
            debug_if(remove(FULL_LOG_FILE_PATH) == 0,
                     "Deleted %s successfully\r\n", FULL_LOG_FILE_PATH);
          }
          //   debug_if(remove(FULL_LOG_FILE_PATH) == 0,
          //            "Deleted %s successfully\r\n", FULL_LOG_FILE_PATH);
        } else {
          debug("fopen log fail\r\n");
        }

        set_mdm_busy(false);
      }
      mdm_evt_flags.clear(flags_read);

      mdm_sem.release();
      break;

    case FLAG_CFGCHECK:
      mdm_sem.acquire();
      //   schedule_cfg_check();
      mdm_evt_flags.clear(flags_read);

      mdm_sem.release();
      break;

    case FLAG_CONNECTION:
      mdm_sem.acquire();
      maintain_connection();
      mdm_evt_flags.clear(flags_read);

      mdm_sem.release();
      break;

    case FLAG_FWCHECK:
      mdm_sem.acquire();

      //   if (upgrade_firmware()) {
      //     go_bootloader();
      //   }

      //   nav.first_fix = false;
      mdm_evt_flags.clear(flags_read);

      mdm_sem.release();
      break;

    default:
      mdm_sem.acquire();
      mdm_evt_flags.clear(flags_read);
      mdm_sem.release();
      break;
    }
  }
}

void maintain_connection() {

  last_rtc_check_NW = (unsigned int)rtc_read();
  printf(CRLF "<----- Checking NW. Status ----->" CRLF);
  printf("timestamp : %d\r\n", last_rtc_check_NW);

  set_notify_ready(false);
  set_mdm_busy(true);

  // check AT>OK
  if (!modem->check_modem_status(10)) {
    netstat_led(OFF);

    vrf_en = 0;
    ThisThread::sleep_for(500ms);
    vrf_en = 1;

    modem->powerkey_trig_mode(1);

    bmqtt_start = false;
    bmqtt_cnt = false;
    bmqtt_sub = false;

    rtc_uptime = (unsigned int)rtc_read();

    while (mdm_status.read() && ((unsigned int)rtc_read() - rtc_uptime < 18))
      ;

    if ((unsigned int)rtc_read() - rtc_uptime > 18) {
      printf("MDM_STAT Timeout : %d sec.\r\n",
             (unsigned int)rtc_read() - rtc_uptime);
      rtc_uptime = (unsigned int)rtc_read();
    }
    //   modem->check_at_ready();
    //   mdmOK = modem->check_modem_status();
    mdmOK = modem->check_at_ready() && modem->check_modem_status();

    if (mdmOK) {
      printf("Power re-attached -> SIM7600 Status: Ready\r\n");
      netstat_led(IDLE);

      if (modem->set_full_FUNCTION()) {
        printf("AT Command and set full_function : OK\r\n");
        modem->set_creg(2);
        modem->set_cereg(2);
        mdmAtt = modem->check_attachNW();
      }
    }

  } else {

    if ((int)rtc_read() - last_utc_rtc_sync > 300) {
      memset(modem->cell_info.cclk_msg, 0, 32);
      if (modem->get_cclk(modem->cell_info.cclk_msg)) {
        modem->sync_rtc(modem->cell_info.cclk_msg);
        last_utc_rtc_sync = (int)rtc_read();
        printf("last_utc_rtc_sync : %d\r\n", last_utc_rtc_sync);
      }
    }

    debug_if(modem->get_csq(&modem->cell_info.sig, &modem->cell_info.ber),
             "sig=%d ber=%d\r\n", modem->cell_info.sig, modem->cell_info.ber);

    memset(modem->cell_info.cpsi_msg, 0, 128);
    debug_if(modem->get_cpsi(modem->cell_info.cpsi_msg), "cpsi=> %s\r\n ",
             modem->cell_info.cpsi_msg);

    mdmAtt = modem->check_attachNW();
    if (mdmAtt && (modem->get_creg() == 1)) {

      netstat_led(CONNECTED);
      debug_if(modem->get_IPAddr(modem->cell_info.ipaddr), "ipaddr= %s\r\n",
               modem->cell_info.ipaddr);

      if (!bmqtt_start) {
        mdmAtt = modem->check_attachNW();
        if (mdmAtt && (modem->get_creg() == 1)) {

          netstat_led(CONNECTED);

          if (modem->dns_resolve(init_script.broker, modem->cell_info.dns_ip) <
              0) {
            memset(modem->cell_info.dns_ip, 0, 16);
            strcpy(modem->cell_info.dns_ip, mqtt_broker_ip);
          }

          bmqtt_start = modem->mqtt_start();
          debug_if(bmqtt_start, "MQTT Started\r\n");

          if (bmqtt_start) {
            modem->mqtt_accquire_client(modem->cell_info.imei);

            bmqtt_cnt =
                modem->mqtt_connect(modem->cell_info.dns_ip, init_script.usr,
                                    init_script.pwd, init_script.port);
            debug_if(bmqtt_cnt, "MQTT Connected\r\n");

            if (bmqtt_cnt) {
              memset(str_sub_topic, 0, 128);
              sprintf(str_sub_topic, "%s/config/%s", init_script.topic_path,
                      modem->cell_info.imei);
              bmqtt_sub = modem->mqtt_sub(str_sub_topic);
            }
          }
        }
      }

      if (modem->mqtt_connect_stat() < 1) {
        if (bmqtt_start) {
          bmqtt_cnt =
              modem->mqtt_connect(modem->cell_info.dns_ip, init_script.usr,
                                  init_script.pwd, init_script.port);
          debug_if(bmqtt_cnt, "MQTT Connected\r\n");

          if (bmqtt_cnt) {
            memset(str_sub_topic, 0, 128);
            sprintf(str_sub_topic, "%s/config/%s", init_script.topic_path,
                    modem->cell_info.imei);
            bmqtt_sub = modem->mqtt_sub(str_sub_topic);
          }
        }
      }

      if (modem->mqtt_isdisconnect() < 1) {
        bmqtt_cnt = false;
        bmqtt_sub = false;

        if (modem->mqtt_release()) {

          if (modem->mqtt_stop()) {
            bmqtt_start = false;
          } else {

            bmqtt_start = false;
            netstat_led(OFF);

            if ((!modem->check_modem_status(3)) && (!mdm_status.read())) {
              modem->MDM_HW_reset();

              printf("NW Attaching Fail: Rebooting Modem!!!" CRLF);

              rtc_uptime = (unsigned int)rtc_read();
              while (mdm_status.read() &&
                     ((unsigned int)rtc_read() - rtc_uptime < 18))
                ;

              modem->check_at_ready();
            }

            if (modem->check_modem_status(10)) {

              if (modem->get_cfun_mode() == 1) {
                //   modem->initial_NW();
                modem->set_min_cFunction();
                modem->set_full_FUNCTION();
              } else {
                modem->set_cops();
                modem->set_full_FUNCTION();
              }

              netstat_led(IDLE);
            }
          }
        }
      } else {
        bmqtt_cnt = true;
      }

      if (((unsigned int)rtc_read() - last_utc_update_stat) > 3600) {

        last_utc_update_stat = (int)rtc_read();
        printf("last_utc_update_stat : %d\r\n", last_utc_update_stat);
        //   device_stat_update(modem, init_script.topic_path);
        device_stat_update(modem, mqtt_obj);
      }

    } else {
      netstat_led(OFF);
      bmqtt_start = false;
      bmqtt_cnt = false;
      bmqtt_sub = false;

      if ((!modem->check_modem_status(3)) && (!mdm_status.read())) {
        modem->MDM_HW_reset();

        printf("NW Attaching Fail: Rebooting Modem!!!" CRLF);

        rtc_uptime = (unsigned int)rtc_read();
        while (mdm_status.read() &&
               ((unsigned int)rtc_read() - rtc_uptime < 18))
          ;

        modem->check_at_ready();
      }

      if (modem->check_modem_status(10)) {

        if (modem->get_cfun_mode() == 1) {
          // modem->initial_NW();
          modem->set_min_cFunction();
          modem->set_full_FUNCTION();
        } else {
          modem->set_cops();
          modem->set_full_FUNCTION();
        }

        netstat_led(IDLE);
      }
    }
  }
  // end check AT>OK

  set_mdm_busy(false);

  if (bmqtt_sub) {
    set_notify_ready(true);
  }
}
