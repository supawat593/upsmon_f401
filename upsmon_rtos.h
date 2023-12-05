#include "./CellularService/CellularService.h"
#include "./ExtStorage/ExtStorage.h"
#include "Base64.h"
#include "FATFileSystem.h"
#include "FlashIAPBlockDevice.h"
#include "SPIFBlockDevice.h"
#include "USBSerial.h"
#include "devices_src.h"
#include "mbed.h"
#include "typedef_src.h"

#define iap_script_offset 0
// #define iap_startup_offset 0x8000 // 32768
#define iap_startup_offset 0x4000 // 16384

// Blinking rate in milliseconds
#define BLINKING_RATE 500ms
#define CRLF "\r\n"

typedef enum { OFF = 0, IDLE, CONNECTED } netstat_mode;
typedef enum { PWRON = 0, NORMAL, NOFILE } blink_mode;

int period_min = 0;

typedef struct {
  int led_state;
  int period_ms;
  float duty;
} blink_t;

class mqttPayload {

public:
  char fname[36];
  char msg[100];
  char stat_fname[50];
  char stat_dir[100];
  char mqtt_payload[256];
  char mqttpub_topic[128];
  char mqtt_stat_payload[512];
  char mqtt_stat_topic[128];
  char mqtt_cfg_topic[128];

  CellularService *_cell;
  init_script_t *_script;

  mqttPayload(init_script_t *script, CellularService *cell)
      : _script(script), _cell(cell) {
    fname[0] = '\0';
    msg[0] = '\0';
    stat_fname[0] = '\0';
    stat_dir[0] = '\0';
    mqtt_payload[0] = '\0';
    mqttpub_topic[0] = '\0';
    mqtt_stat_payload[0] = '\0';
    mqtt_stat_topic[0] = '\0';
    mqtt_cfg_topic[0] = '\0';
  }

  int make_mqttPayload(mail_t *_data) {

    int len = 0;
    static char str_msg[256];
    memset(str_msg, 0, 256);

    len = sprintf(str_msg, payload_pattern, _cell->cell_info.imei, _data->utc,
                  _script->model, _script->siteID, _data->cmd, _data->resp);
    memset(mqtt_payload, 0, 256);
    strncpy(mqtt_payload, str_msg, len);
    return len;
  }

  int make_mqttPubTopic() {
    int len = 0;
    static char str_pub_topic[128];
    memset(str_pub_topic, 0, 128);

    len = sprintf(str_pub_topic, "%s/data/%s", _script->topic_path,
                  _cell->cell_info.imei);
    memset(mqttpub_topic, 0, 128);
    strncpy(mqttpub_topic, str_pub_topic, len);
    return len;
  }

  int make_mqttStatPayload(char *str_stat) {
    int len = 0;
    static char stat_payload[512];
    memset(stat_payload, 0, 512);

    len = sprintf(stat_payload, stat_pattern, _cell->cell_info.imei,
                  (unsigned int)rtc_read(), firmware_vers, Dev_Group,
                  period_min, str_stat, _cell->cell_info.sig,
                  _cell->cell_info.ber, _cell->cell_info.cops_msg,
                  _cell->cell_info.cereg_msg, _cell->cell_info.cpsi_msg);
    memset(mqtt_stat_payload, 0, 512);
    strncpy(mqtt_stat_payload, stat_payload, len);
    return len;
  }

  int make_mqttStatTopic() {
    int len = 0;
    static char stat_topic[128];
    memset(stat_topic, 0, 128);

    len = sprintf(stat_topic, "%s/status/%s", _script->topic_path,
                  _cell->cell_info.imei);
    memset(mqtt_stat_topic, 0, 128);
    strncpy(mqtt_stat_topic, stat_topic, len);
    return len;
  }

  int make_mqttCfgTopic() {
    int len = 0;
    static char cfg_topic[128];
    memset(cfg_topic, 0, 128);

    len = sprintf(cfg_topic, "%s/config/#", _script->topic_path);
    memset(mqtt_cfg_topic, 0, 128);
    strncpy(mqtt_cfg_topic, cfg_topic, len);
    return len;
  }

private:
};

