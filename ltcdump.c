#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include "wav.h"

typedef char bool;
static const char false = 0;
static const char true = 1;

int verbosity = 0;
bool json_output = false;


// Unsigned error return value
static const size_t ST_ERROR = 0xffffffff;

static const char* SYNC_WORD_STR = "0011111111111101";
static unsigned int SYNC_WORD = 0xbffc;

#define countof(x)  (sizeof(x) / sizeof(x[0]))

/*
 * Logging
 */
typedef struct 
{
  const char* string;
  int level;
  int status_code;
} Msg;

typedef struct 
{
  Msg msgs[4096];
  size_t n;
} Queue;

static Queue info_queue = {0};
static Queue error_queue = {0};

static void vqueue_msg(Queue* queue, int level, int status_code, const char*fmt, va_list args)
{
  va_list args_copy;
  va_copy(args_copy, args);
  int size = vsnprintf(NULL, (size_t)0, fmt, args_copy);
  va_end(args_copy);

  char* str = malloc(size + 1);
  vsprintf(str, fmt, args);

  if (queue->n < countof(queue->msgs))
  {
    Msg* msg = &queue->msgs[queue->n++];

    msg->string = str;
    msg->level = level;
    msg->status_code = status_code;
  } 
}

/* SUPRESS UNUSED FUNC WARNING
static void queue_msg(Queue* queue, int level, int status_code, const char*fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  vqueue_msg(queue, level, status_code, fmt, args);

  va_end(args);
}
*/

static void log_error(int status_code, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  if (json_output)
  {
    vqueue_msg(&error_queue, 0, status_code, fmt, args);
  }
  else
  {
    fprintf(stderr, "%d: ", status_code);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
  }

  va_end(args);
}

static void log_info(int level, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);

  if (verbosity >= level)
  {
    if (json_output)
    {
      vqueue_msg(&info_queue, level, 0, fmt, args);
    }
    else
    {
      printf(" *** ");
      vprintf(fmt, args);
      printf("\n");
    }
  }

  va_end(args);
}


/* 
 * The 80 bits of the LTC frame as a C struct.
 * Taken from libltc
 *
 * Little Endian version -- and doxygen doc 
 * */
struct LTCFrame {
        unsigned int frame_units:4; ///< SMPTE framenumber BCD unit 0..9
        unsigned int user1:4;

        unsigned int frame_tens:2; ///< SMPTE framenumber BCD tens 0..3
        unsigned int dfbit:1; ///< indicated drop-frame timecode
        unsigned int col_frame:1; ///< colour-frame: timecode intentionally synchronized to a colour TV field sequence
        unsigned int user2:4;

        unsigned int secs_units:4; ///< SMPTE seconds BCD unit 0..9
        unsigned int user3:4;

        unsigned int secs_tens:3; ///< SMPTE seconds BCD tens 0..6
        unsigned int biphase_mark_phase_correction:1; ///< see note on Bit 27 in description and \ref ltc_frame_set_parity .
        unsigned int user4:4;

        unsigned int mins_units:4; ///< SMPTE minutes BCD unit 0..9
        unsigned int user5:4;

        unsigned int mins_tens:3; ///< SMPTE minutes BCD tens 0..6
        unsigned int binary_group_flag_bit0:1; ///< indicate user-data char encoding, see table above - bit 43
        unsigned int user6:4;

        unsigned int hours_units:4; ///< SMPTE hours BCD unit 0..9
        unsigned int user7:4;

        unsigned int hours_tens:2; ///< SMPTE hours BCD tens 0..2
        unsigned int binary_group_flag_bit1:1; ///< indicate timecode is local time wall-clock, see table above - bit 58        unsigned int binary_group_flag_bit2:1; ///< indicate user-data char encoding (or parity with 25fps), see table above - bit 59
        unsigned int user8:4;

        unsigned int sync_word:16;
};
typedef struct LTCFrame LTCFrame;

/**
 * Human readable time representation, decimal values.
 */
struct SMPTETimecode {
        char timezone[6];   ///< the timezone 6bytes: "+HHMM" textual representation
        unsigned char years; ///< LTC-date uses 2-digit year 00.99
        unsigned char months; ///< valid months are 1..12
        unsigned char days; ///< day of month 1..31

