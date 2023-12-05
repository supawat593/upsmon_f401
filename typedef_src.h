#ifndef _TYPEDEF_SRC_H
#define _TYPEDEF_SRC_H

#include <cstdint>

typedef struct {
  unsigned int utc;
  char cmd[16];
  char resp[128];
} mail_t;

// typedef struct {
//   char broker[32];
//   int port;
//   char usr[32];
//   char pwd[32];
//   char encoded_key[64];
//   char topic_path[100];
//   char full_cmd[64];
//   char model[32];
//   char siteID[32];
//   char Device[16];
// } init_script_t;

typedef struct {
  char broker[32];
  int port;
  char usr[32];
  char pwd[32];
  char encoded_key[64];
  char topic_path[100];
  char full_cmd[64];
  char model[32];
  char siteID[32];
  char Device[16];
  char url_shortpath[64];
  char url_fullpath[128];
} init_script_t;

typedef struct {
  char revID[20];
  char imei[16];
  char iccid[20];
  char ipaddr[32];
  char dns_ip[16];
  int sig;
  int ber;
  char cereg_msg[64];
  char cops_msg[64];
  char cpsi_msg[128];
  char cclk_msg[32];
} __attribute__((__packed__)) cellular_data_t;

// typedef struct {
//   char stat_payload[512];
//   char stat_topic[128];
//   char str_data_topic[128];
//   char str_data_msg[256];
//   char str_sub_topic[128];

// } mqtt_config_t;

typedef struct {
  char sub_topic[128];
  char sub_payload[256];
  int client_idx;
  int len_topic;
  int len_payload;
} mqttsub_notify_t;

typedef struct {
  char notify_buffer[512];
  char cereg_msg[64];
  char rxtopic_msg[512];
  mqttsub_notify_t mqttsub;
} oob_notify_t;

typedef struct {
  unsigned int uid;
  char usr[32];
  char pwd[32];
} unique_stat_t;

typedef struct {
  bool flag_mqtt_start;
  bool flag_mqtt_connect;
  bool flag_mqtt_sub;
  int rty_mqtt_cnt;
} mqtt_flag_t;

typedef struct {
  int rty_at;
  int rty_creg;
  bool flag_atok;
  bool flag_attach;
  mqtt_flag_t mqtt_flag;
} param_mqtt_t;

#endif