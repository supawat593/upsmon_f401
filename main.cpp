/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "upsmon_rtos.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "./TimerTPL5010/TimerTPL5010.h"

#define FLAG_PPS (1UL << 0)
#define FLAG_UPLOAD (1UL << 1)
#define FLAG_CFGCHECK (1UL << 2)
#define FLAG_CONNECTION (1UL << 3)
#define FLAG_FWCHECK (1UL << 4)

#define FLAG_CAPTURE (1UL << 5)

#if TARGET_NUCLEO_F401RE
#define LED2 PA_6
#endif

DigitalOut myled(LED1, 1);
DigitalOut netstat(LED2, 0);

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

bool first_msg = false;

int last_utc_rtc_sync = 0;
int last_utc_update_stat = 0;
unsigned int last_rtc_check_NW = 0;
unsigned int rtc_uptime = 0;

Thread blink_thread(osPriorityNormal, 0x100, nullptr, "blink_thread"),
    netstat_thread(osPriorityNormal, 0x100, nullptr, "netstat_thread"),
    capture_thread(osPriorityNormal, 0x500, nullptr, "data_capture_thread"),
    modem_notify_thread(osPriorityNormal, 0x800, nullptr,
                        "modem_notify_thread"),
    cellular_thread(osPriorityNormal, 0x1000, nullptr, "cellular_thread");

Thread *usb_thread;
Thread isr_thread(osPriorityAboveNormal, 0x400, nullptr, "isr_queue_thread");

EventQueue isr_queue;
EventFlags mdm_evt_flags;
EventFlags capture_evt_flags;
LowPowerTicker capture_tick;

Semaphore mdm_sem(1);

init_script_t init_script, iap_init_script;
mqttPayload *mqtt_obj = NULL;
oob_notify_t oob_msg;

int i_cmd = 0;
int len_mqttpayload = 0;
bool msg_rdy = false;

void cellular_task();
void connection_init();
bool instruction_process(char *msg, init_script_t *_script);
bool intstruction_verify(init_script_t *_script, CellularService *_modem);
void mqtt_init(param_mqtt_t *param);
void maintain_connection(param_mqtt_t *param);

void capture_period() { capture_evt_flags.set(FLAG_CAPTURE); }

