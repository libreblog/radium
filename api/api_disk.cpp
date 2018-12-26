/* Copyright 2017 Kjetil S. Matheussen

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA. */








#include "../common/includepython.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#include <QVector> // Shortening warning in the QVector header. Temporarily turned off by the surrounding pragmas.
#pragma clang diagnostic pop



#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QDir>
#include <QStack>
#include <QSet>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QThread>

#define INCLUDE_SNDFILE_OPEN_FUNCTIONS 1
#include "../common/nsmtracker.h"

#include "../common/OS_disk_proc.h"
#include "../common/OS_string_proc.h"
#include "../common/visual_proc.h"

#include "../embedded_scheme/s7extra_proc.h"

#include "api_common_proc.h"

#include "radium_proc.h"


static int64_t g_curr_disknum = 0;

static QHash<int64_t,disk_t*> g_disks;


const_char* getPath(const_char *path_string){
  return toBase64(path_string);
}

const_char* getHomePath(void){
  return qstring_to_w(OS_get_home_path());
}

const_char* getProgramPath(void){
  return qstring_to_w(QCoreApplication::applicationDirPath());
}

const_char* getPathString(const_char* w_path){
  return fromBase64(w_path);
}

const_char* appendFilePaths(const_char* w_path1, const_char* w_path2){
  return qstring_to_w(w_to_qstring(w_path1) + QDir::separator() + w_to_qstring(w_path2));
}

bool fileExists(const_char* w_path){
  QString filename = w_to_qstring(w_path);
  return QFileInfo::exists(filename);
}

dyn_t getFileInfo(const_char* w_path){
  QString path = w_to_qstring(w_path);

  QFileInfo info(path);

  if (!info.exists()){
    showAsyncMessage(talloc_format("Directory \"%S\" does not exist.", STRING_create(path)));
    return g_uninitialized_dyn;
  }

#if FOR_WINDOWS
  qt_ntfs_permission_lookup++;
#endif

  /*
  bool is_readable =  info.isReadable();

#if FOR_WINDOWS
  qt_ntfs_permission_lookup--;
#endif

  if (!is_readable){
    showAsyncMessage(talloc_format("Directory \"%S\" is not readable", STRING_create(path)));
    return g_uninitialized_dyn;
  }
  */

  SF_INFO sf_info = {};
  SNDFILE *sndfile = radium_sf_open(path, SFM_READ, &sf_info);

  hash_t *ret = HASH_create(10);

  HASH_put_chars(ret, ":path", qstring_to_w(path));
  HASH_put_string(ret, ":filename", STRING_create(info.fileName()));
  HASH_put_bool(ret, ":is-dir", info.isDir());
  HASH_put_bool(ret, ":is-sym-link", info.isSymLink());
  HASH_put_int(ret, ":last-modified", info.lastModified().toSecsSinceEpoch());
  HASH_put_bool(ret, ":is-hidden", info.isHidden());
  HASH_put_int(ret, ":size", info.size());
  HASH_put_string(ret, ":suffix", STRING_create(info.suffix()));

  HASH_put_bool(ret, ":is-audiofile", sndfile != NULL);
  if (sndfile != NULL){
    HASH_put_int(ret, ":num-ch", sf_info.channels);
    HASH_put_int(ret, ":num-frames", sf_info.frames);
    HASH_put_float(ret, ":samplerate", sf_info.samplerate);
    const char *format;
    switch(sf_info.format & 0xffff){
      case SF_FORMAT_PCM_S8       : format = "8bit" ; break;
      case SF_FORMAT_PCM_16       : format = "16bit" ; break;
      case SF_FORMAT_PCM_24       : format = "24bit" ; break;
      case SF_FORMAT_PCM_32       : format = "32bit" ; break;
        
      case SF_FORMAT_PCM_U8       : format = "8bit" ; break;
        
      case SF_FORMAT_FLOAT        : format = "Float" ; break;
      case SF_FORMAT_DOUBLE       : format = "Double" ; break;
        
      case SF_FORMAT_ULAW         : format = "U-Law" ; break;
      case SF_FORMAT_ALAW         : format = "A-Law" ; break;
      case SF_FORMAT_IMA_ADPCM    : format = "ADPCM" ; break;
      case SF_FORMAT_MS_ADPCM     : format = "ADPCM" ; break;
        
      case SF_FORMAT_GSM610       : format = "GSM" ; break;
      case SF_FORMAT_VOX_ADPCM    : format = "ADPCM" ; break;
        
      case SF_FORMAT_G721_32      : format = "ADPCM" ; break;
      case SF_FORMAT_G723_24      : format = "ADPCM" ; break;
      case SF_FORMAT_G723_40      : format = "ADPCM" ; break;
        
      case SF_FORMAT_DWVW_12      : format = "12bit" ; break;
      case SF_FORMAT_DWVW_16      : format = "16bit" ; break;
      case SF_FORMAT_DWVW_24      : format = "24bit" ; break;
      case SF_FORMAT_DWVW_N       : format = "Nbit" ; break;
        
      case SF_FORMAT_DPCM_8       : format = "8bit" ; break;
      case SF_FORMAT_DPCM_16      : format = "16bit" ; break;
        
      case SF_FORMAT_VORBIS       : format = "Vorbis" ; break;
      default                     : format = "Unknown" ; break;
    }
    HASH_put_chars(ret, ":format", format);
    sf_close(sndfile);
  }
  
  return DYN_create_hash(ret);
}