        unsigned char hours; ///< hour 0..23
        unsigned char mins; ///< minute 0..60
        unsigned char secs; ///< second 0..60
        unsigned char frame; ///< sub-second frame 0..(FPS - 1)
};
typedef struct SMPTETimecode SMPTETimecode;

static char* timecode_to_str(SMPTETimecode* stime)
{
  // Ring buffer of 5 returns values, before we start to overwrite.
  static char buffers[5][13];
  static size_t i = 0;

  char* buffer = buffers[i];

  snprintf(buffer, sizeof(buffers[i]),
           "%02d:%02d:%02d:%02d",
           (int)stime->hours, 
           (int)stime->mins, 
           (int)stime->secs, 
           (int)stime->frame);

  i = (i+1) % countof(buffers);

  return buffer;
}

static void ltc_frame_to_time(SMPTETimecode *stime, LTCFrame *frame/*, int flags*/) {
        if (!stime) return;

/*        if (flags & LTC_USE_DATE) {
                smpte_set_timezone_string(frame, stime);

                stime->years  = frame->user5 + frame->user6*10;
                stime->months = frame->user3 + frame->user4*10;
                stime->days   = frame->user1 + frame->user2*10;
        } else { */
                stime->years  = 0;
                stime->months = 0;
                stime->days   = 0;
                sprintf(stime->timezone,"+0000");
/*        } */

        stime->hours = frame->hours_units + frame->hours_tens*10;
        stime->mins  = frame->mins_units  + frame->mins_tens*10;
        stime->secs  = frame->secs_units  + frame->secs_tens*10;
        stime->frame = frame->frame_units + frame->frame_tens*10;
}

/*
 * Encapsulate a node in a linked list of ranges of timecodes.
 */
typedef struct _SMPTETimecodeRange
{
  SMPTETimecode start, end;
  struct _SMPTETimecodeRange* next_ptr;
} SMPTETimecodeRange;

SMPTETimecodeRange* create_timecode_range(SMPTETimecode* start, SMPTETimecode* end)
{
  SMPTETimecodeRange* obj = malloc(sizeof(SMPTETimecodeRange));
  memcpy(&obj->start, start, sizeof(SMPTETimecode));
  memcpy(&obj->end, end, sizeof(SMPTETimecode));
  return obj;
}

void timecode_range_append(SMPTETimecodeRange** first_pptr, SMPTETimecodeRange* new_ptr)
{
  if (!*first_pptr)
  {
    *first_pptr = new_ptr;
  }
  else
  {
    SMPTETimecodeRange* node_ptr = *first_pptr;
    for (;
         node_ptr->next_ptr;
         node_ptr = node_ptr->next_ptr);

    node_ptr->next_ptr = new_ptr;
    new_ptr->next_ptr = NULL;
  }
}

/*
 * Output data for JSON
 */
typedef struct
{
  Queue*              info_queue;
  Queue*              error_queue;
  SMPTETimecodeRange* timecode_range_ptr;
  size_t              discarded_bits_at_start;
  SMPTETimecode       start, end;
} OutputData;

static OutputData* create_output_data(Queue* info_queue, Queue* error_queue)
{
  OutputData* obj = malloc(sizeof(OutputData));

  obj->timecode_range_ptr = NULL;
  obj->info_queue = info_queue;
  obj->error_queue = error_queue;
  obj->discarded_bits_at_start = 0;

  return obj;
}

