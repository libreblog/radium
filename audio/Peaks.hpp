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


#ifndef RADIUM_AUDIO_PEAKS_HPP
#define RADIUM_AUDIO_PEAKS_HPP

#if defined(INCLUDE_SNDFILE_OPEN_FUNCTIONS)
#include <QThread>
#endif


#include "Juce_plugins_proc.h"
#include "../common/Mutex.hpp"
#include "../common/read_binary.h"


#define SAMPLES_PER_PEAK 64 // TODO: This value must be dynamic and placed in the Peaks class. The value must be set higher than 64 if the sample is so long that a 32 bit int becomes too small to use as index. (Need around a 32 day (maybe +32/-16, haven't calculated exactly) long sample (16 days for 96Khz samples) for this to be a problem though, but you never know)



static inline int unit_ceiling(int value, int unit){
  if ( (value % unit) == 0)
    return value;
  else
    return unit * (1 + int(value/unit));
}


static inline int unit_floor(int value, int unit){
  return unit * int(value/unit);
}


  


namespace radium{

extern radium::Mutex g_peak_read_lock; // We don't want to generate peaks for more than one file at the time
  
struct Peak{

private:
  float min, max;

public:
  
  Peak(const float min, const float max)
    : min(min)
    , max(max)
  {    
    R_ASSERT_NON_RELEASE(sizeof(float)==4);
  }

  Peak(const Peak &other)
    : Peak(other.min, other.max)
  {}

  Peak()
    : Peak(FLT_MAX, FLT_MIN)
  {}

  float get_min(void) const {
    R_ASSERT_RETURN_IF_FALSE2(has_data(), 0);
    return min;
  }

  float get_max(void) const {
    R_ASSERT_RETURN_IF_FALSE2(has_data(), 0);
    return max;
  }

  void write_to_disk(disk_t *disk) const {
    DISK_write_binary(disk, &min, 4);
    DISK_write_binary(disk, &max, 4);
  }

  /*
  void set_min(float new_min){
    min = new_min;
  }

  void set_max(float new_max){
    max = new_max;
  }
  */
  
  void scale(float scaleval){
    R_ASSERT_RETURN_IF_FALSE(has_data());
    
    min *= scaleval;
    max *= scaleval;
  }
  
  bool has_data(void) const {
    return min != FLT_MAX;
  }
  
  void merge(const Peak &peak) {
    if (!peak.has_data())
      return;
    else if (!has_data()){
      *this = peak;
    } else {
      min = std::min(peak.min, min);
      max = std::max(peak.max, max);
    }
  }
};

  
#define PEAKS_DIV 4 // I.e. need four peak values to create one peak value in _up.

  
// See bin/scheme/algorithm_doodles.scm for algorithm.
class Peaks{

  friend class DiskPeaks;

  QVector<Peak> _peaks; // array index is in block pos (i.e. frame pos / SAMPLES_PER_PEAK)

  int64_t _total_frames = 0;

  Peaks *_up = NULL;

  mutable radium::Mutex _mutex;

  Peaks(const Peaks&) = delete; // copy-constructor and all that stuff in C++ is total madness. (Use a conservative garbage collector instead. Radium has done that since year 2000.)
  Peaks& operator=(const Peaks&) = delete; // copy-constructor and all that stuff in C++ is total madness.

public:
  
  Peaks(){
  }
  
  ~Peaks(){
    delete _up;
  }
  

private:

  Peak _curr_uplevel_peak;
  
  void add(const Peak &peak){    
    radium::ScopedMutex lock(_mutex);

    int uplevel_pos = _peaks.size() % PEAKS_DIV;
    
    _peaks.push_back(peak);

    if (uplevel_pos == 0) {

      _curr_uplevel_peak = peak;
        
    } else {

      _curr_uplevel_peak.merge(peak);
      
      if (uplevel_pos == PEAKS_DIV-1) {
        
        if (_up==NULL)
          _up = new Peaks();
      
        _up->add(_curr_uplevel_peak);
        
        
      }
      
    }
    
  }