void reset_mqtt_flag(param_mqtt_t *param) {
  param->mqtt_flag.flag_mqtt_start = false;
  param->mqtt_flag.flag_mqtt_connect = false;
  param->mqtt_flag.flag_mqtt_sub = false;
  param->mqtt_flag.rty_mqtt_cnt = 0;
}

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

  unsigned long capture_flags_read = 0;
  char ret_rs232[128];
  char str_cmd[6][20];

  char full_cmd[64];
  strcpy(full_cmd, init_script.full_cmd);

  char delim[] = ",";
  char *ptr;
  //   ptr = strtok(init_script.full_cmd, delim);
  ptr = strtok(full_cmd, delim);
  int n_cmd = 0;

  while ((ptr != 0) && (n_cmd < 6)) {
    // strcpy(str_cmd_buff[k], ptr);
    sscanf(ptr, "\"%[^\"]\"", str_cmd[n_cmd]);
    n_cmd++;
    ptr = strtok(NULL, delim);
  }

  i_cmd = n_cmd;
  printf("---> start Capture Thread <---\r\n");

  while (true) {

    capture_flags_read =
        capture_evt_flags.wait_any(FLAG_CAPTURE, osWaitForever, false);

    switch (capture_flags_read) {
    case FLAG_CAPTURE:

      set_idle_rs232(false);

      for (int j = 0; j < n_cmd; j++) {
        ThisThread::sleep_for(1s);
        mail_t *mail = mail_box.try_alloc();

        memset(ret_rs232, 0, 128);
        memset(mail->cmd, 0, 16);
        memset(mail->resp, 0, 128);

        mail->utc = (unsigned int)rtc_read();
        strcpy(mail->cmd, str_cmd[j]);
        xtc232->flush();

        debug_if(!xtc232->send(mail->cmd), "command: %s have sent fail\r\n",
                 mail->cmd);
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

      capture_evt_flags.clear(capture_flags_read);
      break;
    default:
      capture_evt_flags.clear(capture_flags_read);
      break;
    }

    // ThisThread::sleep_for(1000ms);
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
  printf("Serial Number: %s\r\n", get_device_id());
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

  blink_thread.start(mbed::callback(blink_ack_routine, &myled));
  netstat_thread.start(callback(blink_netstat_routine, &netstat));

  _parser = new ATCmdParser(&mdm, "\r\n", 256, 8000);
  modem = new CellularService(_parser, mdm_pwr, mdm_rst);
  //   modem = new CellularService(&mdm, mdm_pwr, mdm_rst);

  xtc232 = new ATCmdParser(&rs232, "\r", 256, 1500);
  ThisThread::sleep_for(500ms);

  mqtt_obj = new mqttPayload(&init_script, modem);

  if (ext.get_script_flag()) {
    ack_led_stat(NORMAL); //  normal Mode
    set_mdm_busy(false);
  } else {
    ack_led_stat(NOFILE);
  }

  backup_script(&init_script);
  capture_thread.start(callback(capture_thread_routine));
  modem_notify_thread.start(callback(modem_notify_routine, &oob_msg));
  cellular_thread.start(callback(cellular_task));

  if (!first_msg) {
    first_msg = true;
    ThisThread::sleep_for(10s);
    capture_evt_flags.set(FLAG_CAPTURE);
    capture_tick.attach(callback(capture_period),
                        chrono::seconds(period_min * 60));
  }

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

      msg_rdy = (mqtt_obj->mqtt_payload[0] == '{') &&
                (mqtt_obj->mqtt_payload[len_mqttpayload - 1] == '}');

      if ((len_mqttpayload > 0) && msg_rdy) {
        int logsize = ext.check_filesize(FULL_LOG_FILE_PATH, "r");
        // printf("logsize=%d\r\n", logsize);

        if (logsize > 0x40000) {
          printf("\r\n<----- sizeof logfile over criteria ----->\r\n");

          FILE *textfile, *dummyfile;
          int offset = logsize - 0x8000;

          char line[256];
          char dummyfname[128];
          strcpy(dummyfname, "/");
          strcat(dummyfname, SPIF_MOUNT_PATH);
          strcat(dummyfname, "/");
          strcat(dummyfname, "dummylog.txt");

          textfile = fopen(FULL_LOG_FILE_PATH, "r");
          dummyfile = fopen(dummyfname, "w");

          if ((textfile != NULL) && (dummyfile != NULL)) {

            fseek(textfile, offset, SEEK_SET);
            while (fgets(line, 256, textfile) != NULL) {

              fputs(line, dummyfile);
            }
            fclose(dummyfile);

            fseek(textfile, 0, SEEK_SET);
            fclose(textfile);

            debug_if(remove(FULL_LOG_FILE_PATH) == 0,
                     "Deleted %s successfully\r\n", FULL_LOG_FILE_PATH);

            debug_if(rename(dummyfname, FULL_LOG_FILE_PATH) == 0,
                     "rename file %s to %s complete\r\n", dummyfname,
                     FULL_LOG_FILE_PATH);
          } else {
            printf("fopen logfile fail\r\n");
          }

          logsize = ext.check_filesize(FULL_LOG_FILE_PATH, "r");
          debug_if(logsize > 0, "before log have size= %d bytes.\r\n", logsize);
        }

        ext.write_data_log(mqtt_obj->mqtt_payload, (char *)FULL_LOG_FILE_PATH);
        debug("log path %s : size= %d bytes.\r\n", FULL_LOG_FILE_PATH,
              ext.check_filesize(FULL_LOG_FILE_PATH, "r"));
        logsize = ext.check_filesize(FULL_LOG_FILE_PATH, "r");
      }

      debug_if(!msg_rdy, "mqttpayload compose fail\r\n");
      // end test logging

      mail_box.free(mail);
    }
    //  else NullPtr

    else {

      if (get_grant_firmware()) {
        set_grant_firmware(false);
        mdm_evt_flags.set(FLAG_FWCHECK);
      }

      if (get_cfgevt_flag()) {
        set_cfgevt_flag(false);
        mdm_evt_flags.set(FLAG_CFGCHECK);
      }

      if (!get_mdm_busy()) {

        if (((unsigned int)rtc_read() - get_rtc_pub()) >
            ((unsigned int)(0.65 * 60 * period_min))) {

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

  param_mqtt_t param_mqtt = {0, 0, false, false, {false, false, false, 0}};
  volatile bool pub_complete = false;
  volatile bool verified_firmware = false;
  int cfg_return_count = 0;

  unsigned long flags_read = 0;
  printf("Starting cellular_task()        : %p\r\n", ThisThread::get_id());
  connection_init();

  if (intstruction_verify(&init_script, modem)) {
    cfg_return_count = 8;
    set_grant_firmware(true);
  }

  mqtt_init(&param_mqtt);

  while (true) {

    flags_read = mdm_evt_flags.wait_any(FLAG_UPLOAD | FLAG_CFGCHECK |
                                            FLAG_CONNECTION | FLAG_FWCHECK,
                                        osWaitForever, false);

    switch (flags_read) {
    case FLAG_UPLOAD:
      mdm_sem.acquire();

      if (param_mqtt.mqtt_flag.flag_mqtt_connect) {

        printf("\r\n<----- mqttpub task ----->\r\n");
        set_notify_ready(false);
        set_mdm_busy(true);

        debug("log path %s : size= %d bytes.\r\n", FULL_LOG_FILE_PATH,
              ext.check_filesize(FULL_LOG_FILE_PATH, "r"));

        netstat_led_stat(TRANSMITTING);
        // netstat
        pub_complete =
            ext.upload_log(modem, FULL_LOG_FILE_PATH, mqtt_obj->mqttpub_topic);

        if (pub_complete) {
          debug_if(remove(FULL_LOG_FILE_PATH) == 0,
                   "Deleted %s successfully\r\n", FULL_LOG_FILE_PATH);
        }
        // netstat
        netstat_led_stat(REGISTERED);

        set_mdm_busy(false);
      }
      mdm_evt_flags.clear(flags_read);

      mdm_sem.release();
      break;

    case FLAG_CFGCHECK:
      mdm_sem.acquire();

      debug("is_cfgevt_flag: true\r\n");
      //   modem->get_oob_msg(&oob_msg);
      debug("payload= %s\r\n", oob_msg.mqttsub.sub_payload);

      cfg_return_count =
          script_config_process(oob_msg.mqttsub.sub_payload, modem);

      debug_if(cfg_return_count > 0, "cfg_return_count=%d\r\n",
               cfg_return_count);

      if (cfg_return_count >= 8) {
        restore_script(&init_script);
        set_grant_firmware(true);
      } else if (cfg_return_count > 0) {
        debug("before write script file\r\n");
        restore_script(&init_script);
        ext.write_init_script(&init_script, FULL_SCRIPT_FILE_PATH);
        ext.deinit();

        debug("configuration file have configured : Restart NOW!...\r\n");
        system_reset();
      } else {
        debug("configuration fail\r\n");
      }

      mdm_evt_flags.clear(flags_read);

      mdm_sem.release();
      break;

    case FLAG_CONNECTION:
      mdm_sem.acquire();
      //   maintain_connection();
      maintain_connection(&param_mqtt);
      mdm_evt_flags.clear(flags_read);

      mdm_sem.release();
      break;

    case FLAG_FWCHECK:
      mdm_sem.acquire();

      set_notify_ready(false);
      set_mdm_busy(true);

      modem->set_cgdcont(2);
      modem->set_cgact(2);
      modem->get_cgact(2);

      if (ext.process_ota(modem, &init_script)) {
        verified_firmware = true;
        debug("prepare for firmware flashing\r\n");

        if (rename("/spif/firmware.bin", FULL_FIRMWARE_FILE_PATH) == 0) {

          debug("firmware verified : preparing to flashing\r\n");
        } else {

          debug_if(
              remove("/spif/firmware.bin") == 0,
              "firmware not verified : remove file firmware.bin complete\r\n");
        }
      }

      if (cfg_return_count == 8) {

        if (verified_firmware) {
          //   ext.write_init_script(&init_script, FULL_SCRIPT_FILE_PATH);
          ext.deinit();
          debug("firmware preparing complete : Restart NOW!...\r\n");
          system_reset();
        }
      } else if (cfg_return_count > 0) {
        // debug("before write script file\r\n");
        ext.write_init_script(&init_script, FULL_SCRIPT_FILE_PATH);
        ext.deinit();

        debug("configuration file have configured : require system reboot\r\n");
        debug_if(cfg_return_count >= 8,
                 "firmware preparing complete : Restart NOW!...\r\n");

        system_reset();
      } else {
        // dummy
      }

      set_notify_ready(true);
      set_mdm_busy(false);

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

void connection_init() {

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

  modem->check_at_ready() ? netstat_led_stat(SEARCH)
                          : netstat_led_stat(POWEROFF);

  last_rtc_check_NW = (unsigned int)rtc_read();
  if (ext.get_script_flag() && modem->initial_NW()) {
    netstat_led_stat(REGISTERED);
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
}

bool instruction_process(char *msg, init_script_t *_script) {
  bool checked = false;
  if (sscanf(msg, "FirmwareVersion: \"%[^\"]\"", _script->ota_data.version) ==
      1) {
    checked = true;
  } else if (sscanf(msg, "Device: \"%[^\"]\"", _script->ota_data.device) == 1) {
    checked = true;
  } else if (sscanf(msg, "DirPath: \"%[^\"]\"", _script->ota_data.dir_path) ==
             1) {
    checked = true;
  } else if (sscanf(msg, "FileSize: \"%d\"", &_script->ota_data.file_length) ==
             1) {
    checked = true;
  } else if (sscanf(msg, "Checksum: \"%d\"", &_script->ota_data.checksum) ==
             1) {
    checked = true;
  } else if (sscanf(msg, "FileName: \"%[^\"]\"", _script->ota_data.filename) ==
             1) {
    checked = true;
  } else {
    checked = false;
  }
  return checked;
}

bool intstruction_verify(init_script_t *_script, CellularService *_modem) {
  int len = 0;
  char fbuffer[512];
  char file_url[128];
  char str[512];
  bool instruction_verified = false;
  bool method_action = false;

  if (!_modem->http_start()) {
    debug("http_start() incomplete!!!\r\n");
    return false;
  }

  memset(file_url, 0, 128);
  strcpy(file_url, _script->url_path);
  strcat(file_url, "/");
  strcat(file_url, BASE_INSTRUCTION_PATH);

  debug("url_path : %s\r\nfilename: %s\r\n", _script->url_path,
        BASE_INSTRUCTION_PATH);

  // modem->http_set_parameter(url_http);
  _modem->http_set_parameter(file_url);
  method_action = _modem->http_method_action(&len);

  //   if (_modem->http_method_action(&len)) {
  if (method_action) {

    debug("method_action : datalen = % d\r\n ", len);

    debug_if(_modem->http_read_header(fbuffer, &len),
             "read_header : len = % d\r\n\n<---------- header_buf "
             "---------->\r\n%s\r\n\n ",
             len, fbuffer);

    debug_if(_modem->http_getsize_data(&len), "getsize_data datalen=%d\r\n",
             len);

    if (len > 0) {

      modem->http_read_data(fbuffer, 0, len);

      int st = 0, end = 0;
      while ((strncmp(&fbuffer[st], "BEGIN:", 6) != 0) && (st < len)) {
        st++;
      }

      end = st;
      while ((strncmp(&fbuffer[end], "END:", 4) != 0) && (end < len)) {
        end++;
      }

      if ((st < len) && (end < len)) {
        memset(str, 0, 512);
        strncpy(&str[0], &fbuffer[st], end - st + 4);

        const char s[3] = "\r\n";
        char cut_msg[10][64];
        // char dummy_filename[32];
        char *token;
        int i = 0;

        // strcpy(str, _script->ota_data.filename);
        /* get the first token */
        token = strtok(str, s);

        /* walk through other tokens */
        while (token != NULL) {
          //   printf(" %s\r\n", token);
          memset(cut_msg[i], 0, 64);
          strcpy(cut_msg[i], token);
          debug("index=%d -> cut_msg : %s\r\n", i, cut_msg[i]);
          i++;
          token = strtok(NULL, s);
        }

        bool device_matched = false;
        bool version_matched = false;
        bool file_matched = false;
        bool temp_bool = true;

        int nvalue = 0;
        for (int k = 0; k < (i - 2); k++) {
          file_matched =
              temp_bool && instruction_process(cut_msg[1 + k], _script);
          temp_bool = file_matched;
          nvalue = (file_matched) ? nvalue + 1 : nvalue;
        }

        device_matched =
            (strcmp(_script->ota_data.device, (char *)Dev_Group) == 0) ? true
                                                                       : false;
        version_matched =
            (atoi(_script->ota_data.version) - atoi(firmware_vers) > 0) ? true
                                                                        : false;

        debug_if(device_matched, "\r\nFirmware have matched -> device: %s\r\n",
                 _script->ota_data.device);
        debug_if(!device_matched,
                 "\r\nFirmware have not matched -> device: %s\r\n", Dev_Group);

        debug_if(version_matched,
                 "new Firmware have found -> version: %s\r\n\n",
                 _script->ota_data.version);
        debug_if(!version_matched,
                 "Device have latest Firmware -> version: %s\r\n\n",
                 firmware_vers);

        instruction_verified =
            ((nvalue == 6) && device_matched && version_matched) ? true : false;
      }
    }
  }

  //   _modem->http_stop();
  debug_if(_modem->http_stop(), "http_stop() : complete\r\n");

  return instruction_verified;
}

void mqtt_init(param_mqtt_t *param) {

  param->mqtt_flag.flag_mqtt_start = modem->mqtt_start();
  debug_if(param->mqtt_flag.flag_mqtt_start, "MQTT Started\r\n");

  if (param->mqtt_flag.flag_mqtt_start) {

    modem->mqtt_accquire_client(modem->cell_info.imei);

    if (modem->dns_resolve(init_script.broker, modem->cell_info.dns_ip) < 0) {
      memset(modem->cell_info.dns_ip, 0, 16);
      strcpy(modem->cell_info.dns_ip, mqtt_broker_ip);
    }

    param->mqtt_flag.flag_mqtt_connect =
        modem->mqtt_connect(modem->cell_info.dns_ip, init_script.usr,
                            init_script.pwd, init_script.port);
    debug_if(param->mqtt_flag.flag_mqtt_connect, "MQTT Connected\r\n");
    device_stat_update(modem, mqtt_obj, "RESTART");
    last_utc_update_stat = (int)rtc_read();
  }

  if (param->mqtt_flag.flag_mqtt_connect) {

    mqtt_obj->make_mqttCfgTopic();
    param->mqtt_flag.flag_mqtt_sub = modem->mqtt_sub(mqtt_obj->mqtt_cfg_topic);
  }
}

void maintain_connection(param_mqtt_t *param) {

  last_rtc_check_NW = (unsigned int)rtc_read();
  printf(CRLF "<----- Checking NW. Status ----->" CRLF);
  printf("timestamp : %d\r\n", last_rtc_check_NW);

  set_notify_ready(false);
  set_mdm_busy(true);

  // check AT>OK
  param->flag_atok = modem->check_modem_status(10);

  if (!param->flag_atok) {

    param->rty_at++;
    debug("rty_at=%d\r\n", param->rty_at);

    if (param->rty_at > 5) {
      param->rty_at = 0;

      if (mdm_status.read() == 0) {
        modem->powerkey_trig_mode(false);
      }

      vrf_en = 0;
      ThisThread::sleep_for(500ms);
      vrf_en = 1;
      modem->powerkey_trig_mode();
      reset_mqtt_flag(param);
      netstat_led_stat(SEARCH);

    } else {
      if (mdm_status.read() == 0) {
        modem->MDM_HW_reset();
        reset_mqtt_flag(param);
        netstat_led_stat(SEARCH);

      } else {

        vrf_en = 0;
        ThisThread::sleep_for(500ms);
        vrf_en = 1;
        modem->powerkey_trig_mode();
        reset_mqtt_flag(param);
        netstat_led_stat(SEARCH);
      }
    }

  } else {
    param->rty_at = 0;

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

    param->flag_attach = modem->check_attachNW();

    if (param->flag_attach && (modem->get_creg() == 1)) {
      param->rty_creg = 0;

      netstat_led_stat(REGISTERED);
      debug_if(modem->get_IPAddr(modem->cell_info.ipaddr), "ipaddr= %s\r\n",
               modem->cell_info.ipaddr);

      param->mqtt_flag.flag_mqtt_connect =
          (modem->mqtt_connect_stat() == 1) ? true : false;

      if (!param->mqtt_flag.flag_mqtt_connect) {
        param->mqtt_flag.rty_mqtt_cnt++;
        debug("rty_mqtt_cnt=%d\r\n", param->mqtt_flag.rty_mqtt_cnt);

        if (param->mqtt_flag.rty_mqtt_cnt > 5) {

          param->mqtt_flag.rty_mqtt_cnt = 0;

          if (mdm_status.read() == 0) {
            modem->powerkey_trig_mode(false);
          }

          vrf_en = 0;
          ThisThread::sleep_for(500ms);
          vrf_en = 1;
          modem->powerkey_trig_mode();
          reset_mqtt_flag(param);
          netstat_led_stat(SEARCH);

        } else {
          reset_mqtt_flag(param);
          modem->mqtt_disconnect();
          modem->mqtt_release();
          modem->mqtt_stop();
        }

        param->flag_attach = modem->check_attachNW();

        if (param->flag_attach && (modem->get_creg() == 1)) {

          netstat_led_stat(REGISTERED);

          if (modem->dns_resolve(init_script.broker, modem->cell_info.dns_ip) <
              0) {
            memset(modem->cell_info.dns_ip, 0, 16);
            strcpy(modem->cell_info.dns_ip, mqtt_broker_ip);
          }

          param->mqtt_flag.flag_mqtt_start = modem->mqtt_start();
          debug_if(param->mqtt_flag.flag_mqtt_start, "MQTT Started\r\n");

          if (param->mqtt_flag.flag_mqtt_start) {
            modem->mqtt_accquire_client(modem->cell_info.imei);

            param->mqtt_flag.flag_mqtt_connect =
                modem->mqtt_connect(modem->cell_info.dns_ip, init_script.usr,
                                    init_script.pwd, init_script.port);
            debug_if(param->mqtt_flag.flag_mqtt_connect, "MQTT Connected\r\n");
          }
        }

      } else {

        if (!param->mqtt_flag.flag_mqtt_sub) {

          mqtt_obj->make_mqttCfgTopic();
          param->mqtt_flag.flag_mqtt_sub =
              modem->mqtt_sub(mqtt_obj->mqtt_cfg_topic);
        }

        if (((unsigned int)rtc_read() - last_utc_update_stat) > 3600) {

          last_utc_update_stat = (int)rtc_read();
          printf("last_utc_update_stat : %d\r\n", last_utc_update_stat);
          //   device_stat_update(modem, init_script.topic_path);
          device_stat_update(modem, mqtt_obj);
        }
      }
    } else {

      param->rty_creg++;

      if (param->rty_creg > 5) {
        param->rty_creg = 0;

        if (mdm_status.read() == 0) {
          modem->powerkey_trig_mode(false);
        }

        vrf_en = 0;
        ThisThread::sleep_for(500ms);
        vrf_en = 1;
        modem->powerkey_trig_mode();
        reset_mqtt_flag(param);
        netstat_led_stat(SEARCH);

      } else {
        if (mdm_status.read() == 0) {
          modem->MDM_HW_reset();
          reset_mqtt_flag(param);
          netstat_led_stat(SEARCH);

        } else {

          vrf_en = 0;
          ThisThread::sleep_for(500ms);
          vrf_en = 1;
          modem->powerkey_trig_mode();
          reset_mqtt_flag(param);
          netstat_led_stat(SEARCH);
        }
      }
    }
  }
  // end check AT>OK

  set_mdm_busy(false);

  if (param->mqtt_flag.flag_mqtt_sub) {
    set_notify_ready(true);
  }
}
