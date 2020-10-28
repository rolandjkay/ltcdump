#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include "wav.h"

typedef char bool;
static const char false = 0;
static const char true = 1;

#define return_fail {rv = EXIT_FAILURE; goto exit;}
#define return_success {rv = EXIT_SUCCESS; goto exit;}

static const uint32_t BEXT = (uint32_t)'txeb';
static const uint32_t IXML = (uint32_t)'LMXi';
static const uint32_t PAD = (uint32_t)' DAP';
static const uint32_t FMT = (uint32_t)' tmf';
static const uint32_t DATA = (uint32_t)'atad';
static const uint32_t RIFF = (uint32_t)'FFIR';
static const uint32_t WAVE = (uint32_t)'EVAW';


static int transfer(FILE* in_ptr, FILE* out_ptr, size_t bytes) 
{
  uint8_t buffer[4096];
  size_t n;

  while (bytes > sizeof(buffer))
  {
    n = fread(buffer, sizeof(buffer), 1, in_ptr); 
    if (n < 1)
    {
      fprintf(stderr, "EOF on input (%d)\n", __LINE__);
      return -1;
    }

    n = fwrite(buffer, sizeof(buffer), 1, out_ptr);
    if (n < 1)
    {
      fprintf(stderr, "Failed to write data (%d)\n", __LINE__);
      return -1;
    }

    bytes -= sizeof(buffer);
  }
  
 
  n = fread(buffer, bytes, 1, in_ptr); 
  if (n < 1)
  {
    fprintf(stderr, "EOF on input (%d)\n", __LINE__);
    return -1;
  }

  n = fwrite(buffer, bytes, 1, out_ptr);
  if (n < 1)
  {
    fprintf(stderr, "Failed to write data (%d)\n", __LINE__);
    return -1;
  }

  return 0;
}

/*
 * Filter all chunks matching 'chunk_ids' and send to output stream
 */
static int filter_riff(uint32_t* chunk_ids, FILE* in_fptr, FILE *out_fptr)
{
  uint32_t data, file_size, file_type;
  size_t n;

  fseek(in_fptr, 0, SEEK_SET);

  /*
   * Read RIFF ID.
   */
  n = fread(&data, 4, 1, in_fptr);
  if (n != 1)
  {
    fprintf(stderr, "EOF on input (%d)\n", __LINE__);
    return -1;
  }

  if (data != RIFF)
  {
    fprintf(stderr, "Not a RIFF file (%d)\n", __LINE__);
    return -1;
  }

  /*
   * Read file size
   */
  n = fread(&file_size, 4, 1, in_fptr);
  if (n != 1)
  {
    fprintf(stderr, "EOF on input (%d)\n", __LINE__);
    return EXIT_FAILURE;
  }

  printf("File size: %d\n", data);


  /*
   * Read file type ID.
   */
  n = fread(&file_type, 4, 1, in_fptr);
  if (n != 1)
  {
    fprintf(stderr, "EOF on input (%d)\n", __LINE__);
    return EXIT_FAILURE;
  }

  printf("File type %.4s, size=%d\n", (char*)&file_type, file_size);

  /*
   * Loop reading chunks
   */
  size_t bytes_written = 0;
  while(true)
  {
    uint32_t chunk_id, chunk_size;

    n = fread(&chunk_id, 4, 1, in_fptr);
    if (n != 1)
    {
      break; // EOF
    }

    n = fread(&chunk_size, 4, 1, in_fptr);
    if (n != 1)
    {
      fprintf(stderr, "EOF on input (%d)\n", __LINE__);
      return -1;
    }
  
    printf("Chunk %.4s, size=%d: ", (char*)&chunk_id, chunk_size);

    // A matching chunk?
    bool match = false;
    for (uint32_t * match_chunk_id = chunk_ids; *match_chunk_id; ++match_chunk_id)
    {
      if (*match_chunk_id ==  chunk_id)
      {
         match = true;
         break;
      }
    }

    if (match)
    {
      printf("COPYING\n");
      // Seek back so that we copy the chunk header
      if (fseek(in_fptr, -8, SEEK_CUR) == -1)
      {
        fprintf(stderr, "Seek failed on input (%d)\n", __LINE__);
        return -1;
      }

      n = transfer(in_fptr, out_fptr, chunk_size + 8); 
      if (n == -1)
        return -1;

       bytes_written += chunk_size;
    }
    else
    {
      printf("SKIPPING\n");
      fseek(in_fptr, chunk_size, SEEK_CUR);
    }
  }


  return bytes_written;
}