  void merge_in_plain(int start, int end, Peak &peak) const {
    R_ASSERT_RETURN_IF_FALSE(start >= 0);
    R_ASSERT_RETURN_IF_FALSE(end >= start);
    R_ASSERT_RETURN_IF_FALSE(end <= _peaks.size());

    //printf("   merge: Iterating %d\n", end-start);
    for(int pos = start ; pos < end ; pos++)
      peak.merge(_peaks.at(pos));
  }
  
  void merge_in(int start, int end, Peak &peak) const {
    //printf("  merge_in %d -> %d (%d)\n", start, end, end-start);
           
    if (_up==NULL) {
      merge_in_plain(start, end, peak);
      return;
    }

    int next_array_start = unit_ceiling(start, PEAKS_DIV);
    int next_array_end = unit_floor(end, PEAKS_DIV);

    //printf("     next_array: %d -> %d (%d)\n", next_array_start, next_array_end, next_array_end-next_array_start);

    if (next_array_end > next_array_start){
      
      merge_in_plain(start, next_array_start, peak);
      _up->merge_in(next_array_start / PEAKS_DIV, next_array_end / PEAKS_DIV, peak);
      merge_in_plain(next_array_end, end, peak);
      
    } else {
      
      merge_in_plain(start, end, peak);
      
    }
  }
  
private:
  int64_t _creation_time = 0;
  
public:

  enum Add_Samples_Type{
    MORE_IS_COMING_LATER,
    THIS_IS_THE_LAST_CALL
  };
  
  // This function must be called sequentially. E.g.:
  //   create(0, samples, 128)
  //   create(128, samples, 64)
  //   create(192, samples, 256)
  //   etc.
  // Not:
  //   create(128, samples, 64)
  //   create(0, samples, 128)
  //   etc.
  //
  // The 'num_samples' argument can only be non-dividable by SAMPLES_PER_PEAK if add_samples_type==THIS_IS_THE_LAST_CALL.
  void add_samples(float *samples, int64_t num_samples, Add_Samples_Type add_samples_type){
    R_ASSERT_RETURN_IF_FALSE(_creation_time >= 0);
    R_ASSERT_RETURN_IF_FALSE((_creation_time % SAMPLES_PER_PEAK) == 0);
    R_ASSERT_RETURN_IF_FALSE(add_samples_type==THIS_IS_THE_LAST_CALL || (num_samples % SAMPLES_PER_PEAK) == 0);
    
    R_ASSERT_NON_RELEASE(num_samples > 0);

    for(int64_t time = 0 ; time < num_samples ; time += SAMPLES_PER_PEAK){
      int duration = int(R_MIN(num_samples-time, SAMPLES_PER_PEAK));
      float min,max;
      JUCE_get_min_max_val(&samples[time], duration, &min, &max);
      add(Peak(min, max));
    }

    if (add_samples_type==THIS_IS_THE_LAST_CALL)
      _creation_time = -1;
    else
      _creation_time += num_samples;

    _total_frames += num_samples;
  }

  
  const Peak get(int64_t start_time, int64_t end_time) const {
    //printf("Peak.get: %d -> %d (%d)\n", (int)start_time, (int)end_time, (int)(end_time-start_time));

    radium::ScopedMutex lock(_mutex);

    Peak peak;

    R_ASSERT_RETURN_IF_FALSE2(start_time >= 0, peak);
    
    int start = int(start_time / SAMPLES_PER_PEAK);
    if(start >= _peaks.size() || start<0)
      return peak;

    int end_temp = int(end_time / SAMPLES_PER_PEAK);
    int end = end_temp < 0 ? _peaks.size() : R_MIN(_peaks.size(), end_temp);

    // Asked for so little data that the indexes are the same.
    if (start>=end){
      R_ASSERT_RETURN_IF_FALSE2(start==end, peak);
      return _peaks.at(start);
    }
    
    merge_in(start, end, peak);

    return peak;
  }
  
};
 
}


