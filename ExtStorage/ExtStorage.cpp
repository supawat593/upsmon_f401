
#include "./ExtStorage.h"
#include <cstdio>
#include <cstring>

const char xinit_cfg_write[] = {
    "#Configuration file for UPS Monitor\r\n\r\nSTART:\r\nBroker: "
    "\"%s\"\r\nPort: %d\r\nKey: \"%s\"\r\nURL: \"%s\"\r\nTopic: "
    "\"%s\"\r\nCommand: "
    "[%s]\r\nModel: \"%s\"\r\nSite_ID: \"%s\"\r\nSTOP:"};

const char xinit_cfg_pattern[] = {"%*[^\n]\nBroker: \"%[^\"]\"\nPort: %d\nKey: "
                                  "\"%[^\"]\"\nURL: "
                                  "\"%[^\"]\"\nTopic: \"%[^\"]\"\nCommand: "
                                  "[%[^]]]\nModel: \"%[^\"]\"\nSite_ID: "
                                  "\"%[^\"]\"\n%*s"};

ExtStorage::ExtStorage(BlockDevice *_bd, FATFileSystem *_fs) {
  bd = _bd;
  fs = _fs;
  is_script_read = false;
}
bool ExtStorage::init() {
  printf("init_upsmon_filesystem \r\n");

  int err_bd = bd->init();
  debug_if(err_bd != 0, "bd.init() -> err =%d\r\n", err_bd);
  int err_fs = fs->mount(bd);
  debug_if(err_fs != 0, "fs.mount(&bd) -> err =%d\r\n", err_fs);

  return (err_bd == 0) && (err_fs == 0);
}
bool ExtStorage::deinit() {
  printf("deinit_upsmon_filesystem \r\n");

  int err_fs = fs->unmount();
  debug_if(err_fs != 0, "fs.unmount() -> err =%d\r\n", err_fs);
  int err_bd = bd->deinit();
  debug_if(err_bd != 0, "bd.deinit() -> err =%d\r\n", err_bd);

  return (err_bd == 0) && (err_fs == 0);
}

void ExtStorage::write_init_script(init_script_t *_script, char path[128],
                                   const char *fopen_mode) {

  file_mtx.lock();
  file = fopen(path, fopen_mode);
  debug_if(file == NULL, "*** fopen fail ! ***\r\n");

  if (file != NULL) {

    // debug("initial script found\r\n");
    char fbuffer[512];
    sprintf(fbuffer, xinit_cfg_write, _script->broker, _script->port,
            _script->encoded_key, _script->url_path, _script->topic_path,
            _script->full_cmd, _script->model, _script->siteID);

    // sprintf(fbuffer, "Hello : %d\r\n",15);

    debug_if(strlen(fbuffer) > 0,
             "< ------------ fbuffer ------------ >\r\n%s\r\n"
             "< --------------------------------- >\r\n",
             fbuffer);
    fprintf(file, fbuffer);
  }

  fclose(file);
  file_mtx.unlock();
}

void ExtStorage::read_init_script(init_script_t *_script, char path[128],
                                  const char *fopen_mode) {

  file_mtx.lock();
  file = fopen(path, fopen_mode);
  debug_if(file == NULL, "*** fopen fail ! ***\r\n");

  if (file != NULL) {
    // printf("initial script found\r\n");

    // apply_update(file, POST_APPLICATION_ADDR );
    apply_script(file, path, _script);
    // remove(FULL_UPDATE_FILE_PATH);
  }
  //  else {
  //     printf("No update found to apply\r\n");
  // }

  file_mtx.unlock();
}