static void output_data_to_json(OutputData* data)
{

  /*
   * Determine success or failure
   */
  int result_code;
  const char* error_msg = NULL;

  if (data->error_queue->n == 0)
  {
    result_code = 200;
    error_msg = "";
  }
  else
  {
    Msg* last_msg =& data->error_queue->msgs[data->error_queue->n - 1];
    result_code = last_msg->status_code;
    error_msg = last_msg->string;
  }


  /*
   * Output JSON.
   */
  printf("{\n"); 

  printf("\t\"InfoMessages\": [\n");
  
  if (data->info_queue->n > 0)
  {
    Msg* m = &data->info_queue->msgs[0];

    for (size_t i = 0; i < data->info_queue->n - 1; ++i, ++m)
    {
      printf("\t\t{\"status_code\": %d, \"string\":\" %s\", \"level\": %d},\n",
             m->status_code, m->string, m->level); 
    }

    printf("\t\t{\"status_code\": %d, \"string\":\" %s\", \"level\": %d}\n", 
           m->status_code, m->string, m->level); 
  }

  printf("\t], \n");


  printf("\t\"ErrorMessages\": [\n");
  
  if (data->error_queue->n > 0)
  {
    Msg* m = &data->error_queue->msgs[0];

    for (size_t i = 0; i < data->error_queue->n - 1; ++i, ++m)
    {
      printf("\t\t{\"status_code\": %d, \"string\":\" %s\", \"level\": %d},\n",
             m->status_code, m->string, m->level); 
    }

    printf("\t\t{\"status_code\": %d, \"string\":\" %s\", \"level\": %d}\n", 
           m->status_code, m->string, m->level); 
  }

  printf("\t], \n");


  if (result_code == 200)
  {
    printf("\t\"TimecodeRanges\": [\n");

    if (data->timecode_range_ptr)
    {
      SMPTETimecodeRange* node = data->timecode_range_ptr;

      printf("\t\t[\"%s\", \"%s\"]", timecode_to_str(&node->start), 
          timecode_to_str(&node->start));


      for (node = node->next_ptr; node; node = node->next_ptr)
      {
        printf(",\n");
        printf("\t\t[%s, %s]", timecode_to_str(&node->start), 
            timecode_to_str(&node->start));
      }
      printf("\n");

    }

    printf("\t], \n");
  }

  printf("\t\"ResultCode\": %d,\n", result_code);
  printf("\t\"ErrorMsg\": \"%s\"",error_msg);

  if (result_code == 200)
  {
    printf(",\n");
    printf("\t\"DiscardedBitsAtStart\": %ld,\n", data->discarded_bits_at_start);
    printf("\t\"Start\": \"%s\",\n", timecode_to_str(&data->start));
    printf("\t\"End\": \"%s\"\n", timecode_to_str(&data->end));
  }

  printf("}\n"); 
}

static void usage (int status) 
{
  printf ("ltcdump - parse linear time code from a audio-file.\n\n");
  printf ("Usage: ltcdump [ OPTIONS ] <filename>\n\n");
  printf ("Options:\n\
  -f, --fps <num>         override detected framerate\n\
  -v, --verbose           set debug info display\n\
  -j, --json              output results as JSON\n\
  -h, --help              display this help and exit\n\
\n");

  exit (status);
}


static struct option const long_options[] =
{
  {"help", no_argument, 0, 'h'},
  {"fps", required_argument, 0, 'f'},
  {"verbose", no_argument, 0, 'v'},
  {"json", no_argument, 0, 'j'},
  {NULL, 0, NULL, 0}
};



/*
 * Decode a string containg characters '0' and '1' which represent a
 * sequence of LTC frame. Print the timestamps found in these frames
 */