#if defined(INCLUDE_SNDFILE_OPEN_FUNCTIONS)

#include "../common/visual_proc.h"
#include "../common/seqtrack_proc.h"

namespace radium{
  
class DiskPeaks : public QThread{

  const wchar_t *_peak_filename;

  DEFINE_ATOMIC(bool, _is_valid) = false;
  DEFINE_ATOMIC(int64_t, _percentage_read) = 0;

public:

  int64_t _total_frames = -1;
  int _num_ch = -1;
  Peaks **_peaks = NULL;

  const wchar_t *_filename;
  int num_visitors = 1;

  // Use DISKPEAKS_get instead.
  DiskPeaks(const wchar_t *filename)
    : _peak_filename(wcsdup(STRING_append(filename, ".radium_peaks")))
    , _filename(wcsdup(filename))
  {
    SF_INFO sf_info; memset(&sf_info,0,sizeof(sf_info));
    SNDFILE *sndfile = radium_sf_open(_filename,SFM_READ,&sf_info);
    if (sndfile==NULL)
      return;

    sf_close(sndfile);

    _total_frames = sf_info.frames;
    _num_ch = sf_info.channels;

    if(_num_ch == 0 && sf_info.frames == 0){
      GFX_Message(NULL, "0 channels or 0 frames");
      return;
    }

    _peaks = (Peaks**)calloc(_num_ch, sizeof(Peaks*));
    for(int ch=0;ch<_num_ch;ch++)
      _peaks[ch] = new Peaks;
    
    ATOMIC_SET(_is_valid, true);

    //printf("... Has valid: %d\n", has_valid_peaks_on_disk());
 
    start();
  }

  // Use DISKPEAKS_remove instead.
  ~DiskPeaks(){
    R_ASSERT(num_visitors==0);
    
    wait();

    for(int ch=0;ch<_num_ch;ch++)
      delete _peaks[ch];

    free(_peaks);

    free((void*)_peak_filename);
    free((void*)_filename);
  }

  bool is_valid(void){
    return ATOMIC_GET(_is_valid);
  }

  int64_t percentage_read(void){
    return ATOMIC_GET(_percentage_read);
  }

  void wait(void){
    QThread::wait();
  }

  bool has_valid_peaks_on_disk(void) const {
    if (DISK_file_exists(_peak_filename)==false)
      return false;
    
    int64_t peaks_creation_time = DISK_get_creation_time(_peak_filename);
    int64_t sample_creation_time = DISK_get_creation_time(_filename);

    return peaks_creation_time > sample_creation_time;
  }

private:

  void run() override {
    radium::ScopedMutex lock(g_peak_read_lock); // We don't want to generate/read peaks for more than one file at the time
    
    printf("   Reading peaks from file %s\n", STRING_get_qstring(_peak_filename).toUtf8().constData());

    if (has_valid_peaks_on_disk())
      read_peaks_from_disk();
    else{
      read_peaks_from_sample_file();
    }
  }