void ExtStorage::apply_script(FILE *file, char path[128],
                              init_script_t *_script) {

  fseek(file, 0, SEEK_END);
  long len = ftell(file);
  printf("init_script have found : filesize = %ld bytes\r\n", len);
  fseek(file, 0, SEEK_SET);

  int result = 0;
  // allocate memory to contain the whole file:
  char *buffer = (char *)malloc(sizeof(char) * len);
  if (buffer == NULL) {
    printf("malloc : Memory error\r\n");
  }

  // copy the file into the buffer:
  result = fread(buffer, 1, len, file);
  if (result != len) {
    printf("file %s : Reading error\r\n", path);
  }

  //   printHEX((unsigned char *)buffer, len);
  //   printf("buffer -> \r\n%s\r\n\n", buffer);

  char script_buff[len];
  int idx = 0;
  while ((strncmp(&buffer[idx], "START:", 6) != 0) && (idx < len - 6)) {
    idx++;
  }
  if (idx < len) {
    memcpy(&script_buff, &buffer[idx], len - idx);
    // printf("script_buf: %s" CRLF, script_buff);
  }

  char key_encoded[64];

  if (sscanf(script_buff, xinit_cfg_pattern, _script->broker, &_script->port,
             key_encoded, _script->url_path, _script->topic_path,
             _script->full_cmd, _script->model, _script->siteID) == 8) {
    if (_script->topic_path[strlen(_script->topic_path) - 1] == '/') {
      _script->topic_path[strlen(_script->topic_path) - 1] = '\0';
    }

    printf("\r\n<---------------------------------------->\r\n");
    printf("    broker: %s\r\n", _script->broker);
    printf("    port: %d\r\n", _script->port);
    printf("    Key: %s\r\n", key_encoded);
    printf("    URL: %s\r\n", _script->url_path);
    // printf("    usr: %s" CRLF, init_script.usr);
    // printf("    pwd: %s" CRLF, init_script.pwd);
    printf("    topic_path: %s\r\n", _script->topic_path);
    printf("    full_cmd: %s\r\n", _script->full_cmd);
    printf("    model: %s\r\n", _script->model);
    printf("    siteID: %s\r\n", _script->siteID);
    printf("<---------------------------------------->\r\n\n");

    char key_encoded2[64];
    char *key_decode2;
    strcpy(key_encoded2, key_encoded);

    size_t len_key_encode = (size_t)strlen(key_encoded2);
    size_t len_key_decode;

    for (int ix = 0; ix < 2; ix++) {
      key_decode2 =
          base64.Decode(key_encoded2, len_key_encode, &len_key_decode);

      len_key_encode = len_key_decode;
      strcpy(key_encoded2, key_decode2);
    }

    // printf("key_decode2= %s\r\n\n", key_decode2);

    if (sscanf(key_decode2, "%[^ ] %[^\n]", _script->usr, _script->pwd) == 2) {
      //   printf("usr=%s pwd=%s\r\n", init_script.usr, init_script.pwd);
      is_script_read = true;
      strcpy(_script->encoded_key, key_encoded);
    }
  }

  /* the whole file is now loaded in the memory buffer. */

  // terminate
  fclose(file);
  free(buffer);
}

bool ExtStorage::get_script_flag() { return is_script_read; }
void ExtStorage::write_data_log(char msg[256], char path[128]) {

  static char temp_msg[256];
  memset(temp_msg, 0, 256);
  strcpy(temp_msg, msg);
  strcat(temp_msg, "\r\n");

  file_mtx.lock();
  file = fopen(path, "a+");
  debug_if(file == NULL, "*** fopen logfile fail ! ***\r\n");

  if (file != NULL) {

    fprintf(file, temp_msg);

    int len = strlen(temp_msg);
    temp_msg[len - 2] = '\0';

    debug_if(len > 0, "\r\n<----- Log %s Appending ----->\r\nPayload-> %s\r\n",
             path, temp_msg);
  }

  fclose(file);
  file_mtx.unlock();
}

int ExtStorage::check_filesize(char full_path[128], const char *fopen_mode) {
  file_mtx.lock();
  //   char full_path[128];
  long len = 0;
  //   sprintf(full_path, "/%s/%s", SPIF_MOUNT_PATH, path);
  file = fopen(full_path, fopen_mode);

  if (file != NULL) {
    fseek(file, 0, SEEK_END);
    len = ftell(file);
    fseek(file, 0, SEEK_SET);
    fclose(file);
  } else {
    len = -1;
  }
  file_mtx.unlock();
  return len;
}

bool ExtStorage::upload_log(CellularService *_modem, char full_path[128],
                            char topic[128]) {
  char line[256];
  volatile bool pub_complete = false;
  volatile bool tflag = true;

  file_mtx.lock();
  file = fopen(full_path, "r");

  if (file != NULL) {
    int rec = 0;
    //   while (fgets(line, 256, textfile)) {
    while ((fgets(line, 256, file) != NULL) && tflag) {
      // printf("\r\n%s\r\n", line);
      int len = strlen(line);
      line[len - 2] = '\0';
      // modem->mqtt_publish(mqtt_obj->mqttpub_topic, line);
      debug("\r\nrecord number: %d --->\r\n", ++rec);

      pub_complete = tflag && _modem->mqtt_publish(topic, line);
      tflag = pub_complete;
    }

    fclose(file);
  } else {
    printf("fopen log fail!\r\n");
  }

  file_mtx.unlock();
  return pub_complete;
}

