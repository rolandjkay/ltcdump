#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include "wav.h"

int verbosity = 1;

typedef char bool;
static const char false = 0;
static const char true = 1;

// Unsigned error return value
static const size_t ST_ERROR = 0xffffffff;

static const char* SYNC_WORD_STR = "0011111111111101";
static unsigned int SYNC_WORD = 0xbffc;

#define countof(x)  (sizeof(x) / sizeof(x[0]))

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

static void usage (int status) 
{
  printf ("ltcdump - parse linear time code from a audio-file.\n\n");
  printf ("Usage: ltcdump [ OPTIONS ] <filename>\n\n");
  printf ("Options:\n\
  -f, --fps <num>         set framerate\n\
  -v, --verbose           set debug info displau\n\
  -h, --help              display this help and exit\n\
\n");

  exit (status);
}


static struct option const long_options[] =
{
  {"help", no_argument, 0, 'h'},
  {"fps", required_argument, 0, 'f'},
  {"verbose", no_argument, 0, 'v'},
  {NULL, 0, NULL, 0}
};



/*
 * Decode a string containg characters '0' and '1' which represent a
 * sequence of LTC frame. Print the timestamps found in these frames
 */
static size_t consume_digits(char* digits, size_t n)
{
  LTCFrame frame;
  size_t frame_count = 0;
  size_t bytes_discarded = 0;

  /*
   * We're looking for 80 characters that end in SYNC_WORD_STR
   */
  for (; n > 80; --n, ++digits, ++bytes_discarded)
  {
    if (strncmp(&digits[80-16], SYNC_WORD_STR, 16) == 0)
    {
      if (verbosity > 2)
        printf("*** Got frame: %.80s\n", digits);
      break;
    }
    else
    {
      printf("*** Looking for sync word %.80s\n", digits);
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

     SMPTETimecode stime;
     ltc_frame_to_time(&stime, &frame);

     printf("%02d:%02d:%02d:%02d\n", (int)stime.hours, (int)stime.mins, (int)stime.secs, (int)stime.frame);
  }

  return frame_count * 80 + bytes_discarded;
}

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

int main(int argc, char **argv)
{
  char* filename;
  int fps;
  int c;

  while ((c = getopt_long (argc, argv,
         "f:" /* fps */
         "h"  /* help */
         "v", /* verbose */
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
  size_t short_long_threshold = 0.72 * 25;

  WavFile* fptr = wav_open(filename, "r");
  
  if (!fptr)
  {
    fprintf(stderr, "Can't open file\n");
    return EXIT_FAILURE;
  }

  // We are assuming 16 bit signed audio.
  int16_t audio_samples[512];
  char digits[512];
  size_t digit_count=0;
  bool seen_spike = false; // Have we seen a spike yet
  size_t samples_since_spike = 0;
  bool last_digit_was_one = false; // Was the last digit output a 1 ?
  bool first_block = true;

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

    if (verbosity > 2)
    {
      printf("*** Using threshold %d\n", max >> 1);
    }

    /*
     * Process audio samples to digits.
     */
    for (size_t i = 0; i < num_audio_samples; ++i)
    {
      if ((abs(audio_samples[i]) > (max >> 1)) 
      // The sampling might give two adjacent samples in the spike
  		    && samples_since_spike != 0
  		    )
      {

        if (seen_spike && samples_since_spike < 18)
        {
          // Short -> 1
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
  
        seen_spike = true;
        samples_since_spike = 0;

	/*
	 * If we've over-filled the digits buffer then it's likely that
	 * consume_digits() has not found any valid LTC frames. 
	 */
	if (digit_count == sizeof(digits))
	{
          fprintf(stderr, "No LTC frames found in input file.\n");
	  goto exit;
	}
      }
      else
      {
        ++samples_since_spike;
      }
    }

    // If this is the first block, we need to throw away
    // digits until we get to the first frame.
     /*
    size_t offset = 0;
    if (first_block)
    {
      first_block = false;
      printf("%s\n",digits);
      offset = find_first_frame(digits, digit_count);
      if (offset == ST_ERROR)
      {
        fprintf(stderr, "Failed to find LTC frame.\n");
        break;
      }
    }
     printf("%d\n", offset);
      printf("%s\n",&digits[offset]);
      */

    /*
     * Consume digits and output time code
     */
    size_t num_digits_consumed = consume_digits(digits, digit_count);

    if (num_digits_consumed == ST_ERROR)
    {
      fprintf(stderr, "Lost synchronisation.\n");
      goto exit;
    }

    // Remove consumed digits from buffer
    memmove(digits, digits + num_digits_consumed, digit_count - num_digits_consumed);
    digit_count -= num_digits_consumed;
  }

exit:
  wav_close(fptr);

  printf("\
{\
\"Success\" : false,\n\
\"ErrorMsg\" : \"usb dongle missing\"\n\
\"DigitsBeforeFirstFrame\" : 40\n\
\"Start\" : \"17:30:32:13\"\n\
\"End\" : \"17:35:32:23\"\n\
}\n\
");


  return 0;
}
