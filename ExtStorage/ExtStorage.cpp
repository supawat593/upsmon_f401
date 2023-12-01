
#include "./ExtStorage.h"

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
  char xinit_cfg_write[] = {
      "#Configuration file for UPS Monitor\r\n\r\nSTART:\r\nBroker: "
      "\"%s\"\r\nPort: %d\r\nKey: \"%s\"\r\nTopic: \"%s\"\r\nCommand: "
      "[%s]\r\nModel: \"%s\"\r\nSite_ID: \"%s\"\r\nSTOP:"};

  file_mtx.lock();
  file = fopen(path, fopen_mode);
  debug_if(file == NULL, "*** fopen fail ! ***\r\n");

  if (file != NULL) {

    // debug("initial script found\r\n");
    char fbuffer[512];
    sprintf(fbuffer, xinit_cfg_write, _script->broker, _script->port,
            _script->encoded_key, _script->topic_path, _script->full_cmd,
            _script->model, _script->siteID);

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
  char xinit_cfg_pattern[] = {"%*[^\n]\nBroker: \"%[^\"]\"\nPort: %d\nKey: "
                              "\"%[^\"]\"\nTopic: \"%[^\"]\"\nCommand: "
                              "[%[^]]]\nModel: \"%[^\"]\"\nSite_ID: "
                              "\"%[^\"]\"\n%*s"};

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
             key_encoded, _script->topic_path, _script->full_cmd,
             _script->model, _script->siteID) == 7) {
    if (_script->topic_path[strlen(_script->topic_path) - 1] == '/') {
      _script->topic_path[strlen(_script->topic_path) - 1] = '\0';
    }

    printf("\r\n<---------------------------------------->\r\n");
    printf("    broker: %s\r\n", _script->broker);
    printf("    port: %d\r\n", _script->port);
    printf("    Key: %s\r\n", key_encoded);
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

    debug_if(len > 0, "<----- Log %s Appending ----->\r\nPayload-> %s\r\n",
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