#ifndef EXTSTORAGE_H
#define EXTSTORAGE_H

#define BLOCKSIZE_4K 4 * 1024

#include "../CellularService/CellularService.h"
#include "../typedef_src.h"
#include "Base64.h"
#include "FATFileSystem.h"
#include "mbed.h"

class ExtStorage {

public:
  ExtStorage(BlockDevice *_bd, FATFileSystem *_fs);
  bool init();
  bool deinit();
  void write_init_script(init_script_t *_script, char path[128],
                         const char *fopen_mode = "w");
  void read_init_script(init_script_t *_script, char path[128],
                        const char *fopen_mode = "r");
  void apply_script(FILE *_file, char path[128], init_script_t *_script);
  bool get_script_flag();

  void write_data_log(char msg[256], char path[128]);
  int check_filesize(char full_path[128], const char *fopen_mode = "r");
  bool upload_log(CellularService *_modem, char full_path[128],
                  char topic[128]);
  bool process_ota(CellularService *_modem, init_script_t *_script);

private:
  FILE *file;
  Mutex file_mtx;
  Base64 base64;

  BlockDevice *bd;
  FATFileSystem *fs;

  char *xbuffer;
  volatile bool is_script_read;
};

#endif