FlashIAPBlockDevice iap;

FATFileSystem fs(SPIF_MOUNT_PATH);
SPIFBlockDevice bd(MBED_CONF_SPIF_DRIVER_SPI_MOSI,
                   MBED_CONF_SPIF_DRIVER_SPI_MISO,
                   MBED_CONF_SPIF_DRIVER_SPI_CLK, MBED_CONF_SPIF_DRIVER_SPI_CS);
ExtStorage ext(&bd, &fs);

// static UnbufferedSerial pc(USBTX, USBRX);
static BufferedSerial mdm(MDM_TXD_PIN, MDM_RXD_PIN, 115200);
ATCmdParser *_parser;
CellularService *modem;

Mutex mutex_idle_rs232, mutex_usb_cnnt, mutex_mdm_busy, mutex_notify;

MemoryPool<int, 1> netstat_mpool;
Queue<int, 1> netstat_queue;

MemoryPool<blink_t, 1> blink_mpool;
Queue<blink_t, 1> blink_queue;

Mail<mail_t, 60> mail_box;
Mail<mail_t, 8> ret_usb_mail;

Base64 base64_obj;
init_script_t script_bkp;
Mutex mtx_init_script;

volatile bool is_usb_cnnt = false;
volatile bool is_idle_rs232 = true;
volatile bool is_mdm_busy = true;
volatile bool is_notify_ready = false;
volatile bool is_cfgevt_flag = false;

unsigned int last_rtc_pub = 0;
Mutex mtx_rtc_msg;
Mutex mtx_cfgevt;

unsigned int *uid = (unsigned int *)0x801bffc;
unique_stat_t mydata;

char *get_device_id() {
  char tmp[16] = {0};
  sprintf(tmp, "UPS%d", *uid);
  memcpy(&mydata.uid, &tmp, strlen(tmp));
  return (char *)&mydata.uid;
}

unsigned int get_rtc_pub() {
  unsigned int temp = 0;
  mtx_rtc_msg.lock();
  temp = last_rtc_pub;
  mtx_rtc_msg.unlock();
  return temp;
}
void set_rtc_pub(unsigned int nrtc) {
  mtx_rtc_msg.lock();
  last_rtc_pub = nrtc;
  mtx_rtc_msg.unlock();
}

bool get_cfgevt_flag() {
  bool temp;
  mtx_cfgevt.lock();
  temp = is_cfgevt_flag;
  mtx_cfgevt.unlock();
  return temp;
}

void set_cfgevt_flag(bool temp) {
  mtx_cfgevt.lock();
  is_cfgevt_flag = temp;
  mtx_cfgevt.unlock();
}

void restore_script(init_script_t *_script) {
  mtx_init_script.lock();
  memcpy(_script, &script_bkp, sizeof(init_script_t));
  mtx_init_script.unlock();
}

void backup_script(init_script_t *_script) {
  mtx_init_script.lock();
  memcpy(&script_bkp, _script, sizeof(init_script_t));
  mtx_init_script.unlock();
}

void printout_mqttsub_notify(mqttsub_notify_t *data) {
  mutex_notify.lock();
  printf("<-------------------------------------------------\r\n");
  printf("len_topic=%d\r\n", data->len_topic);
  printf("sub_topic=%s\r\n", data->sub_topic);
  printf("len_payload=%d\r\n", data->len_payload);
  printf("sub_payload=%s\r\n", data->sub_payload);
  printf("client_idx=%d\r\n", data->client_idx);
  printf("------------------------------------------------->\r\n");
  mutex_notify.unlock();
}

bool get_notify_ready() {
  bool temp;
  mutex_notify.lock();
  temp = is_notify_ready;
  mutex_notify.unlock();
  return temp;
}

void set_notify_ready(bool temp) {
  mutex_notify.lock();
  is_notify_ready = temp;
  mutex_notify.unlock();
}

bool get_mdm_busy() {
  bool temp;
  mutex_mdm_busy.lock();
  temp = is_mdm_busy;
  mutex_mdm_busy.unlock();
  return temp;
}