static size_t consume_digits(char* digits, size_t n,
                             size_t* digits_discarded_ptr,
                             bool* got_frame_ptr,
                             SMPTETimecode* timecode_ptr )
{
  LTCFrame frame;
  size_t frame_count = 0;
  size_t digits_discarded = 0;
  if (got_frame_ptr) *got_frame_ptr = false;

  /*
   * We're looking for 80 characters that end in SYNC_WORD_STR
   */
  for (; n > 80; --n, ++digits, ++digits_discarded)
  {
    if (strncmp(&digits[80-16], SYNC_WORD_STR, 16) == 0)
    {
      log_info(2, "Got frame: %.80s", digits);
      break;
    }
    else
    {
      log_info(2, "Looking for sync word %.80s", digits);
      //*resynced_ptr = true;
    }
  }

  for (frame_count = 0; n > 80; ++frame_count, n -= 80)
  {
    // Consume 80 bits
    for (size_t byte_count = 0; byte_count < 10; ++byte_count)
    {
      uint8_t byte = 0;
    
      byte |= digits[80*frame_count + byte_count*8 + 0] == '1' ? 1 : 0;
      byte |= digits[80*frame_count + byte_count*8 + 1] == '1' ? 2 : 0;
      byte |= digits[80*frame_count + byte_count*8 + 2] == '1' ? 4 : 0;
      byte |= digits[80*frame_count + byte_count*8 + 3] == '1' ? 8 : 0;
      byte |= digits[80*frame_count + byte_count*8 + 4] == '1' ? 16 : 0;
      byte |= digits[80*frame_count + byte_count*8 + 5] == '1' ? 32 : 0;
      byte |= digits[80*frame_count + byte_count*8 + 6] == '1' ? 64 : 0;
      byte |= digits[80*frame_count + byte_count*8 + 7] == '1' ? 128 : 0;
    
      *(((uint8_t*)&frame) + byte_count) = byte;
    }

    // Checksum the frame
    //  - Should be impossible because we syned above.
    if (frame.sync_word != SYNC_WORD)
    {
      return ST_ERROR;
    }

    if (got_frame_ptr)
    {
      *got_frame_ptr = true;
    }

    if (timecode_ptr)
    {
      ltc_frame_to_time(timecode_ptr, &frame);
    }
  }

  if (digits_discarded_ptr) *digits_discarded_ptr = digits_discarded;

  return frame_count * 80 + digits_discarded;
}

/*
size_t find_first_frame(char* digits, size_t n)
{
  for (size_t i = 0 ; i < n - 16 ; i++)
  {
    if (strncmp(&digits[i], SYNC_WORD_STR, 16) == 0)
    {
      return i + 16;
    }
  }
  return ST_ERROR;
}
*/

/*
 * Compute the arithmetic mean of tha data in 'data' for which the 
 * corresponding element of include is true.
 */
static size_t average(size_t* data, size_t n, int8_t* labels, int8_t label)
{
  size_t num = 0;
  size_t sum = 0;

  for (size_t i = 0; i < n; ++i)
  {
    if (labels[i] == label)
    {
      num++;
      sum += data[i];
    }
  }

  return num == 0 ? ST_ERROR : sum / num;
}

static size_t max(size_t* data, size_t n)
{
  size_t max = 0;
  for (size_t i = 0; i < n; ++i)
  {
    max = data[i] > max ? data[i] : max; 
  }
  return max;
}

static size_t min(size_t* data, size_t n)
{
  size_t min = SIZE_MAX;
  for (size_t i = 0; i < n; ++i)
  {
    min = data[i] < min ? data[i] : min; 
  }
  return min;
}

/*
 * Stats for each sub-distribution in bimodel; see below.
 */
typedef struct 
{
  int16_t mean;
  size_t num_samples_outside_threshold;
  size_t num_samples;
  bool is_valid;
} DistStats;


