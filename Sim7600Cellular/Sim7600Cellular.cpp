#include "Sim7600Cellular.h"
#include <cstdio>
#include <cstring>

// extern ATCmdParser *_parser;

Sim7600Cellular::Sim7600Cellular(ATCmdParser *_parser) : _atc(_parser) {}
Sim7600Cellular::Sim7600Cellular(BufferedSerial *_serial) : serial(_serial) {
  _atc = new ATCmdParser(serial, "\r\n", 256, 8000);
  debug("initial cellular module by bufferedserial argument\r\n");
}

Sim7600Cellular::Sim7600Cellular(PinName tx, PinName rx) {
  serial = new BufferedSerial(tx, rx, 115200);
  _atc = new ATCmdParser(serial, "\r\n", 256, 8000);
}

void Sim7600Cellular::printHEX(unsigned char *msg, unsigned int len) {

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
bool Sim7600Cellular::check_at_ready() {

  int ret = 0;
  debug("wait for *ATREADY\r\n");

  if (!_atc->scanf("*ATREADY: %d", &ret)) {
    debug("*ATREADY Receiving fail!!!\r\n");
  }

  return (ret == 1) ? true : false;
}

void Sim7600Cellular::get_ati(char *ati_retmsg) {
  char buffer[160] = {0};
  //   _atc->debug_on(1);
  _atc->set_timeout(250);
  _atc->send("ATI");
  _atc->read(buffer, 160);

  int st = 0;
  while ((strncmp(&buffer[st], "Manufacturer: ", 14) != 0) && (st < 146)) {
    st++;
  }

  int end = 0;
  while ((strncmp(&buffer[end], "\r\n\r\nOK", 6) != 0) && (end < 154)) {
    end++;
  }

  memcpy(ati_retmsg, &buffer[st], end - st);

  _atc->set_timeout(8000);
  _atc->flush();
}

bool Sim7600Cellular::check_modem_status(int rty) {
  bool bAT_OK = false;
  //   _atc->debug_on(1);
  _atc->set_timeout(1000);

  for (int i = 0; (!bAT_OK) && (i < rty); i++) {

    if (_atc->send("AT")) {
      //   ThisThread::sleep_for(100ms);
      if (_atc->recv("OK")) {
        bAT_OK = true;
        // debug("Module SIM7600  OK\r\n");
      } else {
        bAT_OK = false;
        debug("Module SIM7600  Fail : %d\r\n", i);
      }
    }
  }

  //   _atc->debug_on(0);
  _atc->set_timeout(8000);
  _atc->flush();

  return bAT_OK;
}

bool Sim7600Cellular::enable_echo(bool en) {
  char cmd_txt[10];
  sprintf(cmd_txt, "ATE%d", (int)en);
  if (_atc->send(cmd_txt) && _atc->recv("OK")) {
    if (en) {
      printf("Enable AT Echo\r\n");
    } else {
      printf("Disable AT Echo\r\n");
    }
    return true;
  }
  return false;
}
bool Sim7600Cellular::save_setting() {
  if (_atc->send("AT&W0") && _atc->recv("OK")) {
    return true;
  }
  return false;
}

void Sim7600Cellular::set_ntp_srv(char *srv, int tz_q) {
  char cmd_ntp[100];
  sprintf(cmd_ntp, "AT+CNTP=\"%s\",%d", srv, tz_q);

  if (_atc->send(cmd_ntp) && _atc->recv("OK")) {
    printf("set ntp_srv: [%s] Done\r\n", srv);
  }
}

int Sim7600Cellular::get_ntp_srv(char *ntp_srv) {
  char ret_ntp[64];

  if (_atc->send("AT+CNTP?") && _atc->scanf("+CNTP: %[^\n]\r\n", ret_ntp)) {
    strcpy(ntp_srv, ret_ntp);
    return 1;
  }
  strcpy(ntp_srv, "");
  return -1;
}

bool Sim7600Cellular::check_ntp_status() {
  int err = -1;
  _atc->send("AT+CNTP") && _atc->recv("+CNTP: %d\r\n", &err);
  return (err == 0) ? true : false;
}

bool Sim7600Cellular::check_attachNW() {
  int ret = -1;

  if (_atc->send("AT+CGATT?") && _atc->recv("+CGATT: %d\r\n", &ret)) {
    printf("check_attachNW received pattern: +CGATT: %d\r\n", ret);
  } else {
    ret = -1;
  }

  return (ret == 1) ? true : false;
}

bool Sim7600Cellular::set_attachNW(int en) {
  //   _atc->debug_on(1);
  printf("set_attachNW processing --> en=%d\r\n", en);

  if (_atc->send("AT+CGATT=%d", en) && _atc->recv("OK")) {
    _atc->debug_on(0);
    return true;
  }

  //   _atc->debug_on(0);
  return false;
}

int Sim7600Cellular::set_cops(int mode, int format) {
  char _cmd[16];
  sprintf(_cmd, "AT+COPS=%d,%d", mode, format);
  if (_atc->send(_cmd) && _atc->recv("OK")) {
    printf("set---> AT+COPS=%d,%d\r\n", mode, format);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_cops(char *cops) {
  char _ret[64];

  if (_atc->send("AT+COPS?") && _atc->recv("+COPS: %*d,%*d,\"%[^\"]\"", _ret)) {
    // printf("imei=  %s\r\n", _imei);
    strcpy(cops, _ret);
    return 1;
  }
  strcpy(cops, "");
  return -1;
}

int Sim7600Cellular::get_csq(int *power, int *ber, int retry) {
  int _power = 99;
  int _ber = 99;
  //   char ret[20];
  int i = 0;
  _atc->set_timeout(2000);
  while ((i < retry) && (_power == 99)) {
    if (_atc->send("AT+CSQ") && _atc->recv("+CSQ: %d,%d\r\n", &_power, &_ber)) {
      debug("retry: %d -> +CSQ: %d,%d\r\n", i, _power, _ber);
    }
    i++;
    ThisThread::sleep_for(1000);
  }
  _atc->set_timeout(8000);

  if (i < retry) {
    *power = _power;
    *ber = _ber;
    return 1;
  }

  *power = 99;
  *ber = 99;
  return -1;
}

int Sim7600Cellular::get_cclk(char *cclk) {
  char ret_cclk[32];
  if (_atc->send("AT+CCLK?") &&
      _atc->scanf("+CCLK: \"%[^\"]\"\r\n", ret_cclk)) {
    // printf("msg= %s\r\n", ret_cclk);
    strcpy(cclk, ret_cclk);
    return 1;
  }
  strcpy(cclk, "");
  return -1;
}

int Sim7600Cellular::get_cgdcont(int cid) {

  char buff[512];
  char rcv[16];
  char ret[256];

  sprintf(rcv, "+CGDCONT: %d", cid);

  _atc->set_timeout(2000);
  _atc->send("AT+CGDCONT?");
  _atc->read(buff, 512);

  _atc->set_timeout(8000);
  _atc->flush();

  int st = 0, end;
  while ((strncmp(&buff[st], rcv, strlen(rcv)) != 0) && (st < 512)) {
    st++;
  }

  end = st;
  while ((strncmp(&buff[end], "\r\n", 2) != 0) && (end < 512)) {
    end++;
  }

  memset(ret, 0, 256);
  memcpy(&ret[0], &buff[st], end - st);

  if (strlen(ret) > 0) {
    debug("cgdcont cid=%d -> %s\r\n", cid, ret);
    return 0;
  }
  debug("cgdcont cid=%d -> not configurated\r\n", cid);
  return -1;
}

int Sim7600Cellular::set_cgdcont(int cid, char *apn, char *pdp_type) {

  if (_atc->send("AT+CGDCONT=%d,\"%s\",\"%s\"", cid, pdp_type, apn) &&
      _atc->recv("OK")) {
    debug("set cgdcont : cid=%d pdp_type=%s apn=%s\r\n", cid, pdp_type, apn);
  }
  return 0;
}

int Sim7600Cellular::set_creg(int n) {
  char cmd[10];
  sprintf(cmd, "AT+CREG=%d", n);
  if (_atc->send(cmd) && _atc->recv("OK")) {
    printf("modem set---> AT+CREG=%d\r\n", n);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_creg() {
  int n = 0;
  int stat = 0;
  char ret[20];

  if (_atc->send("AT+CREG?") && _atc->scanf("+CREG: %[^\n]\r\n", ret)) {
    // printf("pattern found +CREG: %s\r\n", ret);
    // sscanf(ret,"%*d,%d",&stat);

    if (sscanf(ret, "%d,%d", &n, &stat) == 2) {
      return stat;
    } else if (sscanf(ret, "%d,%d,%*s,%*s", &n, &stat) == 2) {
      return stat;
    } else {
      return -1;
    }
  }

  return -1;
}

int Sim7600Cellular::get_creg(char *payload) {
  int n = 0;
  int stat = 0;
  char ret[20];
  if (_atc->send("AT+CREG?") && _atc->scanf("+CREG: %[^\n]\r\n", ret)) {
    printf("pattern found +CREG: %s\r\n", ret);
    // sscanf(ret,"%*d,%d",&stat);
    char ret_msg[64];
    sprintf(ret_msg, "+CREG: %s", ret);
    strcpy(payload, ret_msg);
    return 1;
  }
  strcpy(payload, "");
  return -1;
}

int Sim7600Cellular::set_cereg(int n) {
  char cmd[15];
  sprintf(cmd, "AT+CEREG=%d", n);
  if (_atc->send(cmd) && _atc->recv("OK")) {
    printf("modem set---> AT+CEREG=%d\r\n", n);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_cereg(char *payload) {
  int n = 0;
  int stat = 0;
  char ret[20];

  if (_atc->send("AT+CEREG?") && _atc->scanf("+CEREG: %[^\n]\r\n", ret)) {
    printf("pattern found +CEREG: %s\r\n", ret);
    // sscanf(ret,"%*d,%d",&stat);
    char ret_msg[64];
    sprintf(ret_msg, "+CEREG: %s", ret);
    strcpy(payload, ret_msg);
    return 1;
  }
  strcpy(payload, "");
  return -1;
}

int Sim7600Cellular::set_cgact(int cid, int state) {
  char cmd[16];
  sprintf(cmd, "AT+CGACT=%d,%d", state, cid);

  if (_atc->send(cmd) && _atc->recv("OK")) {
    debug_if(state == 1, "cid=%d : PDP Activated\r\n", cid);
    debug_if(state == 0, "cid=%d : PDP Deactivated\r\n", cid);
    return state;
  }

  debug("set_cgact : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::get_cgact(int cid) {
  int xcid = -1;
  int xstate = -1;
  char buff[128];
  char rcv[16];
  char msg[12];

  sprintf(rcv, "+CGACT: %d", cid);

  _atc->set_timeout(4000);
  _atc->send("AT+CGACT?");
  _atc->read(buff, 128);
  _atc->set_timeout(8000);
  _atc->flush();

  int st = 0;
  while ((strncmp(&buff[st], rcv, strlen(rcv)) != 0) && (st < 128)) {
    st++;
  }

  memset(msg, 0, 12);
  memcpy(&msg[0], &buff[st], 11);

  if (sscanf(msg, "+CGACT: %d,%d", &xcid, &xstate) == 2) {
    debug("get_cgact: cid= %d state= %d\r\n", xcid, xstate);
    return xstate;
  }

  debug_if(xstate < 0, "get_cgact : pattern check fail\r\n");
  return xstate;
}

bool Sim7600Cellular::set_full_FUNCTION(int rst) {
  bool bcops = false;
  bool bcfun = false;
  char _cmd[16];

  sprintf(_cmd, "AT+CFUN=1,%d", rst);

  if (set_cops() == 1) {
    bcops = true;
  }

  if (_atc->send(_cmd) && _atc->recv("OK")) {
    printf("set---> AT+CFUN=1\r\n");
    bcfun = true;
  }
  return bcops && bcfun;
}

bool Sim7600Cellular::set_min_cFunction() {
  if (_atc->send("AT+CFUN=0") && _atc->recv("OK")) {
    printf("set_min_cFunction---> AT+CFUN=0\r\n");
    return true;
  }
  return false;
}
int Sim7600Cellular::get_cfun_mode() {
  int cmode = -1;

  if (_atc->send("AT+CFUN?") && _atc->scanf("+CFUN: %d", &cmode)) {
    printf("current cfun mode = %d\r\n", cmode);
    return cmode;
  }
  return cmode;
}

int Sim7600Cellular::get_revID(char *revid) {
  char _revid[20];
  if (_atc->send("AT+CGMR") && _atc->scanf("+CGMR: %[^\n]\r\n", _revid)) {
    printf("+CGMR: %s\r\n", _revid);
    strcpy(revid, _revid);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_IMEI(char *simei) {
  char _imei[16];
  if (_atc->send("AT+SIMEI?") && _atc->scanf("+SIMEI: %[^\n]\r\n", _imei)) {
    // printf("imei=  %s\r\n", _imei);
    strcpy(simei, _imei);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_ICCID(char *ciccid) {
  char _iccid[20];
  if (_atc->send("AT+CICCID") && _atc->scanf("+ICCID: %[^\n]\r\n", _iccid)) {
    // printf("iccid= %s\r\n", _iccid);
    strcpy(ciccid, _iccid);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::set_pref_Mode(int mode) {
  char _cmd[20];
  sprintf(_cmd, "AT+CNMP=%d", mode);
  _atc->set_timeout(12000);
  if (_atc->send(_cmd) && _atc->recv("OK")) {
    printf("set Preferred Mode -> AT+CNMP=%d\r\n", mode);
    _atc->set_timeout(8000);
    return 1;
  }

  _atc->set_timeout(8000);
  return -1;
}

int Sim7600Cellular::get_pref_Mode() {
  int ret = 0;
  if (_atc->send("AT+CNMP?") && _atc->recv("+CNMP: %d\r\n", &ret) &&
      _atc->recv("OK")) {
    printf("+CNMP: %d\r\n", ret);
    // strcpy(ciccid, _iccid);
    return ret;
  }
  return -1;
}

int Sim7600Cellular::set_acq_order(int a1, int a2, int a3, int a4, int a5,
                                   int a6) {
  char _cmd[30];
  sprintf(_cmd, "AT+CNAOP=7,%d,%d,%d,%d,%d,%d", a1, a2, a3, a4, a5, a6);
  if (_atc->send(_cmd) && _atc->recv("OK")) {
    printf("set_acq_order -> %s\r\n", _cmd);
    // strcpy(ciccid, _iccid);
    return 1;
  }
  return -1;
}
int Sim7600Cellular::get_acq_order() {
  char ret[30];
  if (_atc->send("AT+CNAOP?") && _atc->scanf("+CNAOP: %[^\n]\r\n", ret)) {
    printf("+CNAOP: %s\r\n", ret);
    // strcpy(ciccid, _iccid);
    return 1;
  }
  return -1;
}

int Sim7600Cellular::get_IPAddr(char *ipaddr) {
  char _ipaddr[32];
  if (_atc->send("AT+CGPADDR=1") &&
      _atc->scanf("+CGPADDR: 1,%[^\n]\r\n", _ipaddr)) {
    strcpy(ipaddr, _ipaddr);
    return 1;
  }
  strcpy(ipaddr, "0.0.0.0");
  return -1;
}

int Sim7600Cellular::get_cpsi(char *cpsi) {
  char _cpsi[128];
  if (_atc->send("AT+CPSI?") && _atc->scanf("+CPSI: %[^\n]\r\n", _cpsi)) {
    // strcpy(cpsi, _ipaddr);
    sprintf(cpsi, "+CPSI: %s", _cpsi);
    return 1;
  }
  strcpy(cpsi, "");
  return -1;
}

int Sim7600Cellular::set_tz_update(int en) {
  char cmd[10];
  sprintf(cmd, "AT+CTZU=%d", en);

  if (_atc->send(cmd) && _atc->recv("OK")) {
    if (en) {
      printf("Enable Automatic timezone update via NITZ\r\n");
      return 1;
    } else {
      printf("Disable Automatic timezone update via NITZ\r\n");
      return 0;
    }
  }
  return -1;
}

int Sim7600Cellular::dns_resolve(char *src, char *dst) {
  int ip1 = 0, ip2 = 0, ip3 = 0, ip4 = 0;
  char ret_dns[128];
  char cmd[150];
  char result[16];

  if (sscanf(src, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) == 4) {
    strcpy(dst, src);
    printf("host is ip --> %d.%d.%d.%d\r\n", ip1, ip2, ip3, ip4);
    return 0;
  } else {
    sprintf(cmd, "AT+CDNSGIP=\"%s\"", src);
    if (_atc->send(cmd) && _atc->scanf("+CDNSGIP: %[^\n]\r\n", ret_dns) &&
        _atc->recv("OK")) {
      printf("dns resolve Complete --> %s\r\n", ret_dns);

      if (sscanf(ret_dns, "%*d,\"%*[^\"]\",\"%[^\"]\"", result) == 1) {
        printf("host ip --> %s\r\n", result);
        strcpy(dst, result);
        return 1;
      }
      strcpy(dst, "");
      return -1;
    }
    strcpy(dst, "");
    return -1;
  }
}

int Sim7600Cellular::ping_dstNW(char *dst, int nrty, int p_size,
                                int dest_type) {

  printf("\r\n-------- uping_dstNW()!!! -------->  [%s]\r\n", dst);

  char cping[100];

  sprintf(cping, "AT+CPING=\"%s\",%d,%d,%d,1000,10000,255", dst, dest_type,
          nrty, p_size);
  //   printf("CMD : %s\r\n", cping);

  _atc->set_timeout(12000);
  _atc->flush();

  int len = 64 * nrty;
  char pbuf[len];
  memset(pbuf, 0, len);

  if (_atc->send(cping) && _atc->recv("OK")) {

    _atc->read(pbuf, len);
  }

  _atc->set_timeout(8000);

  int st = 0, end;
  //   char end_text[] = {0x0d, 0x0a, 0x00, 0x00};
  char end_text[] = {0x0d, 0x0a};

  while ((strncmp(&pbuf[st], "+CPING: 3", 9) != 0) && (st < len)) {
    st++;
  }

  end = st;
  while ((strncmp(&pbuf[end], end_text, 2) != 0) && (end < len)) {
    end++;
  }

  // printf("st=%d end=%d\r\n", st, end);

  char res_ping[end - st + 1];
  memset(res_ping, 0, end - st + 1);
  memcpy(&res_ping, &pbuf[st], end - st);
  printf("res_ping = %s\r\n", res_ping);

  int num_sent = nrty, num_recv = 0, num_lost = 0, min_rtt = 0, max_rtt = 0,
      avg_rtt = 0;

  sscanf(res_ping, "+CPING: 3,%d,%d,%d,%d,%d,%d", &num_sent, &num_recv,
         &num_lost, &min_rtt, &max_rtt, &avg_rtt);

  printf("min=%d max=%d avg=%d\r\n", min_rtt, max_rtt, avg_rtt);
  if (num_sent == num_recv) {
    printf("ping =>  %s  [OK] rtt = %d ms.\r\n", dst, avg_rtt);
    printf("---------------------------------------------\r\n");

    // free(pmask);  //for using with pointer

    return avg_rtt;
  } else {
    printf("ping => %s [Fail]\r\n", dst);
    printf("---------------------------------------------\r\n");
    // return false;

    // free(pmask);	//for using with pointer

    return 9999;
  }
}

bool Sim7600Cellular::delete_allsms() {
  if (_atc->send("AT+CMGD=1,4") && _atc->recv("OK")) {
    debug("delete all sms complete ...\r\n");
    return true;
  }
  return false;
}

bool Sim7600Cellular::mqtt_start() {
  bool bmqtt_start = false;
  //   if (_atc->send("AT+CMQTTSTART") && _atc->recv("OK")
  //   &&_atc->recv("+CMQTTSTART: 0")) {
  if (_atc->send("AT+CMQTTSTART") && _atc->recv("+CMQTTSTART: 0")) {
    // printf("AT+CMQTTSTART --> Completed\r\n");
    bmqtt_start = true;
  }
  return bmqtt_start;
}

bool Sim7600Cellular::mqtt_stop() {
  bool bmqtt_start = false;
  if (_atc->send("AT+CMQTTSTOP") && _atc->recv("+CMQTTSTOP: 0") &&
      _atc->recv("OK")) {
    printf("mqtt stop --> Completed\r\n");
    bmqtt_start = true;
  }
  debug_if(!bmqtt_start, "MQTT stop : Pattern Fail!\r\n");
  return bmqtt_start;
}

bool Sim7600Cellular::mqtt_release(int clientindex) {
  char cmd[32];
  sprintf(cmd, "AT+CMQTTREL=%d", clientindex);
  if (_atc->send(cmd) && _atc->recv("OK")) {
    printf("Release mqtt client : index %d\r\n", clientindex);
    return true;
  }

  printf("Release mqtt client : Pattern Fail!\r\n");
  return false;
}

bool Sim7600Cellular::mqtt_accquire_client(char *clientName) {
  char cmd[128];
  sprintf(cmd, "AT+CMQTTACCQ=0,\"%s\"", clientName);
  if (_atc->send(cmd) && _atc->recv("OK")) {
    printf("set client id -> %s\r\n", cmd);
    return true;
  }
  return false;
}

bool Sim7600Cellular::mqtt_connect(char *broker_ip, char *usr, char *pwd,
                                   int port, int clientindex) {
  char cmd[128];
  int index = 0;
  int err = 0;
  //   bool bmqtt_cnt = false;
  sprintf(cmd, "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",60,0,\"%s\",\"%s\"",
          broker_ip, port, usr, pwd);
  //   printf("connect cmd -> %s\r\n", cmd);

  if (_atc->send(cmd) && _atc->recv("OK") &&
      _atc->recv("+CMQTTCONNECT: %d,%d\r\n", &index, &err)) {
    // if (_parser->send(cmd)) {
    // printf("connect cmd -> %s" CRLF, cmd);

    if ((index == clientindex) && (err == 0)) {
      //   printf("MQTT connected\r\n");
      return true;
    }
    printf("MQTT connect : index=%d err=%d\r\n", index, err);
    return false;
  }
  printf("MQTT connect : Pattern Fail!\r\n");
  return false;
}

int Sim7600Cellular::mqtt_connect_stat() {
  char _ret[128];
  if (_atc->send("AT+CMQTTCONNECT?") &&
      _atc->scanf("+CMQTTCONNECT: %[^\n]\r\n", _ret)) {
    // strcpy(cpsi, _ipaddr);

    int nret = 0, keepAlive = 0, clean = 0;
    // 0,"tcp://188.166.189.39:1883",60,0,"IoTdevices","devices@iot"
    if (sscanf(_ret, "%d,\"%*[^\"]\",%d,%d,\"%*[^\"]\",\"%*[^\"]\"", &nret,
               &keepAlive, &clean) == 3) {
      debug("client_index=%d keepalive=%d clean_session=%d\r\n", nret,
            keepAlive, clean);
      //   sprintf(ret_msg, "+CMQTTCONNECT: %s", _ret);
      return 1;

    } else {
      //   strcpy(ret_msg, "Disconnected\r\n");
      return 0;
    }
  }
  return -1;
}

int Sim7600Cellular::mqtt_connect_stat(char *ret_msg) {
  char _ret[128];
  if (_atc->send("AT+CMQTTCONNECT?") &&
      _atc->scanf("+CMQTTCONNECT: %[^\n]\r\n", _ret)) {
    // strcpy(cpsi, _ipaddr);

    int nret = 0, keepAlive = 0, clean = 0;
    // 0,"tcp://188.166.189.39:1883",60,0,"IoTdevices","devices@iot"
    if (sscanf(_ret, "%d,\"%*[^\"]\",%d,%d,\"%*[^\"]\",\"%*[^\"]\"", &nret,
               &keepAlive, &clean) == 3) {

      sprintf(ret_msg, "+CMQTTCONNECT: %s", _ret);
      return 1;

    } else {
      strcpy(ret_msg, "Disconnected\r\n");
      return 0;
    }
  }
  return -1;
}

int Sim7600Cellular::mqtt_isdisconnect(int clientindex) {
  char rcv[20];
  int disc_state = 0;

  sprintf(rcv, "+CMQTTDISC: %d,%%d\r\n", clientindex);
  //   debug("rcv= %s\r\n", rcv);

  int cnt_state = -1;

  if ((_atc->send("AT+CMQTTDISC?")) && (_atc->recv(rcv, &disc_state)) &&
      (_atc->recv("OK"))) {
    cnt_state = (disc_state == 0) ? 1 : 0;

    debug_if(cnt_state == 1, "client_index=%d : MQTT Connected\r\n",
             clientindex);
    debug_if(cnt_state == 0, "client_index=%d : MQTT Disconnected\r\n",
             clientindex);

    return cnt_state;
  }

  debug("mqtt_isdisconnect pattern checking fail!\r\n");
  return -1;
}

int Sim7600Cellular::mqtt_disconnect(int client_index, int timeout_sec) {
  char cmd[128];
  int ret[2] = {-1, -1};
  //   _atc->debug_on(1);
  sprintf(cmd, "AT+CMQTTDISC=%d,%d", client_index, timeout_sec);

  if ((_atc->send(cmd)) &&
      (_atc->recv("+CMQTTDISC: %d,%d\r\n", &ret[0], &ret[1]))) {

    debug_if((ret[0] == client_index) && (ret[1] == 0),
             "client_index=%d : MQTT Disconnected\r\n", client_index);

    debug("mqtt_disconnect -> pattern checked : index=%d err=%d\r\n", ret[0],
          ret[1]);
  }

  //   _atc->debug_on(0);
  debug_if(ret[1] < 0, "mqtt_disconnect pattern checking fail!\r\n");
  return ret[1];
}

bool Sim7600Cellular::mqtt_publish(char topic[128], char payload[512], int qos,
                                   int interval_s) {
  int len_topic = 0;
  int len_payload = 0;
  char cmd_pub_topic[32];
  char cmd_pub_msg[32];
  char cmd_pub[32];

  len_topic = strlen(topic);
  len_payload = strlen(payload);

  sprintf(cmd_pub_topic, "AT+CMQTTTOPIC=0,%d", len_topic);
  sprintf(cmd_pub_msg, "AT+CMQTTPAYLOAD=0,%d", len_payload);
  sprintf(cmd_pub, "AT+CMQTTPUB=0,%d,%d", qos, interval_s);

  //   debug("cmd_pub_topic= %s\r\n", cmd_pub_topic);
  _atc->flush();
  _atc->send(cmd_pub_topic);

  if (_atc->recv(">")) {

    if ((_atc->write(topic, len_topic) == len_topic) && _atc->recv("OK")) {
      printf("set topic : %s Done!\r\n", topic);
    }
  }

  //   debug("cmd_pub_msg= %s\r\n", cmd_pub_msg);
  _atc->flush();
  if (_atc->send(cmd_pub_msg) && _atc->recv(">")) {
    if ((_atc->write(payload, len_payload) == len_payload) &&
        _atc->recv("OK")) {
      printf("set pubmsg : %s : Done!\r\n", payload);
    }
  }

  //   debug("cmd_pub= %s\r\n", cmd_pub);
  ThisThread::sleep_for(1s);
  _atc->flush();
  _atc->set_timeout(12000);

  if (_atc->send(cmd_pub) && _atc->recv("OK") && _atc->recv("+CMQTTPUB: 0,0")) {
    printf("Publish msg : Done!!!\r\n");
    _atc->set_timeout(8000);
    return true;
  }

  _atc->set_timeout(8000);
  return false;
}

bool Sim7600Cellular::mqtt_sub(char topic[128], int clientindex, int qos) {
  int len_topic = 0;
  char cmd_sub_topic[32];
  char cmd_sub[32];
  char cmd_ret_sub[32];

  len_topic = strlen(topic);

  sprintf(cmd_sub_topic, "AT+CMQTTSUBTOPIC=%d,%d,%d", clientindex, len_topic,
          qos);
  sprintf(cmd_sub, "AT+CMQTTSUB=%d", clientindex);
  sprintf(cmd_ret_sub, "+CMQTTSUB: %d,0", clientindex);

  printf("cmd_sub_topic= %s\r\n", cmd_sub_topic);
  //   _atc->flush();
  _atc->send(cmd_sub_topic);

  if (_atc->recv(">")) {

    if ((_atc->write(topic, len_topic) == len_topic) && _atc->recv("OK")) {
      printf("set topic : %s Done!\r\n", topic);
    }
  }

  ThisThread::sleep_for(1s);
  //   _atc->flush();
  _atc->set_timeout(12000);

  if (_atc->send(cmd_sub) && _atc->recv("OK") && _atc->recv(cmd_ret_sub)) {
    printf("Subscribe Topic : Done!!!\r\n");
    _atc->set_timeout(8000);
    return true;
  }

  _atc->set_timeout(8000);
  return false;
}

bool Sim7600Cellular::mqtt_unsub(char topic[128], int clientindex, int dup) {
  int len_topic = 0;
  char cmd_unsub_topic[32];
  char cmd_unsub[32];
  char cmd_ret_unsub[32];

  len_topic = strlen(topic);

  sprintf(cmd_unsub_topic, "AT+CMQTTUNSUBTOPIC=%d,%d", clientindex, len_topic);
  sprintf(cmd_unsub, "AT+CMQTTUNSUB=%d,%d", clientindex, dup);
  sprintf(cmd_ret_unsub, "+CMQTTUNSUB: %d,0", clientindex);

  printf("cmd_unsub_topic= %s\r\n", cmd_unsub_topic);

  //   _atc->flush();
  _atc->send(cmd_unsub_topic);

  if (_atc->recv(">")) {

    if ((_atc->write(topic, len_topic) == len_topic) && _atc->recv("OK")) {
      printf("set topic : %s Done!\r\n", topic);
    }
  }

  ThisThread::sleep_for(1s);
  //   _atc->flush();
  _atc->set_timeout(12000);

  if (_atc->send(cmd_unsub) && _atc->recv("OK") && _atc->recv(cmd_ret_unsub)) {
    printf("Unsubscribe Topic : Done!!!\r\n");
    _atc->set_timeout(8000);
    return true;
  }

  _atc->set_timeout(8000);
  return false;
}

bool Sim7600Cellular::http_start() {
  bool ret = false;
  _atc->set_timeout(30000);

  if (_atc->send("AT+HTTPINIT") && _atc->recv("OK")) {
    ret = true;
  }

  debug_if(!ret, "http_start() : checking pattern fail\r\n");
  _atc->set_timeout(8000);
  return ret;
}

bool Sim7600Cellular::http_stop() {
  bool ret = false;
  _atc->set_timeout(30000);

  if (_atc->send("AT+HTTPTERM") && _atc->recv("OK")) {
    ret = true;
  }

  debug_if(!ret, "http_stop() : checking pattern fail\r\n");
  _atc->set_timeout(8000);
  return ret;
}

bool Sim7600Cellular::http_set_parameter(char url[128], int content,
                                         int readmode) {

  bool ret = false;
  bool temp = false;
  _atc->set_timeout(30000);

  if (_atc->send("AT+HTTPPARA=\"URL\",\"%s\"", url) && _atc->recv("OK")) {
    debug("set parameter : url -> %s\r\n", url);
    temp = true;
  }

  if (content == 0) {
    ret = temp && _atc->send("AT+HTTPPARA=\"CONTENT\",\"%s\"", "text/plain") &&
          _atc->recv("OK");
    temp = ret;
  } else if (content == 1) {
    ret = temp &&
          _atc->send("AT+HTTPPARA=\"CONTENT\",\"%s\"",
                     "application/octet-stream") &&
          _atc->recv("OK");
    temp = ret;
  } else {
    ret = temp &&
          _atc->send("AT+HTTPPARA=\"CONTENT\",\"%s\"", "multipart/form-data") &&
          _atc->recv("OK");
    temp = ret;
  }

  debug_if(!ret, "http_set_parameter() : checking pattern fail\r\n");
  _atc->set_timeout(8000);
  return ret;
}

bool Sim7600Cellular::http_method_action(int *datalen, int method) {

  bool ret = false;
  int status = -1;
  _atc->set_timeout(30000);

  if (_atc->send("AT+HTTPACTION=%d", method) && _atc->recv("OK") &&
      _atc->recv("+HTTPACTION: %d,%d,%d\r\n", &method, &status, datalen)) {
    debug("http_method_action -> method=%d status=%d datalen=%d\r\n", method,
          status, *datalen);
    ret = (status == 200) ? true : false;
    // return ret;
  }
  debug_if(status < 0, "http_method_action() : checking pattern fail\r\n");
  _atc->set_timeout(8000);
  _atc->flush();
  return ret;
}

bool Sim7600Cellular::http_read_header(char *rxbuf, int *datalen) {
  bool ret = false;
  char dummy[32];
  int len_dummy = 0;

  //   _atc->debug_on(1);
  memset(buf, 0, 0x200);
  _atc->set_timeout(1500);
  _atc->send("AT+HTTPHEAD");

  _atc->read(buf, 0x200);

  //   printHEX((unsigned char *)buf, 0x200);

  int st = 0, end = 0;
  while ((memcmp(&buf[st], "+HTTPHEAD: ", 11) != 0) && (st < 0x200)) {
    st++;
  }

  *datalen = 0;
  if (st < 0x200) {
    sscanf(&buf[st], "+HTTPHEAD: %d\r\n", datalen);
    sprintf(dummy, "+HTTPHEAD: %d\r\n", *datalen);
    len_dummy = strlen(dummy);
    end = st + len_dummy + *datalen;
  }

  while ((memcmp(&buf[end], "OK\r\n", 4) != 0) && (end < 0x200)) {
    end++;
  }

  if ((st < 0x200) && (end < 0x200)) {
    rxbuf[end - st - len_dummy - 2] = '\0';
    memcpy(rxbuf, &buf[st + len_dummy], end - st - len_dummy - 2);

    // printHEX((unsigned char *)rxbuf, end - st - len_dummy - 2);

    ret = true;
  }

  debug_if(!ret, "http_read_header() : checking pattern fail\r\n");
  _atc->set_timeout(8000);
  _atc->flush();
  //   _atc->debug_on(0);
  return ret;
}

bool Sim7600Cellular::http_getsize_data(int *datalen) {
  bool ret = false;
  //   _atc->debug_on(1);
  _atc->set_timeout(30000);
  if (_atc->send("AT+HTTPREAD?") &&
      _atc->recv("+HTTPREAD: LEN,%d\r\n", datalen) && _atc->recv("OK")) {

    ret = true;
  }

  debug_if(!ret, "http_getsize_data() : checking pattern fail\r\n");
  _atc->set_timeout(8000);
  _atc->flush();
  //   _atc->debug_on(0);
  return ret;
}

bool Sim7600Cellular::http_read_data(char *rxbuf, int offset, int datalen) {
  bool ret = false;
  int xlen = datalen + 0x80;
  memset(buf, 0xff, 0x2080);

  _atc->set_timeout(1500);
  if (_atc->send("AT+HTTPREAD=%d,%d", offset, datalen) && _atc->recv("OK")) {

    _atc->read(buf, xlen);

    int st = 0, end = 0;

    while ((memcmp(&buf[st], "+HTTPREAD: ", 11) != 0) && (st < xlen)) {
      st++;
    }

    if (st < xlen) {
      end = st + datalen;
    }

    while ((memcmp(&buf[end], "\r\n+HTTPREAD: 0\r\n", 16) != 0) &&
           (end < xlen)) {
      end++;
    }

    int npart = 0;
    int partsize = 0x400;
    int last_size = 0;

    npart = datalen >> 10;
    last_size = datalen & ((1 << 10) - 1);

    if (last_size > 0) {
      npart += 1;
    }

    if (end < xlen) {

      int k = 0;
      int index = 0;
      int detect_size = 0;
      int offset_data = 0;

      while (k < (end - st)) {

        if (memcmp(&buf[st + k], "+HTTPREAD: ", 11) == 0) {

          sscanf(&buf[st + k], "+HTTPREAD: %d\r\n", &detect_size);

          if (detect_size != 0) {

            if ((last_size > 0) && (index == (npart - 1))) {
              partsize = last_size;
            }

            if (detect_size >= 1000) {
              offset_data = 17;
            } else if (detect_size >= 100) {
              offset_data = 16;
            } else if (detect_size >= 10) {
              offset_data = 15;
            } else {
              offset_data = 14;
            }

            detect_size = 0;
            memcpy(&rxbuf[index << 10], &buf[st + k + offset_data], partsize);

            index++;
          }
        }

        k++;
      }

      ret = true;
    }
  }

  debug_if(ret, "http_read_data : offset= 0x%06X size=%d bytes. ---> Done\r\n",
           offset, datalen);
  debug_if(!ret, "http_read_data() : checking pattern fail\r\n");
  _atc->set_timeout(8000);
  _atc->flush();

  return ret;
}

int Sim7600Cellular::ftp_start() {
  int err = -1;

  if (_atc->send("AT+CFTPSSTART") && _atc->recv("+CFTPSSTART: %d\r\n", &err)) {

    debug_if(err != 0, "ftp start service not complete : err= %d\r\n", err);
    return err;
  }

  debug("ftp_start() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::ftp_stop() {
  int err = -1;

  if (_atc->send("AT+CFTPSSTOP") && _atc->recv("+CFTPSSTOP: %d\r\n", &err)) {

    debug_if(err != 0, "ftp stop service not complete : err= %d\r\n", err);
    return err;
  }

  debug("ftp_stop() : pattern check fail\r\n");
  return -1;
}

bool Sim7600Cellular::ftp_set_sockaddr_type(int singleip) {

  char cmd[20];
  sprintf(cmd, "AT+CFTPSSINGLEIP=%d", singleip);

  if (_atc->send(cmd) && _atc->recv("OK")) {
    return true;
  }
  debug("ftp_set_sockaddr_type() : pattern check fail\r\n");
  return false;
}

int Sim7600Cellular::ftp_set_transfer_type(char type) {
  int err = -1;
  char cmd[20];
  sprintf(cmd, "AT+CFTPSTYPE=%c", type);

  if (_atc->send(cmd) && _atc->recv("+CFTPSTYPE: %d", &err)) {
    debug("ftp_set_transfer_type() : err=%d\r\n", err);
  }

  debug_if(err < 0, "ftp_set_transfer_type() : pattern check fail\r\n");
  return err;
}

int Sim7600Cellular::ftp_login(char srv[128], char usr[32], char pwd[32],
                               int port, int ftp_type) {
  int err = -1;
  char cmd[128];
  sprintf(cmd, "AT+CFTPSLOGIN=\"%s\",%d,\"%s\",\"%s\",%d", srv, port, usr, pwd,
          ftp_type);

  //   printf("ftp_login cmd= %s\r\n", cmd);

  if (_atc->send(cmd) && _atc->recv("+CFTPSLOGIN: %d\r\n", &err)) {

    debug_if(err != 0, "ftp login not complete : err= %d\r\n", err);
    return err;
  }

  debug("ftp_login() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::ftp_login_stat() {
  int stat = -1;

  if (_atc->send("AT+CFTPSLOGIN?") &&
      _atc->recv("+CFTPSLOGIN: %d\r\n", &stat)) {

    debug_if(stat != 1, "ftp not logged in\r\n");
    return stat;
  }

  debug("ftp_login_stat() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::ftp_logout() {
  int err = -1;

  if (_atc->send("AT+CFTPSLOGOUT") &&
      _atc->recv("+CFTPSLOGOUT: %d\r\n", &err)) {

    debug_if(err != 0, "ftp logout not complete : err= %d\r\n", err);
    return err;
  }

  debug("ftp_logout() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::ftp_get_currentdir(char *dir) {
  int err = -1;
  char ret[128];
  char rcv[128];
  char short_dir[128];

  if (_atc->send("AT+CFTPSPWD") && _atc->recv("OK") &&
      _atc->recv("+CFTPSPWD: %[^\r]\r\n", ret)) {

    // debug("ret=%s\r\n", ret);
    sprintf(rcv, "+CFTPSPWD: %s", ret);

    if (sscanf(rcv, "+CFTPSPWD: \"%[^\"]\"", short_dir) == 1) {
      strcpy(dir, short_dir);
      //   printf("current ftp remote_dir to %s\r\n", short_dir);
      err = 0;
    } else if (sscanf(rcv, "+CFTPSPWD: %d", &err) == 1) {
      //   printf("get current ftp remote_dir : err= %d\r\n", err);
    } else {
      // blank
    }

    debug_if(err >= 0, "get current remote dir : err= %d\r\n", err);
    return err;
  }

  debug("get current remote dir not complete : pattern checking fail\r\n");
  return err;
}

int Sim7600Cellular::ftp_dir_listfile(char dir[128]) {
  int err = -1;
  char cmd[128];
  sprintf(cmd, "AT+CFTPSLIST=\"%s\"", dir);

  _atc->set_timeout(12000);

  if (_atc->send(cmd) && _atc->recv("OK") &&
      _atc->recv("+CFTPSLIST: %d\r\n", &err)) {
    printf("ftp listfile remote_dir %s : err= %d\r\n", dir, err);
  }

  _atc->set_timeout(8000);
  _atc->flush();

  debug_if(err < 0,
           "get current remote dir not complete : pattern checking fail\r\n");
  return err;
}

int Sim7600Cellular::ftp_changedir(char dir[128]) {
  int err = -1;
  char cmd[150];
  sprintf(cmd, "AT+CFTPSCWD=\"%s\"", dir);

  if (_atc->send(cmd) && _atc->recv("+CFTPSCWD: %d\r\n", &err)) {
    printf("change ftp remote_dir to %s\r\n", dir);
    debug_if(err != 0, "ftp changedir not complete : err= %d\r\n", err);
    return err;
  }

  debug("ftp_changedir() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::ftp_checksize_rmtfile(char rmt_filename[64]) {
  int len = -1;
  char cmd[150];
  sprintf(cmd, "AT+CFTPSSIZE=\"%s\"", rmt_filename);

  if (_atc->send(cmd) && _atc->recv("OK") &&
      _atc->recv("+CFTPSSIZE: %d\r\n", &len)) {
  }

  debug_if(len < 0, "ftp_checksize_rmtfile() : pattern check fail\r\n");
  return len;
}

int Sim7600Cellular::ftp_delete_rmtfile(char rmt_filename[64]) {
  int err = -1;
  char cmd[150];
  sprintf(cmd, "AT+CFTPSDELE=\"%s\"", rmt_filename);

  if (_atc->send(cmd) && _atc->recv("+CFTPSDELE: %d\r\n", &err)) {
    printf("delete ftp remote_file %s\r\n", rmt_filename);
    debug_if(err != 0, "ftp_delete_rmtfile not complete : err= %d\r\n", err);
    return err;
  }

  debug("ftp_delete_rmtfile() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::ftp_downloadfile_toFS(char filepath[64], int dir_mode) {
  int err = -1;
  char cmd[128];
  sprintf(cmd, "AT+CFTPSGETFILE=\"%s\",%d", filepath, dir_mode);

  if (_atc->send(cmd) && _atc->recv("+CFTPSGETFILE: %d\r\n", &err)) {
    // printf("download ftp remote_file %s to FS Module\r\n", filepath);
    debug_if(err != 0, "ftp_downloadfile_toFS not complete : err= %d\r\n", err);
    return err;
  }

  debug("ftp_downloadfile_toFS() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::fs_getdir(char *dir) {

  char ret[64];

  if (_atc->send("AT+FSCD?") && _atc->recv("+FSCD: %[^\n]\r\n", ret)) {
    // debug("ret=%s\r\n", ret);
    memset(dir, 0, 64);
    strcpy(dir, ret);
    return 0;
  }

  debug("fs_getdir() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::fs_setdir(char *dir) {

  char cmd[128];

  int len = strlen(dir);
  if ((dir[len - 1] == '/') || (dir[len - 1] == '\\')) {
    dir[len - 1] = '\0';
  }

  sprintf(cmd, "AT+FSCD=%s", dir);
  //   printf("cmd: %s\r\n", cmd);

  if (_atc->send(cmd) && _atc->recv("OK")) {

    return 0;
  }

  debug("fs_setdir() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::fs_list_currentdir(int mode) {

  char type[32];

  if (_atc->send("AT+FSLS=%d", mode) && _atc->recv("+FSLS: %[^\n]\r\n", type) &&
      _atc->recv("OK")) {
    debug("fs_list mode= %s\r\n", type);
    return 0;
  }

  debug("fs_list_currentdir() : pattern check fail\r\n");
  return -1;
}

bool Sim7600Cellular::fs_deletefile(char filepath[64]) {

  char cmd[128];

  sprintf(cmd, "AT+FSDEL=%s", filepath);

  if (_atc->send(cmd) && _atc->recv("OK")) {

    return 0;
  }

  debug("fs_deletefile() : pattern check fail\r\n");
  return false;
}

bool Sim7600Cellular::fs_renamefile(char old_name[64], char new_name[64]) {

  char cmd[128];

  sprintf(cmd, "AT+FSRENAME=%s,%s", old_name, new_name);

  if (_atc->send(cmd) && _atc->recv("OK")) {

    return 0;
  }

  debug("fs_renamefile() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::fs_get_remainsize() {

  char dir[6];
  int totalsize, usedsize;

  if (_atc->send("AT+FSMEM") &&
      _atc->recv("+FSMEM: %[^(](%d,%d)", dir, &totalsize, &usedsize)) {
    debug("storage %s total/used -> (%d,%d) bytes.\r\n", dir, totalsize,
          usedsize);
    return totalsize - usedsize;
  }

  debug("fs_get_remainsize() : pattern check fail\r\n");
  return -1;
}

int Sim7600Cellular::fs_attributefile(char filepath[64]) {

  int len = -1;
  char cmd[128];

  sprintf(cmd, "AT+FSATTRI=%s", filepath);
  //   printf("cmd= %s\r\n", cmd);

  if (_atc->send(cmd) && _atc->recv("+FSATTRI: %d\r\n", &len)) {

    debug_if(len > 0, "len of file %s = %d bytes.\r\n", filepath, len);
    return len;
  }

  debug("fs_attributefile() : pattern check fail\r\n");
  return -1;
}

bool Sim7600Cellular::fs_download(char full_fname[128], char *rcvbuf,
                                  int offset, int len) {

  char cmd[128];
  bool tail_detect = false;
  //   char sub_part[512];

  int npart = 0;
  int last_part_size = 0;
  int part_size = 0;

  memset(buf, 0xff, 0x10e0);
  //   int dummy_len = len + 2;

  sprintf(cmd, "AT+CFTRANTX=\"%s\",%d,%d", full_fname, offset, len);

  _atc->set_timeout(3000);
  _atc->send(cmd) && _atc->recv("+CFTRANTX");

  _atc->read(buf, 0x10e0);
  printf("fs_download() -> fname = %s offset =0x%06X len =%d Bytes.\r\n",
         full_fname, offset, len);

  _atc->set_timeout(8000);
  _atc->flush();

  int k = 0;
  while ((memcmp(&buf[k], "+CFTRANTX: 0", 12) != 0) && (k < len)) {
    k++;
  }

  tail_detect = (k < len) ? true : false;

  part_size = (1 << 9);
  last_part_size = len & ((1 << 9) - 1);
  npart = len >> 9;

  if (last_part_size > 0) {
    npart += 1;
  }

  const char s[3] = ": ";
  int i = 0;
  int index = 0;
  int start_offset = 0;

  //   while ((i < len) && (index < 8)) {
  while ((i < k) && (index < 8)) {

    if (memcmp(&buf[i], s, 2) == 0) {

      //   if (last_part_size > 0) {
      //     //   num += 1;

      //     if (index == (npart - 1)) {
      //       part_size = last_part_size;
      //     }
      //   }

      if ((index == (npart - 1)) && (last_part_size > 0)) {
        part_size = last_part_size;
      }

      if (buf[i + 8] == 0x0d) {
        memcpy(&rcvbuf[index << 9], &buf[i + 10], part_size);
      } else if (buf[i + 9] == 0x0d) {
        memcpy(&rcvbuf[index << 9], &buf[i + 11], part_size);
      } else if (buf[i + 10] == 0x0d) {
        memcpy(&rcvbuf[index << 9], &buf[i + 12], part_size);
      } else {
        // blank
      }

      debug("sub part %d\r\n", index);

      start_offset = index << 9;
      printHEX((unsigned char *)&rcvbuf[start_offset], part_size);

      index++;
    }

    i++;
  }

  return tail_detect;
}

int Sim7600Cellular::read_atc_to_char(char *tbuf, int size, char end) {
  int count = 0;
  int x = 0;

  if (size > 0) {
    for (count = 0; (count < size) && (x >= 0) && (x != end); count++) {
      x = _atc->getc();
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