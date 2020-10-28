/*
 * To get perfect synchronisation, we need to strip the first partial
 * frame from the audio recorder's audio track. This utility helps
 * with this. 
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include "wav.h"

typedef char bool;
//static const char false = 0;
static const char true = 1;

int trim_start_of_wav_file(const char* input_filename,
                           const char* output_filename,
                           size_t num_samples)
{
  // Num of samples to write out in each block.
  static const size_t block_size = 4096;
  uint8_t* buffer = NULL;
  WavFile* in_fptr = wav_open(input_filename, "r");
  WavFile* out_fptr = wav_open(output_filename, "w");

  WavU16 n_channels = wav_get_num_channels(in_fptr);
  size_t sample_size = wav_get_sample_size(in_fptr);

  wav_set_num_channels(out_fptr, n_channels);
  wav_set_sample_size(out_fptr, sample_size);
  wav_set_format(out_fptr, wav_get_format(in_fptr));
  wav_set_sample_rate(out_fptr, wav_get_sample_rate(in_fptr));
  wav_set_valid_bits_per_sample(out_fptr, wav_get_valid_bits_per_sample(in_fptr));

  ssize_t num_blocks_to_skip = num_samples / block_size;
  ssize_t num_samples_to_skip = num_samples % block_size;

  buffer = malloc(sample_size * n_channels * block_size);

  while (true)
  {
    size_t samples_read = wav_read(in_fptr, buffer, block_size);

    if (samples_read != block_size)
    {
      if (wav_err()->code != WAV_OK)
      {
        fprintf(stderr, "Error reading wav file: %s", wav_err()->message);
        return -1;
      }
      else
      {
        break;
      }
    }

    if (num_blocks_to_skip-- > 0) continue;

    size_t shortened_block_size = block_size - num_samples_to_skip;
    size_t samples_written = wav_write(out_fptr, 
                                       buffer + num_samples_to_skip, 
                                       block_size - num_samples_to_skip);
    num_samples_to_skip = 0;

    if (samples_written != block_size - shortened_block_size && wav_err()->code != WAV_OK)
    {
      fprintf(stderr, "Error writing wav file: %s", wav_err()->message);
      return -1;
    }
  }

  wav_close(in_fptr);
  wav_close(out_fptr);

  return 0;
}


static void usage (int status)
{
  printf ("trim_wav - Trim samples from beginning of wav file.\n\n");
  printf ("Usage: ltcdump -n xxx <input filename> <output filename>\n\n");
  printf ("Options:\n\
  -n, --num_samples <num>   number of samples to trim\n\
  -h, --help                display this help and exit\n\
  \n");

  exit (status);
}

static struct option const long_options[] =
{
  {"help", no_argument, 0, 'h'},
  {"num_samples", required_argument, 0, 'f'},
  {NULL, 0, NULL, 0}
};


int main(int argc, char **argv)
{
  int num_samples = -1;
  int c;

  while ((c = getopt_long (argc, argv,
         "n:"  /* Number of samples to trim */
         "h" , /* help */
         long_options, (int *) 0)) != EOF)
  {
    switch (c) {
      case 'n':
        {
        num_samples = atoi(optarg);
        }
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


  if (argc - optind != 2)
  {
    usage(EXIT_FAILURE);
  }

  const char* input_filename = argv[optind];
  const char* output_filename = argv[optind + 1];

  if (num_samples == -1)
  {
    fprintf(stderr, "Please use -n to specify number of samples to trim.\n");
    return EXIT_FAILURE;
  }

  return trim_start_of_wav_file(input_filename, output_filename, num_samples) ? EXIT_SUCCESS : EXIT_FAILURE;
}