static void usage (int status) 
{
  printf ("riff_merge - Merge meta data chunks from one file with data chunks from another\n\n");
  printf ("Usage: riff_merge <metadata filename> <data filename> <output filename>\n\n");

  exit (status);
}

int main(int argc, char **argv)
{
  uint32_t data;
  size_t n;
  size_t file_size = 0;
  int m;
  int rv = EXIT_SUCCESS;
  FILE* meta_in_fptr = NULL;
  FILE* data_in_fptr = NULL;
  FILE* out_fptr = NULL;
  uint32_t metadata_chunk_ids[] = {BEXT, IXML, PAD, 0};
  uint32_t data_chunk_ids[] = {FMT, DATA, 0};

  if (argc != 3) usage(EXIT_FAILURE);

  const char* metadata_filename = argv[1];
  const char* data_filename = argv[2];
  const char* out_filename = argv[3];

  // Check that output file doesn't exist
  if (access(out_filename, F_OK) != -1)
  {
    char* line = NULL;
    size_t len = 0;
    ssize_t num_bytes;

    printf("File '%s' already exists. Overwirte ?  [y/N] ", out_filename);

    num_bytes = getline(&line, &len, stdin);
    if (num_bytes == -1)
    {
      return_fail;
    }
    else if (num_bytes != 2 && (line[0] != 'y' || line[0] != 'Y'))
    {
      return_success; 
    }
    free(line);
  }

  meta_in_fptr = fopen(metadata_filename, "r");
  data_in_fptr = fopen(data_filename, "r");
  out_fptr = fopen(out_filename, "w");

  // Write WAVE header
  data = RIFF;
  n = fwrite(&data, 4, 1, out_fptr);
  if (n < 1)
  {
    fprintf(stderr, "EOF on output (%d)\n", __LINE__);
    return_fail;
  }

  data = 0;
  n = fwrite(&data, 4, 1, out_fptr);
  if (n < 1)
  {
    fprintf(stderr, "EOF on output (%d)\n", __LINE__);
    return_fail;
  }

  data = WAVE;
  n = fwrite(&data, 4, 1, out_fptr);
  if (n < 1)
  {
    fprintf(stderr, "EOF on output (%d)\n", __LINE__);
    return_fail;
  }



  m = printf("Filtering meta data from %s...\n", "sss");
  printf("%.*s\n", m-1, "===========================================================================================");

  n = filter_riff(metadata_chunk_ids, meta_in_fptr, out_fptr);
  if (n == -1) return_fail;
  file_size += n;

  printf("\n\n\n");

  m = printf("Filtering data from %s...\n", "sss");
  printf("%.*s\n", m-1, "===========================================================================================");
  n = filter_riff(data_chunk_ids, data_in_fptr, out_fptr);
  if (n == -1) return_fail;
  file_size += n;

  if (fseek(out_fptr, 4, SEEK_SET) == -1)
  {
    fprintf(stderr, "Failed to seek to set filesize (%d)\n", __LINE__);
    return_fail;
  }

  n = fwrite(&file_size, 4, 1, out_fptr);
  if (n < 1)
  {
    fprintf(stderr, "EOF on output (%d)\n", __LINE__);
    return_fail;
  }

exit:
  fclose(meta_in_fptr);
  fclose(data_in_fptr);
  fclose(out_fptr);
  return rv;
}