bool ExtStorage::process_ota(CellularService *_modem, init_script_t *_script) {

  int len = 0;
  char file_url[128];
  char str[128];
  bool ota_complete = false;
  bool method_action = false;

  xbuffer = (char *)malloc(BLOCKSIZE_4K * sizeof(char));
  if (xbuffer == NULL) {
    debug("allocating xbuffer[BLOCKSIZE_4K] incomplete!!!\r\n");
    return false;
  }

  if (!_modem->http_start()) {
    debug("http_start() incomplete!!!\r\n");
    free(xbuffer);
    return false;
  }

  const char s[2] = "/";
  char cut_msg[8][32];
  char dummy_filename[32];
  char *token;
  int i = 0;

  strcpy(str, _script->ota_data.filename);
  /* get the first token */
  token = strtok(str, s);

  /* walk through other tokens */
  while (token != NULL) {
    // printf(" %s\r\n", token);

    //      memset(cut_msg[i], 0, 16);
    strcpy(cut_msg[i], token);
    i++;
    token = strtok(NULL, s);
  }

  if (i > 1) {
    if (sscanf(cut_msg[i - 1], "%[^.].bin", dummy_filename) == 1) {

      strcat(_script->ota_data.dir_path, cut_msg[0]);

      if (i > 2) {
        for (int k = 1; k < (i - 1); k++) {
          strcat(_script->ota_data.dir_path, "/");
          strcat(_script->ota_data.dir_path, cut_msg[k]);
        }
      }

      memset(_script->ota_data.filename, 0, 128);
      strcpy(_script->ota_data.filename, cut_msg[i - 1]);

      //   debug("dir_path: %s\r\nfilename: %s\r\n", _script->ota_data.dir_path,
      //         _script->ota_data.filename);
    }
  }

  debug("url_path : %s\r\ndir_path: %s\r\nfilename: %s\r\n", _script->url_path,
        _script->ota_data.dir_path, _script->ota_data.filename);

  memset(file_url, 0, 128);
  strcpy(file_url, _script->url_path);
  strcat(file_url, "/");
  strcat(file_url, _script->ota_data.dir_path);
  strcat(file_url, "/");
  strcat(file_url, _script->ota_data.filename);

  // modem->http_set_parameter(url_http);
  _modem->http_set_parameter(file_url);
  method_action = _modem->http_method_action(&len);

  //   if (_modem->http_method_action(&len)) {
  if (method_action) {

    debug("method_action : datalen = % d\r\n ", len);

    memset(xbuffer, 0, BLOCKSIZE_4K);
    debug_if(_modem->http_read_header(xbuffer, &len),
             "read_header : len = % d\r\n\n<---------- header_buf "
             "---------->\r\n%s\r\n\n ",
             len, xbuffer);
    memset(xbuffer, 0xff, BLOCKSIZE_4K);

    debug_if(_modem->http_getsize_data(&len), "getsize_data datalen=%d\r\n",
             len);

    debug("datalen=%d script_datalen=%d\r\n", len,
          _script->ota_data.file_length);
    if (_script->ota_data.file_length == len) {
      int num = 0;
      int subsize = BLOCKSIZE_4K;
      int last_subpart = 0;

      if (len > 0) {

        num = len >> 12;
        last_subpart = len & ((1 << 12) - 1);

        if (last_subpart > 0) {
          num += 1;
        }

        file_mtx.lock();
        file = fopen("/spif/firmware.bin", "wb");
        if (file != NULL) {
          //
          unsigned int crc = 0;
          MbedCRC<POLY_32BIT_ANSI, 32> ct;
          ct.compute_partial_start(&crc);

          int x = 0;
          bool part_complete = true;

          while ((x < num) && part_complete) {

            if ((last_subpart > 0) && (x == (num - 1))) {
              subsize = last_subpart;
            }

            part_complete = _modem->http_read_data(xbuffer, (x << 12), subsize);

            if (part_complete) {
              ct.compute_partial((void *)xbuffer, subsize, &crc);
              fwrite((const char *)xbuffer, 1, subsize, file);
              // printHEX((unsigned char *)xbuffer, subsize);
            }
            memset(xbuffer, 0xff, 0x1000);

            x++;
          }

          ct.compute_partial_stop(&crc);
          printf("calc_crc32 = %u [0x%08X]\r\n\n", crc, crc);
          //

          fclose(file);

          if (crc == _script->ota_data.checksum) {
            debug("CRC32_ANSI : matched\r\n");
            ota_complete = true;
          } else {
            debug_if(remove("/spif/firmware.bin") == 0,
                     "crc32 not matched remove firmware.bin\r\n");
          }

        } else {
          debug("firmware file : fopen fail\r\n");
        }
        file_mtx.unlock();
      }
    }
  }

  debug_if(_modem->http_stop(), "http_stop() : complete\r\n");
  //   _modem->http_stop();
  free(xbuffer);

  return ota_complete;
}