void set_mdm_busy(bool temp) {
  mutex_mdm_busy.lock();
  is_mdm_busy = temp;
  mutex_mdm_busy.unlock();
}

bool get_idle_rs232() {
  bool temp;
  mutex_idle_rs232.lock();
  temp = is_idle_rs232;
  mutex_idle_rs232.unlock();
  return temp;
}

void set_idle_rs232(bool temp) {
  mutex_idle_rs232.lock();
  is_idle_rs232 = temp;
  mutex_idle_rs232.unlock();
}

bool get_usb_cnnt() {
  bool temp;
  mutex_usb_cnnt.lock();
  temp = is_usb_cnnt;
  mutex_usb_cnnt.unlock();
  return temp;
}

void set_usb_cnnt(bool temp) {
  mutex_usb_cnnt.lock();
  is_usb_cnnt = temp;
  mutex_usb_cnnt.unlock();
}

void blip_netstat(DigitalOut *led) {
  int led_state = 0;

  while (true) {
    int *net_queue;

    if (netstat_queue.try_get_for(Kernel::Clock::duration(200), &net_queue)) {
      led_state = *net_queue;
      netstat_mpool.free(net_queue);
    }

    if (led_state == 1) {
      *led = 1;
    } else if (led_state == 2) {
      *led = !*led;
    } else {
      *led = 0;
    }
  }
}

void netstat_led(netstat_mode inp = IDLE) {

  int *net_queue = netstat_mpool.try_alloc();

  switch (inp) {

  case IDLE:
    *net_queue = 1;
    netstat_queue.try_put(net_queue);
    break;
  case CONNECTED:
    *net_queue = 2;
    netstat_queue.try_put(net_queue);
    break;
  default:
    *net_queue = 0;
    netstat_queue.try_put(net_queue);
  }
}

void blink_routine(DigitalOut *led) {
  int led_state = 0;
  int period_ms = 1000;
  float duty = 0.5;
  while (true) {
    blink_t *led_queue;
    if (blink_queue.try_get_for(Kernel::Clock::duration(int(period_ms * duty)),
                                &led_queue)) {
      led_state = led_queue->led_state;
      period_ms = led_queue->period_ms;
      duty = led_queue->duty;
      blink_mpool.free(led_queue);
    }

    if (led_state == 0) {
      *led = 1;
    } else {
      *led = !*led;
    }
  }
}

void blink_led(blink_mode inp = PWRON) {

  blink_t *led_queue = blink_mpool.try_alloc();

  switch (inp) {

  case NORMAL:
    led_queue->led_state = 1;
    led_queue->duty = 0.5;
    led_queue->period_ms = 1000;
    blink_queue.try_put(led_queue);
    break;
  case NOFILE:
    led_queue->led_state = 2;
    led_queue->duty = 0.5;
    led_queue->period_ms = 200;
    blink_queue.try_put(led_queue);
    break;
  default:
    led_queue->led_state = 0;
    led_queue->duty = 0.5;
    led_queue->period_ms = 1000;
    blink_queue.try_put(led_queue);
  }
}

void printHEX(unsigned char *msg, unsigned int len) {

  printf(
      "\r\n>>>>>------------------- printHEX -------------------------<<<<<");
  printf("\r\nAddress :  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");
  printf(
      "\r\n----------------------------------------------------------------");

  unsigned int k = 0;
  for (unsigned int j = 0; j < len; j++) {
    if ((j % 16) == 0) {
      printf("\r\n0x%04X0 : ", k);
      k++;
    }
    printf("%02X ", (unsigned)msg[j]);
  }
  printf("\r\n----------------------------------------------------------------"
         "\r\n");
}

void initial_FlashIAPBlockDevice(FlashIAPBlockDevice *_bd) {
  printf("FlashIAPBlockDevice initialising\r\n");
  //   _bd->init();
  debug_if(_bd->init() != 0, "initial FlashIAP Fail!!!\r\n");
  printf("FlashIAP block device size: %llu\r\n", _bd->size());
  printf("FlashIAP block device program_size: %llu\r\n",
         _bd->get_program_size());
  printf("FlashIAP block device read_size: %llu\r\n", _bd->get_read_size());
  printf("FlashIAP block device erase size: %llu\r\n", _bd->get_erase_size());
}

