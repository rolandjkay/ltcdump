/*
 * To get perfect synchronisation, we need to pad with enough silence
 * to cover the first partial frame from the audio recorder's audio track. This
 * utility helps with this. 
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include "wav.h"

typedef enum {
  UNITS_MICROSECONDS,
  UNITS_SAMPLES
} Units;

typedef char bool;
//static const char false = 0;
static const char true = 1;

int pad_start_of_wav_file(const char* input_filename,
                          const char* output_filename,
                          size_t n,
                          Units units)
{
  int rv = -1;
  // Num of samples to write out in each block.
  static const size_t block_size = 4096;
  uint8_t* buffer = NULL;
  size_t num_samples;
  WavFile* in_fptr = wav_open(input_filename, "r");
  WavFile* out_fptr = wav_open(output_filename, "w");

  WavU16 n_channels = wav_get_num_channels(in_fptr);
  size_t sample_size = wav_get_sample_size(in_fptr);

  wav_set_num_channels(out_fptr, n_channels);
  wav_set_sample_size(out_fptr, sample_size);
  wav_set_format(out_fptr, wav_get_format(in_fptr));
  wav_set_sample_rate(out_fptr, wav_get_sample_rate(in_fptr));
  wav_set_valid_bits_per_sample(out_fptr, wav_get_valid_bits_per_sample(in_fptr));

  // Convert microseconds to num samples.
  if (units == UNITS_SAMPLES)
  {
    num_samples = n;
  }
  else
  {
    num_samples = wav_get_sample_rate(in_fptr) * n / 1000000;
  }

  buffer = malloc(sample_size * num_samples * n_channels);
  memset(buffer, 0, sample_size * num_samples * n_channels);
  size_t samples_written = wav_write(out_fptr, buffer, num_samples);

  if (samples_written != num_samples && wav_err()->code != WAV_OK)
  {
    fprintf(stderr, "Error writing wav file: %s", wav_err()->message);
    goto exit;    
  }
 
  free(buffer);

  /*
  ssize_t num_blocks_to_skip = num_samples / block_size;
  ssize_t num_samples_to_skip = num_samples % block_size;
  */

  buffer = malloc(sample_size * n_channels * block_size);

  while (true)
  {
    size_t samples_read = wav_read(in_fptr, buffer, block_size);

    if (samples_read != block_size)
    {
      if (wav_err()->code != WAV_OK)
      {
        fprintf(stderr, "Error reading wav file: %s", wav_err()->message);
        goto exit;
      }
    }

    /* if (num_blocks_to_skip-- > 0) continue;

    size_t shortened_block_size = block_size - num_samples_to_skip;
    size_t samples_written = wav_write(out_fptr, 
                                       buffer + num_samples_to_skip, 
                                       samples_read - num_samples_to_skip);
    num_samples_to_skip = 0;

    if (samples_written != samples_read - shortened_block_size  && wav_err()->code != WAV_OK)
    {
      fprintf(stderr, "Error writing wav file: %s", wav_err()->message);
      goto exit;
    }
    */

    samples_written = wav_write(out_fptr, buffer, samples_read);

    if (samples_written != samples_read && wav_err()->code != WAV_OK)
    {
      fprintf(stderr, "Error writing wav file: %s", wav_err()->message);
      goto exit;
    }

    if (samples_read != block_size) break;
  }

  rv = 0;

exit:
  free(buffer);
  wav_close(in_fptr);
  wav_close(out_fptr);

  return rv;
}


static void usage (int status)
{
  printf ("pad_wav - Pad beginning of wav file with silence.\n\n");
  printf ("Usage: pad_wav [ -n xxx | -m xxx] <input filename> <output filename>\n\n");
  printf ("Options:\n\
  -n, --num_samples <num>   number of samples to pad\n\
  -m, --microseconds <num>  number of microseconds to pad\n\
  -h, --help                display this help and exit\n\
  \n");

  exit (status);
}

static struct option const long_options[] =
{
  {"help", no_argument, 0, 'h'},
  {"num_samples", required_argument, 0, 'n'},
  {"microseconds", required_argument, 0, 'm'},
  {NULL, 0, NULL, 0}
};


int main(int argc, char **argv)
{
  int num_samples = -1;
  int microseconds = -1;
  int c;

  while ((c = getopt_long (argc, argv,
         "n:"  /* Number of samples to pad */
         "m:"  /* Number of samples to pad */
         "h" , /* help */
         long_options, (int *) 0)) != EOF)
  {
    switch (c) {
      case 'n':
        {
        num_samples = atoi(optarg);
        }
        break;

      case 'm':
        {
        microseconds = atoi(optarg);
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

  if (num_samples == -1 && microseconds == -1)
  {
    fprintf(stderr, "Please use -n or -m to specify how much to pad.\n");
    return EXIT_FAILURE;
  }

  // Check that output file doesn't exist
  if (access(output_filename, F_OK) != -1)
  {
    char* line = NULL;
    size_t len = 0;
    ssize_t num_bytes;

    printf("File '%s' already exists. Overwirte ?  [y/N] ", output_filename);

    num_bytes = getline(&line, &len, stdin);
    if (num_bytes == -1)
    {
      fprintf(stderr, "Failed to read line of input from user.\n");
      return EXIT_FAILURE;
    }
    else if (num_bytes != 2 && (line[0] != 'y' || line[0] != 'Y'))
    {
      return EXIT_SUCCESS;
    }
    free(line);
  }

  return pad_start_of_wav_file(input_filename, 
	                output_filename, 
			num_samples == -1 ? microseconds : num_samples,
			num_samples == -1 ? UNITS_MICROSECONDS : UNITS_SAMPLES
			)  == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