static int detect_fps(int16_t* audio_samples, size_t n, 
                      size_t num_samples_per_sec,
                      size_t spike_threshold)
{
  bool seen_spike = false; // Have we seen a spike yet
  size_t samples_since_spike = 0;
  size_t samples_between_spikes[100];
  size_t spike_count = 0;
  int8_t* labels = malloc(n * sizeof(int8_t)); 

  for (size_t i = 0; i < n; ++i, ++samples_since_spike)
  {
    labels[i] = 0;

    // NB: Sometimes the sampling puts two samples in a peak.
    if (abs(audio_samples[i]) > spike_threshold && samples_since_spike > 1)
    {
      if (seen_spike && spike_count < countof(samples_between_spikes))
      {
        samples_between_spikes[spike_count++] = samples_since_spike;
      }   
      samples_since_spike = 0;
      seen_spike = true;
    }
  }

  // If no spikes, then this is not a valid LTC wav file.
  if (!seen_spike || spike_count == 0) return -1;

  // There should be a tight bimodel distribution about two peaks.
  // The lower peak corrsponding to 0s and the upper to 1s
  // The bit rate is given by the lower peak.
  //
  // So, we split into two distributions by dividing the data into those
  // samples that lie above and below the halfway point between the maximum and
  // minimum values.  set. Then, to check that we have reasonable looking data,
  // we check that each distribution is tighly clustered about it's average.
  //
  size_t mid_point = (max(samples_between_spikes, spike_count) 
          + min(samples_between_spikes, spike_count)) >> 1;

  // Partition into values above and below mean.
  for (size_t i = 0; i < spike_count; ++i)
  {
    labels[i] = samples_between_spikes[i] < mid_point ? 0 : 1;
  }

  DistStats low = {0}, high = {0};
  low.mean = average(samples_between_spikes, spike_count, labels, /* label = */ 0);
  high.mean = average(samples_between_spikes, spike_count, labels, /* label = */ 1);

  if (low.mean == ST_ERROR || high.mean == ST_ERROR) return -1;

  // Check that 90% of the samples are within 15% of mean
  size_t threshold = high.mean / 7;
  for (size_t i = 0; i < spike_count; ++i) 
  {
    if (labels[i] == 0)
    {
      if (abs(samples_between_spikes[i] - low.mean) > threshold)
      {
        low.num_samples_outside_threshold++;
      }
      ++low.num_samples;
    }
    else
    {
      if (abs(samples_between_spikes[i] - high.mean) > threshold)
      {
        high.num_samples_outside_threshold++;
      }
      ++high.num_samples;
    }
  }

  free(labels);

  low.is_valid = 1.0 * low.num_samples_outside_threshold / low.num_samples < 0.1;
  high.is_valid = 1.0 * high.num_samples_outside_threshold / high.num_samples < 0.1;

  if (!low.is_valid || !high.is_valid)
  {
    return -1;
  }

  // bits per second = samples per sec / samples per bit  
  // FPS = bits per second / 80 bits per frame
  return num_samples_per_sec / high.mean / 80;
}

#define return_fail {rv = EXIT_FAILURE; goto exit;}
#define return_success {rv = EXIT_SUCCESS; goto exit;}