void script_to_iap(FlashIAPBlockDevice *_bd, init_script_t *_init) {
  init_script_t *buffer = (init_script_t *)malloc(sizeof(init_script_t));
  memcpy(buffer, _init, sizeof(init_script_t));

  _bd->erase(iap_script_offset, _bd->get_erase_size());
  _bd->program(buffer, iap_script_offset, _bd->get_erase_size());
  free(buffer);
}

void iap_to_script(FlashIAPBlockDevice *_bd, init_script_t *_init) {
  init_script_t *buffer = (init_script_t *)malloc(sizeof(init_script_t));
  _bd->read(buffer, iap_script_offset, sizeof(init_script_t));
  memcpy(_init, buffer, sizeof(init_script_t));
  free(buffer);
}

int read_xparser_to_char(char *tbuf, int size, char end,
                         ATCmdParser *_xparser) {
  int count = 0;
  int x = 0;

  if (size > 0) {
    for (count = 0; (count < size) && (x >= 0) && (x != end); count++) {
      x = _xparser->getc();
      *(tbuf + count) = (char)x;
    }

    count--;
    *(tbuf + count) = 0;

    // Convert line endings:
    // If end was '\n' (0x0a) and the preceding character was 0x0d, then
    // overwrite that with null as well.
    if ((count > 0) && (end == '\n') && (*(tbuf + count - 1) == '\x0d')) {
      count--;
      *(tbuf + count) = 0;
    }
  }

  return count;
}

int detection_notify(const char *keyword, char *src, char *dst) {
  int st = 0, end = 0;
  int len_keyword = 0;
  len_keyword = strlen(keyword);

  int len_src = 0;
  if (strlen(src) > 1) {
    len_src = strlen(src);
  } else {
    len_src = 512;
  }

  //   printf("Notify: len_src=%d\r\n", len_src);
  // printHEX((unsigned char *)src, len_src);

  char end_text[] = {0x0d, 0x0a, 0x00, 0x00};
  st = 0;
  end = 0;

  while ((strncmp(&src[st], keyword, len_keyword) != 0) && (st < len_src)) {
    st++;
  }

  end = st;
  while ((strncmp(&src[end], end_text, 4) != 0) && (end < len_src)) {
    end++;
  }

  if (st < len_src) {
    memcpy(&dst[0], &src[st], end - st + 2);
    return 1;
  } else {
    strcpy(dst, "");
    return 0;
  }
}