  void read_peaks_from_disk(void){
    bool success = false;

    disk_t *disk = DISK_open_binary_for_reading(_peak_filename);
    if (disk==NULL){
      GFX_Message(NULL, "Unable to open peak file %s for reading", STRING_get_qstring(_peak_filename).toUtf8().constData());
      goto exit;
    }

    {
      char source[8] = {};
      if (DISK_read_binary(disk, source, 8)==-1)
        goto exit;
      if(strncmp(source, "RADIUM0", 8)){
        GFX_Message(NULL, "Unable to read peak file %s. Expected \"RADUIM0\", found something else.", STRING_get_qstring(_peak_filename).toUtf8().constData());
        goto exit;
      }
    }

    {
      unsigned char source[4];
      if (DISK_read_binary(disk, source, 4)==-1)
        goto exit;
      int num_ch = get_le_32(source);
      if (num_ch != _num_ch){
        GFX_Message(NULL, "Peak file %s is not correct. It contains peaks for %d channels, but soundfile contains %d channels.", STRING_get_qstring(_peak_filename).toUtf8().constData(), num_ch, _num_ch);
        goto exit;
      }
    }
    
    for(int ch = 0 ; ch < _num_ch ; ch++){
      {
        unsigned char source[4];
        if (DISK_read_binary(disk, source, 4)==-1)
          goto exit;
        int samples_per_peak = get_le_32(source);
        if (samples_per_peak != SAMPLES_PER_PEAK){
          GFX_Message(NULL, "Something is wrong with peak file %s (1)", STRING_get_qstring(_peak_filename).toUtf8().constData());
          goto exit;
        }
      }

      int num_peaks;

      {
        unsigned char source[4];
        if (DISK_read_binary(disk, source, 4)==-1)
          goto exit;
        num_peaks = get_le_32(source);        
        if (num_peaks <= 0){
          GFX_Message(NULL, "Something is wrong with peak file %s (2)", STRING_get_qstring(_peak_filename).toUtf8().constData());
          goto exit;
        }
      }
      
      {
        unsigned char source[8];
        if (DISK_read_binary(disk, source, 8)==-1)
          goto exit;
        int64_t total_frames = get_le_64(source);
        if (total_frames != _total_frames){
          GFX_Message(NULL, "Something is wrong with peak file %s. Expected %" PRId64 " frames, found %" PRId64 "", STRING_get_qstring(_peak_filename).toUtf8().constData(), _total_frames, total_frames);
          goto exit;          
        }
      }

#define READ_EVERYTHING_AT_ONCE 1
      
#if READ_EVERYTHING_AT_ONCE
      // TODO: Optimize. Read more than one float at the time.
      float *data = new float[num_peaks*2]; // Allocate on heap instead of the stack since it could be very big (even likely to be very big)
      if (data==NULL){
        GFX_Message(NULL, "Unable to allocate enough temporary memory when reading Peak file %s. Size: %d.", STRING_get_qstring(_peak_filename).toUtf8().constData(), (int)(num_peaks*2*sizeof(float)));
        goto exit;
      }
      
      if (DISK_read_binary(disk, data, sizeof(float)*2*num_peaks)==-1){
        GFX_Message(NULL, "Unable to read data from peak file %s.", STRING_get_qstring(_peak_filename).toUtf8().constData());
        goto exit;
      }
#endif
      
      int64_t last_percentage_read = -1;
      
      for(int i = 0 ; i<num_peaks ; i++){
#if READ_EVERYTHING_AT_ONCE
        _peaks[ch]->add(Peak(data[i*2], data[i*2+1]));
#else
        float minmax[2];
        
        if (DISK_read_binary(disk, &minmax[0], sizeof(float)*2)==-1)
          goto exit;
        
        _peaks[ch]->add(Peak(minmax[0], minmax[1]));
#endif
        
        {
          int64_t percentage_read = 100*i/num_peaks;
          
          if (percentage_read != last_percentage_read){
            ATOMIC_SET(_percentage_read, percentage_read);
            SEQUENCER_update(); // SEQUENCER_update can be called from another thread.
          }
          
          last_percentage_read = percentage_read;
        }

      }

#if READ_EVERYTHING_AT_ONCE
      delete[] data;
#endif
#undef READ_EVERYTHING_AT_ONCE
      
    }

    success = true;

  exit:

    DISK_close_and_delete(disk);

    if (success==false){
      read_peaks_from_sample_file();
      //start();
    }

    return;
  }