int main(int argc, char **argv)
{
  char* filename;
  OutputData* output_data = create_output_data(&info_queue, &error_queue);
  int fps = 0;
  int c;
  int rv = EXIT_SUCCESS;

  while ((c = getopt_long (argc, argv,
         "f:" /* fps */
         "h"  /* help */
         "v"  /* verbose */
         "j", /* output JSON */
         long_options, (int *) 0)) != EOF)
  {
    switch (c) {
      case 'f':
        {
        fps = atoi(optarg);
        }
        break;

      case 'v':
        verbosity++;
        break;

      case 'j':
        json_output = true;
        break;

      case 'h':
        usage (0);

      default:
        usage (EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    usage (EXIT_FAILURE);
  }

  filename = argv[optind];


  /*
   * Do the work 
   */
  WavFile* fptr = wav_open(filename, "r");
  
  if (!fptr)
  {
    log_error(500, "Out of memory opening input file");
    return_fail;
  }

  if (wav_err()->code != WAV_OK)
  {
    char* str = wav_err()->message;
    log_error(404, "%s", str);
    return_fail;
  }


  // We are assuming 16 bit signed audio.
  int16_t audio_samples[512];
  char digits[512];
  size_t digit_count=0;
  bool seen_spike = false; // Have we seen a spike yet
  size_t samples_since_spike = 0;
  bool last_digit_was_one = false; // Was the last digit output a 1 ?
  SMPTETimecode starting_timecode;  // 1st code in current range.
  SMPTETimecode last_timecode;      // Last code we saw
  bool seen_starting_timecode = false;

  while (true)
  {
    // Fill up our audio buffer
    size_t num_audio_samples = wav_read(fptr, audio_samples, countof(audio_samples));

    if (num_audio_samples == 0)
    {
      break;
    }

    // Analyse the data to determine the mognitude of the spikes.
    // - We expect a distribution like this
    //
    //                   *|*
    //                   *|*                 
    //   **             **|**                **
    // -------------------|----------------------
    //
    // Most data is clustered around zero with some outliers,
    // which are the spikes.
    // So, we can consider any sample with a magnitude 
    // greater than half the max value to be a spike
    int16_t max = 0;
    for (size_t i = 0; i < num_audio_samples; ++i)
    {
      if (audio_samples[i] > max)
        max = audio_samples[i];
    } 

    int16_t threshold = max >> 1;

    log_info(2, "Using threshold %d", threshold);

    /*
     * If it's the first block of data, calibrate the FPS
     */
    if (digit_count == 0 && fps == 0)
    {
      size_t freq = wav_get_sample_rate(fptr);
      fps = detect_fps(audio_samples, num_audio_samples, freq, threshold);
      
      if (fps == -1)
      {
        log_error(415, "Failed to detect FPS; input does not contain LTC.");
        return_fail;
      }

      log_info(1, "Detected FPS=%d", fps);
    }


    /*
     * Process audio samples to digits.
     */
    size_t short_long_threshold = 0.72 * fps;

    for (size_t i = 0; i < num_audio_samples; ++i, ++samples_since_spike)
    {
      if ((abs(audio_samples[i]) > (max >> 1)) 
          // The sampling might give two adjacent samples in the spike
          && samples_since_spike > 1
          )
      {
        // If this is not the first spike, then it makes sense
        // to calculate the duration since the last spike.
        if (seen_spike)
        {
          if (samples_since_spike < short_long_threshold)
          {
            // Short -> 1
            // (Two spikes equates to a '1', so skip the second)
            if (!last_digit_was_one)
            {
              digits[digit_count++] = '1';
              last_digit_was_one = true;
            }
            else
            {
              last_digit_was_one = false;
            }
          }
          else
          {
            // Long --> 0
            last_digit_was_one = false;
            digits[digit_count++] = '0';
          } 
        }
  
        seen_spike = true;
        samples_since_spike = 0;

        /*
         * If we've over-filled the digits buffer then it's likely that
         * consume_digits() has not found any valid LTC frames. 
         */
        if (digit_count == sizeof(digits))
        {
          log_error(415, "No LTC frames found in input file.");
          return_fail
        }
      }
    }

    /*
     * Consume digits and output time code
     */
    bool got_frame;
    size_t digits_discarded = 0;
    size_t num_digits_consumed = consume_digits(digits, digit_count, 
                                                &digits_discarded, 
                                                &got_frame, 
                                                &last_timecode);

    if (num_digits_consumed == ST_ERROR)
    {
      log_error(408, "Lost synchronisation");
      return_fail;
    }

    if (!seen_starting_timecode)
    {
      output_data->discarded_bits_at_start += digits_discarded;
    }


    // Remove consumed digits from buffer
    memmove(digits, digits + num_digits_consumed, digit_count - num_digits_consumed);
    digit_count -= num_digits_consumed;

    if (got_frame > 0)
    {
      log_info(2, "Frame: %s", timecode_to_str(&last_timecode));

      if (digits_discarded > 0 && seen_starting_timecode)
      {
        log_info(1, "Warning: Gap between LTC frames");
      
        log_info(0, "Timecode range %s --> %s", 
                    timecode_to_str(&starting_timecode),
                    timecode_to_str(&last_timecode));

        // Add to linked list of ranges for JSON.
        timecode_range_append(&output_data->timecode_range_ptr, 
                              create_timecode_range(&starting_timecode, &last_timecode));

        starting_timecode = last_timecode;
      }

      if (!seen_starting_timecode)
      {
        starting_timecode = last_timecode;
        seen_starting_timecode = true;
      }
    }
  }

  if (seen_starting_timecode)
  {
    log_info(0, "Timecode range %s --> %s", 
                timecode_to_str(&starting_timecode),
                timecode_to_str(&last_timecode));

    // Add to linked list of ranges for JSON.
    timecode_range_append(&output_data->timecode_range_ptr, 
                          create_timecode_range(&starting_timecode, &last_timecode));
  }

exit:
  wav_close(fptr);

  // Extract start and end timecodes.
  if (!output_data->timecode_range_ptr)
  {
    log_error(415, "No timecode found in file.");
  }
  else
  {
    output_data->start = output_data->timecode_range_ptr->start;

    for (SMPTETimecodeRange* ptr = output_data->timecode_range_ptr; 
         ptr; 
         ptr = ptr->next_ptr)
    {
      if (!ptr->next_ptr)
      {
        output_data->end = output_data->timecode_range_ptr->end;
      }
    }
  }


  if (json_output)
  {
    output_data_to_json(output_data);
  }

  return rv;
}