static bool call_callback(func_t* callback, bool in_main_thread, bool is_finished, const_char* path){
  int64_t num_calls = g_num_calls_to_handleError;

  bool ret = S7CALL(bool_bool_dyn,
                    callback,
                    is_finished,                
                    !strcmp(path,"") ? g_uninitialized_dyn : getFileInfo(path)
                    );


  if (num_calls != g_num_calls_to_handleError)
    return false;

  return ret;
}

static void traverse(QString path, func_t* callback, bool in_main_thread, int64_t gc_protect_pos){
  R_ASSERT(in_main_thread || gc_protect_pos >= 0);

  DEFINE_ATOMIC(bool, callback_has_returned_false) = false;
  int64_t last_id = -1;

  QDirIterator it(path);

  while (it.hasNext() && ATOMIC_GET(callback_has_returned_false)==false) {
    
    QString path = it.next();
    QFileInfo info(path);

    if (in_main_thread){

      if (!call_callback(callback,
                         true, 
                         false,
                         qstring_to_w(path)
                         ))
        ATOMIC_SET(callback_has_returned_false, true);

    } else {

      // Note: There's no hard limit on the number of simultaneous callbacks in THREADING_run_on_main_thread_async.
      // It just allocate new memory when needed.

      last_id = THREADING_run_on_main_thread_async([callback, path, info, &ATOMIC_NAME(callback_has_returned_false)](){
          if(!call_callback(callback,
                            true, 
                            false,
                            qstring_to_w(path)
                            ))
            ATOMIC_SET(callback_has_returned_false, true);
        });

    }

  }

  if (in_main_thread){

    if (ATOMIC_GET(callback_has_returned_false)==false)
      call_callback(callback, true, true, "");

  } else {

    if (last_id >= 0)
      THREADING_wait_for_async_function(last_id);

    THREADING_run_on_main_thread_async([callback, &ATOMIC_NAME(callback_has_returned_false), gc_protect_pos](){

        if (ATOMIC_GET(callback_has_returned_false)==false)
          call_callback(callback, false, true, "");

        s7extra_unprotect(callback, gc_protect_pos);

      });

  }
}

#if FOR_WINDOWS
extern Q_CORE_EXPORT int qt_ntfs_permission_lookup;
#endif

bool iterateDirectory(const_char* w_path, bool async, func_t* callback){  
  QString path = w_to_qstring(w_path);

  QFileInfo info(path);

  if (!info.exists()){
    showAsyncMessage(talloc_format("Directory \"%S\" does not exist.", STRING_create(path)));
    return false;
  }

#if FOR_WINDOWS
  qt_ntfs_permission_lookup++;
#endif

  bool is_readable =  info.isReadable();

#if FOR_WINDOWS
  qt_ntfs_permission_lookup--;
#endif

  if (!is_readable){
    showAsyncMessage(talloc_format("Directory \"%S\" is not readable", STRING_create(path)));
    return false;
  }
 
  if(async){

    class MyThread : QThread{
      QString _path;
      func_t *_callback;
      int64_t _gc_protect_pos;

    public:
      MyThread(QString path, func_t *callback, int64_t gc_protect_pos)
        : _path(path)
        , _callback(callback)
        , _gc_protect_pos(gc_protect_pos)
      {
        start();
        connect(this, SIGNAL(finished()), this, SLOT(deleteLater()));
      }

      void run() override {
        traverse(_path, _callback, false, _gc_protect_pos);
      }
    };

    int64_t gc_protect_pos = s7extra_protect(callback); // unprotected in traverse.
    new MyThread(path, callback, gc_protect_pos);

  } else {

    traverse(path, callback, true, -1);

  }

  return true;
}

int64_t openFileForReading(const_char* w_path){
  disk_t *disk = DISK_open_for_reading(w_to_qstring(w_path));
  if (disk==NULL){
    handleError("Unable to open file %s for reading\n", w_to_qstring(w_path).toUtf8().constData());
    return -1;
  }

  int64_t disknum = ++g_curr_disknum;
  g_disks[disknum] = disk;
  return disknum;
}

int64_t openFileForWriting(const_char* w_path){
  disk_t *disk = DISK_open_for_writing(w_to_qstring(w_path));
  if (disk==NULL){
    handleError("Unable to open file for writing (couldn't create temporary file, check your file system)");
    return -1;
  }

  int64_t disknum = ++g_curr_disknum;
  g_disks[disknum] = disk;
  return disknum;
}

bool closeFile(int64_t disknum){
  disk_t *disk = g_disks.value(disknum);
  if (disk==NULL){
    handleError("closeFile: No file #%d", (int)disknum);
    return false;
  }

  g_disks.remove(disknum);

  return DISK_close_and_delete(disk);
}