int script_config_process(char cfg_msg[512], CellularService *_modem) {
  char str_cmd[10][64];
  char raw_key[64];
  char delim[] = "&";
  char *ptr;
  //   ptr = strtok(init_script.full_cmd, delim);
  ptr = strtok(cfg_msg, delim);
  int n_cmd = 0;

  while ((ptr != 0) && (n_cmd < 10)) {
    strcpy(str_cmd[n_cmd], ptr);
    // sscanf(ptr, "\"%[^\"]\"", str_cmd[n_cmd]);
    n_cmd++;
    ptr = strtok(NULL, delim);
  }

  char raw_usr[16], raw_pwd[16];
  int cfg_success = 0;
  bool match_device = false;

  for (int i = 0; i < n_cmd; i++) {

    debug("str_cmd[%d] ---> %s\r\n", i, str_cmd[i]);

    if (sscanf(str_cmd[i], "Device: \"%[^\"]\"", script_bkp.Device) == 1) {

      if (strcmp(script_bkp.Device, _modem->cell_info.imei) == 0) {
        match_device = true;
      } else if (strcmp(script_bkp.Device, get_device_id()) == 0) {
        match_device = true;
      } else {
        match_device = false;
      }
    } else if (sscanf(str_cmd[i], "Command: [%[^]]]", script_bkp.full_cmd) ==
               1) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Broker: \"%[^\"]\"", script_bkp.broker)) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Topic: \"%[^\"]\"", script_bkp.topic_path) ==
               1) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Model: \"%[^\"]\"", script_bkp.model) == 1) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Site_ID: \"%[^\"]\"", script_bkp.siteID) ==
               1) {
      cfg_success++;
    } else if (sscanf(str_cmd[i], "Key: \"%[^\"]\"", raw_key) == 1) {

      //   char raw_usr[16], raw_pwd[16];
      char key_encoded2[64];
      char *key_decode2;
      strcpy(key_encoded2, raw_key);

      size_t len_key_encode = (size_t)strlen(key_encoded2);
      size_t len_key_decode;

      for (int ix = 0; ix < 2; ix++) {
        key_decode2 =
            base64_obj.Decode(key_encoded2, len_key_encode, &len_key_decode);

        len_key_encode = len_key_decode;
        strcpy(key_encoded2, key_decode2);
      }

      if (sscanf(key_decode2, "%[^ ] %[^\n]", raw_usr, raw_pwd) == 2) {

        memset(script_bkp.encoded_key, 0, 64);
        memcpy(&script_bkp.encoded_key, &raw_key, strlen(raw_key));
        printf("configuration process: Key Changed" CRLF);
        cfg_success++;
      }
    } else if (sscanf(str_cmd[i], "Auth_Key: \"%[^ ] %[^\"]\"", raw_usr,
                      raw_pwd) == 2) {

      //   char raw_usr[16], raw_pwd[16];
      char key_decoded[64];
      char *key_encode2;
      memset(raw_key, 0, 64);
      strcpy(raw_key, raw_usr);
      strcat(raw_key, " ");
      strcat(raw_key, raw_pwd);
      strcpy(key_decoded, raw_key);

      size_t len_key_decode = (size_t)strlen(raw_key);
      size_t len_key_encode;

      for (int ix = 0; ix < 2; ix++) {
        key_encode2 =
            base64_obj.Encode(key_decoded, len_key_decode, &len_key_encode);

        if (key_encode2 != NULL) {

          len_key_decode = len_key_encode;
          strcpy(key_decoded, key_encode2);
        }
      }

      if (len_key_encode > 0) {
        memset(script_bkp.encoded_key, 0, 64);
        strcpy(script_bkp.encoded_key, key_encode2);
        printf("Key generation from Auth_Key Complete" CRLF);
        cfg_success++;
      }
    } else {
      printf("Pattern Check : Not Matched!\r\n");
    }
  }

  debug_if(!match_device, "Not Matched or Authentication fail...\r\n");

  //   return cfg_success;
  return ((cfg_success > 0) && match_device) ? cfg_success : 0;
}