  void store_peaks_to_disk(void){
    printf("   DiskPeaks: storing peaks to disk\n");

    disk_t *disk = DISK_open_binary_for_writing(_peak_filename);
    if (disk==NULL){
      printf("   DiskPeaks: Failed opening \"%s\"\n", STRING_get_qstring(_peak_filename).toUtf8().constData());
      return;
    }

    {
      unsigned char source[8] = "RADIUM0";
      DISK_write_binary(disk, source, 8);
    }

    {
      unsigned char source[4];
      put_le_32(source, _num_ch);
      DISK_write_binary(disk, source, 4);
    }

    for(int ch = 0 ; ch < _num_ch ; ch++){

      {
        unsigned char source[4];
        put_le_32(source, SAMPLES_PER_PEAK);
        DISK_write_binary(disk, source, 4);
      }

      {
        unsigned char source[4];
        put_le_32(source, _peaks[ch]->_peaks.size());
        DISK_write_binary(disk, source, 4);
      }

      {
        unsigned char source[8];
        put_le_64(source, _peaks[ch]->_total_frames);
        DISK_write_binary(disk, source, 8);
      }

      for(const auto &peak : _peaks[ch]->_peaks){
        peak.write_to_disk(disk);
      }
    }

    DISK_close_and_delete(disk);
    printf("   DiskPeaks: Finished writing peak file \"%s\"\n", STRING_get_qstring(_peak_filename).toUtf8().constData());
  }

  void read_peaks_from_sample_file(void){

    SF_INFO sf_info; memset(&sf_info,0,sizeof(sf_info));
    SNDFILE *sndfile = radium_sf_open(_filename,SFM_READ,&sf_info);
    if (sndfile==NULL){
      printf("   DiskPeaks: Failed opening %s\n", STRING_get_qstring(_filename).toUtf8().constData());
      ATOMIC_SET(_is_valid, false);
      return;
    }

    float interleaved_samples[_num_ch * SAMPLES_PER_PEAK];
    float samples[_num_ch][SAMPLES_PER_PEAK];

    int64_t total_frames = sf_info.frames;

    int64_t total_frames_read = 0;

    int64_t last_percentage_read = -1;

    for(int64_t i = 0 ; ; i += SAMPLES_PER_PEAK){
      
      int num_read = (int)sf_readf_float(sndfile, interleaved_samples, SAMPLES_PER_PEAK);
      
      if (num_read==0) {
        QString s = STRING_get_qstring(_filename); 
        GFX_Message(NULL, "Unable to read from pos %" PRId64 " in file %s", i, s.toLocal8Bit().constData());
        ATOMIC_SET(_is_valid, false);
        break;
      }

      bool is_finished = false;

      total_frames_read += num_read;

      {
        int64_t percentage_read = 100*total_frames_read/total_frames;
        
        if (percentage_read != last_percentage_read){
          ATOMIC_SET(_percentage_read, percentage_read);
          SEQUENCER_update(); // SEQUENCER_update can be called from another thread.
        }
        
        last_percentage_read = percentage_read;
      }
      
      if(total_frames_read == total_frames){
        R_ASSERT(last_percentage_read==100);
        is_finished=true;
      }

      int pos=0;
      for(int i=0;i<num_read;i++)
        for(int ch=0;ch<_num_ch;ch++)
          samples[ch][i] = interleaved_samples[pos++];
      
      for(int ch=0;ch<_num_ch;ch++)
        _peaks[ch]->add_samples(samples[ch], num_read, is_finished ? Peaks::THIS_IS_THE_LAST_CALL : Peaks::MORE_IS_COMING_LATER);
      
      if (is_finished==true)
        break;
    }

    sf_close(sndfile);

    printf("   DiskPeaks: Finished reading peaks. Valid: %d\n", is_valid());

    if (is_valid())
      store_peaks_to_disk();
  }

};

}


radium::DiskPeaks *DISKPEAKS_get(const wchar_t *wfilename);
void DISKPEAKS_remove(radium::DiskPeaks *diskpeaks);


#endif
 

#endif