int writeToFile(int64_t disknum, const_char* string){
  disk_t *disk = g_disks.value(disknum);
  if (disk==NULL){
    handleError("writeToFile: No file #%d", (int)disknum);
    return -1;
  }

  int bytes_written = DISK_write(disk, string);
  int string_len = (int)strlen(string);
  if (bytes_written != string_len){
    handleError("writeToFile: Unable to write all bytes to file. (%d / %d)", bytes_written, string_len);
  }

  return bytes_written;
}

bool fileAtEnd(int64_t disknum){
  disk_t *disk = g_disks.value(disknum);
  if (disk==NULL){
    handleError("writeToFile: No file #%d", (int)disknum);
    return false;
  }

  return DISK_at_end(disk);
}

const_char* readLineFromFile(int64_t disknum){
  disk_t *disk = g_disks.value(disknum);
  if (disk==NULL){
    handleError("writeToFile: No file #%d", (int)disknum);
    return "";
  }

  const_char* ret = DISK_readline(disk);
  if (ret==NULL){
    handleError("readLineFromFile: Attempting to read past end of file");
    return "";
  }

  return ret;
}



// read binary
#include "../common/read_binary.h"


int64_t openFileForBinaryReading(const_char* w_path){
  disk_t *disk = DISK_open_binary_for_reading(STRING_create(w_to_qstring(w_path)));
  if (disk==NULL){
    handleError("Unable to open file %s for reading\n", w_to_qstring(w_path).toUtf8().constData());
    return -1;
  }

  R_ASSERT(DISK_is_binary(disk));

  int64_t disknum = ++g_curr_disknum;
  g_disks[disknum] = disk;
  return disknum;
}


static bool read_binary(const_char* funcname, int64_t disknum, unsigned char dest[], int64_t num_bytes){
  disk_t *disk = g_disks.value(disknum);
  if (disk==NULL){
    handleError("%s: No file #%d", funcname, (int)disknum);
    return false;
  }

  if (DISK_is_binary(disk)==false){
    handleError("%s: File #%d is not opened in binary mode", funcname, (int)disknum);
    return false;
  }

  int64_t num_bytes_read = DISK_read_binary(disk, dest, num_bytes);
  if(num_bytes_read != num_bytes){
    handleError("%s: Reading file failed", funcname);
    return false;
  }

  return true;
}


// 32 bit

int readLe32FromFile(int64_t disknum){
  unsigned char chars[4];
  if(read_binary("readLe32FromFile", disknum, chars, 4)==false)
    return 0;

  return get_le_32(chars);
}

int64_t readLeU32FromFile(int64_t disknum){
  unsigned char chars[4];
  if(read_binary("readLeU32FromFile", disknum, chars, 4)==false)
    return 0;

  return get_le_u32(chars);
}

/*
// there is no get_be_32 function
int readBe32FromFile(int64_t disknum){
  unsigned char chars[4];
  if(read_binary("readLe32FromFile", disknum, chars, 4)==false)
    return 0;

  return get_be_32(chars);
}
*/

int64_t readBeU32FromFile(int64_t disknum){
  unsigned char chars[4];
  if(read_binary("readLeU32FromFile", disknum, chars, 4)==false)
    return 0;

  return get_be_u32(chars);
}

// 16 bit

int readLe16FromFile(int64_t disknum){
  unsigned char chars[2];
  if(read_binary("readLe16FromFile", disknum, chars, 2)==false)
    return 0;

  return get_le_16(chars);
}

/*
// there is no get_le_u16 function
int readLeU16FromFile(int64_t disknum){
  unsigned char chars[2];
  if(read_binary("readLeU16FromFile", disknum, chars, 2)==false)
    return 0;

  return get_le_u16(chars);
}
*/

/*
// there is no get_be_16 function
int readBe16FromFile(int64_t disknum){
  unsigned char chars[2];
  if(read_binary("readBe16FromFile", disknum, chars, 2)==false)
    return 0;

  return get_be_16(chars);
}
*/

int readBeU16FromFile(int64_t disknum){
  unsigned char chars[2];
  if(read_binary("readBeU16FromFile", disknum, chars, 2)==false)
    return 0;

  return get_be_u16(chars);
}

// 8 bit

int read8FromFile(int64_t disknum){
  disk_t *disk = g_disks.value(disknum);
  if (disk==NULL){
    handleError("readU8FromFile: No file #%d", (int)disknum);
    return 0;
  }
  
  if (DISK_is_binary(disk)==false){
    handleError("read8FromFile: File #%d is not opened in binary mode", (int)disknum);
    return 0;
  }

  int8_t size_chars[1] = {};
  if(DISK_read_binary(disk, size_chars, 1) !=1 ){
    handleError("read8FromFile: unable to read from file #%d",(int)disknum);
    return 0;
  }

  return size_chars[0];
}

int readU8FromFile(int64_t disknum){
  unsigned char chars[1];
  if(read_binary("read8FromFile", disknum, chars, 1)==false)
    return 0;

  return chars[0];
}