void device_stat_update(CellularService *_obj, mqttPayload *_mqttpayload,
                        const char *stat_mode = "NORMAL") {
  char str_stat[15];
  strcpy(str_stat, stat_mode);
  _mqttpayload->make_mqttStatTopic();

  _obj->set_cereg(2);
  _obj->get_csq(&_obj->cell_info.sig, &_obj->cell_info.ber);
  _obj->get_cops(_obj->cell_info.cops_msg);

  memset(_obj->cell_info.cpsi_msg, 0, 128);
  _obj->get_cpsi(_obj->cell_info.cpsi_msg);

  if (_obj->get_cereg(_obj->cell_info.cereg_msg) > 0) {

    _mqttpayload->make_mqttStatPayload(str_stat);

    _obj->mqtt_publish(_mqttpayload->mqtt_stat_topic,
                       _mqttpayload->mqtt_stat_payload);
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

void modem_notify_routine(oob_notify_t *oob_msg) {
  printf("mdm_notify_routine() started --->\r\n");
  char mdm_xbuf[512];
  int st = 0, end = 0;

  while (true) {

    if (get_notify_ready() && (!get_mdm_busy())) {

      if (mdm.readable()) {

        set_mdm_busy(true);
        memset(oob_msg->rxtopic_msg, 0, 512);
        // read_xparser_to_char(mdm_xbuf, 512, '\r', _parser);
        modem->read_atc_to_char(mdm_xbuf, 512, '\r');

        if (detection_notify("+CMQTTRXSTART:", mdm_xbuf,
                             oob_msg->rxtopic_msg)) {

          printf("\r\nNotify msg: %s\r\n", oob_msg->rxtopic_msg);

          int len_buf = 512;
          if (sscanf(oob_msg->rxtopic_msg, "+CMQTTRXSTART: %*d,%d,%d",
                     &oob_msg->mqttsub.len_topic,
                     &oob_msg->mqttsub.len_payload) == 2) {

            len_buf =
                (oob_msg->mqttsub.len_topic + oob_msg->mqttsub.len_payload)
                << 1;
          }
          //   debug_if(len_buf < 512, "len_buf=%d\r\n", len_buf);

          memset(mdm_xbuf, 0, len_buf);
          memset(oob_msg->rxtopic_msg, 0, len_buf);

          //   _parser->set_timeout(500);
          //   _parser->read(mdm_xbuf, len_buf);
          //   _parser->set_timeout(8000);

          int ix = 0;
          while (mdm.readable()) {
            mdm.read((char *)&mdm_xbuf[ix], 1);
            ix++;
          }

          st = 0;
          while ((strncmp(&mdm_xbuf[st], "+CMQTTRXTOPIC", 13) != 0) &&
                 (st < (len_buf - 13))) {
            st++;
          }

          if (st < (len_buf - 13)) {

            memcpy(&oob_msg->rxtopic_msg[0], &mdm_xbuf[st],
                   sizeof(mdm_xbuf) - st);
            oob_msg->mqttsub = {"\0", "\0", 0, 0, 0};

            if (sscanf(oob_msg->rxtopic_msg, mqtt_sub_topic_pattern,
                       &oob_msg->mqttsub.len_topic, oob_msg->mqttsub.sub_topic,
                       &oob_msg->mqttsub.len_payload,
                       oob_msg->mqttsub.sub_payload,
                       &oob_msg->mqttsub.client_idx) == 5) {

              printout_mqttsub_notify(&oob_msg->mqttsub);
              //   is_rx_mqtt = true;
              set_cfgevt_flag(true);
            }
          }

        }

        else if (detection_notify("+CEREG:", mdm_xbuf, oob_msg->rxtopic_msg)) {
          printf("\r\nNotify msg: %s\r\n", oob_msg->rxtopic_msg);

          char data1[5], data2[10];
          int int1, int2;
          uint32_t u32_x1, u32_x2;
          if (sscanf(oob_msg->rxtopic_msg, "+CEREG: %d,%[^,],%[^,],%d", &int1,
                     data1, data2, &int2) == 4) {
            u32_x1 = hex2int(data1);
            u32_x2 = hex2int(data2);
            // debug("int1= %d\tint2= %d\r\nu32_x1= %04X\tu32_x2= %08X\r\n",
            // int1,
            //       int2, u32_x1, u32_x2);
          }
        } else if (detection_notify("+CREG:", mdm_xbuf, oob_msg->rxtopic_msg)) {
          printf("\r\nNotify msg: %s\r\n", oob_msg->rxtopic_msg);
        } else if (detection_notify("+CMQTTCONNLOST:", mdm_xbuf,
                                    oob_msg->rxtopic_msg)) {
          printf("\r\nNotify msg: %s\r\n", oob_msg->rxtopic_msg);
          //   bmqtt_cnt = false;
          //   bmqtt_sub = false;

          int int_cause;
          if (sscanf(oob_msg->rxtopic_msg, "+CMQTTCONNLOST: %*d,%d",
                     &int_cause) == 1) {
            debug_if(int_cause == 3, "cause=3 -> Network is closed.\r\e");
            // bmqtt_cnt = false;
            // bmqtt_sub = false;
          }
        } else if (detection_notify("+CMQTTNONET", mdm_xbuf,
                                    oob_msg->rxtopic_msg)) {
          printf("\r\nNotify msg: %s\r\n", oob_msg->rxtopic_msg);
          //   bmqtt_start = false;
          //   bmqtt_cnt = false;
          //   bmqtt_sub = false;
        } else {
        }

        set_mdm_busy(false);
      }
    }
  }
}
