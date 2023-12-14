#ifndef SIM7600CELLULAR_H
#define SIM7600CELLULAR_H

#include "mbed.h"

class Sim7600Cellular {

public:
  Sim7600Cellular(ATCmdParser *_parser);
  Sim7600Cellular(BufferedSerial *_serial);
  Sim7600Cellular(PinName tx, PinName rx);
  bool check_at_ready();
  void get_ati(char *ati_retmsg);
  bool check_modem_status(int rty = 20);
  bool enable_echo(bool en);
  bool save_setting();

  void set_ntp_srv(char *srv, int tz_q);
  int get_ntp_srv(char *ntp_srv);
  bool check_ntp_status();

  bool check_attachNW();
  bool set_attachNW(int en = 0);
  int set_cops(int mode = 0, int format = 2);
  int get_cops(char *cops);
  int get_csq(int *power, int *ber, int retry = 10);
  int get_cclk(char *cclk);
  int get_cgdcont(int cid);
  int set_cgdcont(int cid, char *apn = "internet", char *pdp_type = "IPV4V6");
  int set_creg(int n);
  int get_creg();
  int get_creg(char *payload);
  int set_cereg(int n);
  int get_cereg(char *payload);
  int set_cgact(int cid = 1, int state = 1);
  int get_cgact(int cid = 1);
  bool set_full_FUNCTION(int rst = 0);
  bool set_min_cFunction();
  int get_cfun_mode();
  int get_revID(char *revid);
  int get_IMEI(char *simei);
  int get_ICCID(char *ciccid);

  int set_pref_Mode(int mode = 2);
  int get_pref_Mode();
  int set_acq_order(int a1 = 9, int a2 = 5, int a3 = 3, int a4 = 11, int a5 = 2,
                    int a6 = 4);
  int get_acq_order();

  int get_IPAddr(char *ipaddr);
  int get_cpsi(char *cpsi);
  int set_tz_update(int en);
  int dns_resolve(char *src, char *dst);
  int ping_dstNW(char *dst, int nrty = 4, int p_size = 64, int dest_type = 1);

  bool delete_allsms();

  bool mqtt_start();
  bool mqtt_stop();
  bool mqtt_release(int clientindex = 0);
  bool mqtt_accquire_client(char *clientName);
  bool mqtt_connect(char *broker_ip, char *usr, char *pwd, int port = 1883,
                    int clientindex = 0);
  int mqtt_connect_stat(void);
  int mqtt_connect_stat(char *ret_msg);
  int mqtt_isdisconnect(int clientindex = 0);
  int mqtt_disconnect(int client_index = 0, int timeout_sec = 60);
  bool mqtt_publish(char topic[128], char payload[512], int qos = 1,
                    int interval_s = 60);
  bool mqtt_sub(char topic[128], int clientindex = 0, int qos = 1);
  bool mqtt_unsub(char topic[128], int clientindex = 0, int dup = 0);

  bool http_start();
  bool http_stop();
  bool http_set_parameter(char url[128], int content = 0, int readmode = 0);
  // method -> 0 = GET, 1 = POST, 2 = HEAD, 3 = DELETE, 4 = PUT
  bool http_method_action(int *datalen, int method = 0);
  bool http_read_header(char *rxbuf, int *datalen);
  bool http_getsize_data(int *datalen);
  bool http_read_data(char *rxbuf, int offset, int datalen);

    // FTP
  int ftp_start();
  int ftp_stop();
  bool ftp_set_sockaddr_type(int singleip = 0);
  // type: 'A' -> ascii , "I" -> binary
  int ftp_set_transfer_type(char type = 'I');
  // #ftp_type : 0= ftp srv, 1= explicit ftps srv with ssl,
  // 2= explicit ftps srv with tls, 3= implicit ftps srv
  int ftp_login(char srv[128], char usr[32], char pwd[32], int port = 21,
                int ftp_type = 0);
  int ftp_login_stat();
  int ftp_logout();
  int ftp_get_currentdir(char *dir);
  int ftp_dir_listfile(char dir[128]);
  int ftp_changedir(char dir[128]);
  int ftp_checksize_rmtfile(char rmt_filename[64]);
  int ftp_delete_rmtfile(char rmt_filename[64]);
  // #dir_mode : 1= c:/ (local storage) , 2= d:/ (sd card)
  int ftp_downloadfile_toFS(char filepath[64], int dir_mode = 1);

  // FileSystem
  int fs_getdir(char *dir);
  int fs_setdir(char *dir = "C:");
  // fs_list_currentdir() -> mode : 0 both, 1 subdirectories, 2 files
  int fs_list_currentdir(int mode = 2);
  bool fs_deletefile(char filepath[64]);
  bool fs_renamefile(char old_name[64], char new_name[64]);
  int fs_get_remainsize();
  int fs_attributefile(char filepath[64]);
  //   bool fs_download(char full_fname[128], int offset, int len);
  bool fs_download(char full_fname[128], char *rcvbuf, int offset, int len);

  int read_atc_to_char(char *tbuf, int size, char end);

private:
  ATCmdParser *_atc;
  BufferedSerial *serial;
  char buf[0x2080];

  void printHEX(unsigned char *msg, unsigned int len);
};

#endif