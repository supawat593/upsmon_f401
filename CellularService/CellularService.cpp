#include "CellularService.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>

CellularService::CellularService(PinName tx, PinName rx, DigitalOut &pwrkey,
                                 DigitalOut &rst)
    : Sim7600Cellular(tx, rx), mdm_pwr(pwrkey), mdm_rst(rst) {}

CellularService::CellularService(BufferedSerial *_serial, DigitalOut &pwrkey,
                                 DigitalOut &rst)
    : Sim7600Cellular(_serial), mdm_pwr(pwrkey), mdm_rst(rst) {}

CellularService::CellularService(ATCmdParser *_parser, DigitalOut &pwrkey,
                                 DigitalOut &rst)
    : Sim7600Cellular(_parser), mdm_pwr(pwrkey), mdm_rst(rst) {}

void CellularService::powerkey_trig_mode(bool en) {

  mdm_pwr = 0;
  ThisThread::sleep_for(100ms);

  mdm_pwr = 1; // Active LOW

  if (en) {
    debug("MDM Powerkey : ON\r\n");
    ThisThread::sleep_for(100ms);
  } else {
    debug("MDM Powerkey : OFF\r\n");
    ThisThread::sleep_for(3000ms);
  }

  mdm_pwr = 0;
}

void CellularService::MDM_HW_reset(void) {
  printf("MDM HW reset\r\n");
  mdm_rst = 0;
  ThisThread::sleep_for(100ms);
  mdm_rst = 1; // Active LOW
  ThisThread::sleep_for(3000ms);
  mdm_rst = 0;
}

bool CellularService::initial_NW() {

  bool mdmAtt = false;
  bool mdmOK = this->check_modem_status();

  if (mdmOK) {
    printf("SIM7600 Status: Ready\r\n");

    debug_if(!this->delete_allsms(), "delete all sms fail!\r\n");

    if (this->enable_echo(0)) {

      if (!this->save_setting()) {
        printf("Save ME user setting : Fail!!!\r\n");
      }
    }
  }

  this->get_revID(this->cell_info.revID);

  this->set_tz_update(0);

  if (mdmOK) {
    this->set_cops();
    this->set_full_FUNCTION();
  }

  debug_if(this->get_IMEI(cell_info.imei) > 0, "imei=  %s\r\n", cell_info.imei);
  debug_if(this->get_ICCID(cell_info.iccid) > 0, "iccid=  %s\r\n",
           cell_info.iccid);

  debug_if(!this->set_attachNW(1), "Set_attachNW Fail!\r\n");

  int nty = 0;
  //   while (!mdmAtt) {
  while ((!mdmAtt) && (nty < 5)) {
    ThisThread::sleep_for(3000ms);
    mdmAtt = this->check_attachNW();
    nty++;
  }

  debug_if(!mdmAtt, "NW Attaching Fail ...\r\n");
  this->get_csq(&cell_info.sig, &cell_info.ber);

  while (this->get_creg() != 1)
    ;

  if (this->get_creg() > 0) {
    printf("NW Registered!!!\r\n");
    return true;
  }
  return false;
}

void CellularService::ntp_setup(const char *srv, int tz_q) {

  char ret_ntp[64];
  this->set_ntp_srv((char *)srv, tz_q);
  debug_if(this->get_ntp_srv(ret_ntp) > 0, "ntp_srv--> +CNTP: %s\r\n", ret_ntp);
  debug_if(this->check_ntp_status(), "NTP: Operation succeeded\r\n");
}

void CellularService::sync_rtc(char cclk[64]) {
  int qdiff = 0;
  char chdiff[4];
  // 20/11/02,00:17:48+28
  printf("sync_rtc(\"%s\")\r\n", cclk);
  sscanf(cclk, "%d/%d/%d,%d:%d:%d%s", &struct_tm.tm_year, &struct_tm.tm_mon,
         &struct_tm.tm_mday, &struct_tm.tm_hour, &struct_tm.tm_min,
         &struct_tm.tm_sec, chdiff);
  /*
      struct_tm.tm_sec=second;
      struct_tm.tm_min=minute;
      struct_tm.tm_hour=hour;
      struct_tm.tm_mday=day;
      struct_tm.tm_mon=month-1;
      struct_tm.tm_year=year+100;  //+2000-1900
  */
  qdiff = atoi(chdiff);
  qdiff /= 4;

  struct_tm.tm_mon -= 1;
  struct_tm.tm_year += 100;

  time_t sec_rtc = mktime(&struct_tm);

  unsigned int temp = 0;
  temp = (unsigned int)sec_rtc - (qdiff * 3600);
  sec_rtc = (time_t)temp;

  if ((int)sec_rtc > 0) {
    rtc_write(sec_rtc);
  }
}

void CellularService::ctrl_timer(bool in) { in ? tm1.start() : tm1.stop(); }

int CellularService::read_systime_ms() {
  return duration_cast<chrono::milliseconds>(tm1.elapsed_time()).count();
}

int CellularService::read_systime_sec() {
  return duration_cast<chrono::seconds>(tm1.elapsed_time()).count();
}