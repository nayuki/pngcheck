/*
 * authenticate a PNG file (as per draft 10)
 *
 * this program checks the PNG identifier with conversion checks,
 * the file structure and the chunk CRCs.
 *
 * With -v switch, the chunk names are printed.
 * with -t switch, text chunks are printed (without any charset conversion,
 *                                          works on X11R>=5 OK)
 *
 * written by Alexander Lehmann <alex@hal.rhein-main.de>
 *
 *
 * 23.02.95 fixed wrong magic numbers
 *
 * 13.03.95 crc code from png spec, compiles on memory impaired PCs now,
 *          check for IHDR/IEND chunks
 *
 * 23 Mar 95  glennrp rewrote magic number checking and moved it to
 *             PNG_check_magic(buffer)
 *
 * 27.03.95 AL: fixed CRC code for 64 bit, -t switch, unsigned char vs. char
 *          pointer changes
 *
 * 01.06.95 AL: check for data after IEND chunk
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int PNG_check_magic(unsigned char *magic);
int PNG_check_chunk_name(char *chunk_name);

#define BS 32000 /* size of read block for CRC calculation */
int verbose; /* ==1 print chunk info */
int printtext; /* ==1 print tEXt chunks */
char *fname;
unsigned char buffer[BS];

/* table of crc's of all 8-bit messages */
unsigned long crc_table[256];

/* Flag: has the table been computed? Initially false. */
int crc_table_computed = 0;

/* make the table for a fast crc */
void make_crc_table(void)
{
  unsigned long c;
  int n, k;

  for (n = 0; n < 256; n++)
  {
    c = (unsigned long)n;
    for (k = 0; k < 8; k++)
      c = c & 1 ? 0xedb88320L ^ (c >> 1) : c >> 1;
    crc_table[n] = c;
  }
  crc_table_computed = 1;
}

/* update a running crc with the bytes buf[0..len-1]--the crc should be
   initialized to all 1's, and the transmitted value is the 1's complement
   of the final running crc. */

unsigned long update_crc(unsigned long crc, unsigned char *buf, int len)
{
  unsigned long c = crc;
  unsigned char *p = buf;
  int n = len;

  if (!crc_table_computed) {
    make_crc_table();
  }
  if (n > 0) do {
    c = crc_table[(c ^ (*p++)) & 0xff] ^ (c >> 8);
  } while (--n);
  return c;
}

/* use these instead of ~crc and -1, since that doesn't work on machines that
   have 64 bit longs */

#define CRCCOMPL(c) ((c)^0xffffffff)
#define CRCINIT (CRCCOMPL(0))

unsigned long getlong(FILE *fp)
{
  unsigned long res=0;
  int c;
  int i;

  for(i=0;i<4;i++) {
    if((c=fgetc(fp))==EOF) {
      printf("%s: EOF while reading 4 bytes value\n", fname);
      return 0;
    }
    res<<=8;
    res|=c&0xff;
  }
  return res;
}

void pngcheck(FILE *fp, char *_fname)
{
  long s;
  unsigned char magic[8];
  char chunkid[5];
  int toread;
  int c;
  unsigned long crc, filecrc;
  int first=1;
  int iend_read=0;

  fname=_fname; /* make filename available to functions above */

  if(fread(magic, 1, 8, fp)!=8) {
    printf("%s: Cannot read PNG header\n", fname);
    return;
  }

  if (PNG_check_magic(magic) != 0) return;

  while((c=fgetc(fp))!=EOF) {
    ungetc(c, fp);
    if(iend_read) {
      printf("%s: additional data after IEND chunk\n", fname);
      return;
    }
    s=getlong(fp);
    if(fread(chunkid, 1, 4, fp)!=4) {
      printf("%s: EOF while reading chunk type\n", fname);
      return;
    }

    chunkid[4]=0;

    if (PNG_check_chunk_name(chunkid) != 0) return;

    if(verbose) {
      printf("%s: chunk %s at %lx length %lx\n", fname, chunkid, ftell(fp)-4, s);
    }

    if(first && strcmp(chunkid,"IHDR")!=0) {
      printf("%s: file doesn't start with a IHDR chunk\n", fname);
    }
    first=0;

    crc=update_crc(CRCINIT, (unsigned char *)chunkid, 4);

    while(s>0) {
      toread=s;
      if(toread>BS) {
        toread=BS;
      }
      if(fread(buffer, 1, toread, fp)!=toread) {
        printf("%s: EOF while reading chunk data (%s)\n", fname, chunkid);
        return;
      }
      crc=update_crc(crc, buffer, toread);
      s-=toread;
      if(printtext && strcmp(chunkid, "tEXt")==0) {
        if(strlen((char *)buffer)<toread) {
          buffer[strlen((char *)buffer)]=':';
        }
        fwrite(buffer, 1, toread, stdout);
      }
    }
    if(printtext && strcmp(chunkid, "tEXt")==0) {
      printf("\n");
    }
    filecrc=getlong(fp);
    if(filecrc!=CRCCOMPL(crc)) {
      printf("%s: CRC error in chunk %s (actual %08lx, should be %08lx)\n",
              fname, chunkid, CRCCOMPL(crc), filecrc);
      return;
    }
    if(strcmp(chunkid, "IEND")==0) {
      iend_read=1;
    }
  }
  if(!iend_read) {
    printf("%s: file doesn't end with a IEND chunk\n", fname);
    return;
  }
  printf("%s: file appears to be OK\n", fname);
}

int main(int argc, char *argv[])
{
  FILE *fp;
  int i;

  if(argc>1 && strcmp(argv[1],"-v")==0) {
    verbose=1;
    argc--;
    argv++;
  } else
  if(argc>1 && strcmp(argv[1],"-t")==0) {
    printtext=1;
    argc--;
    argv++;
  }

  if(argc==1) {
    pngcheck(stdin, "stdin");
  } else {
    for(i=1;i<argc;i++) {
      if((fp=fopen(argv[i],"rb"))==NULL) {
        perror(argv[i]);
      } else {
        pngcheck(fp, argv[i]);
        fclose(fp);
      }
    }
  }

  return 0;
}

/*  PNG_subs
 *
 *  Utility routines for PNG encoders and decoders
 *  by Glenn Randers-Pehrson
 *
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define OK      0
#define ERROR   -1
#define WARNING -2

/* (int)PNG_check_magic ((unsigned char*) magic)
 *
 * check the magic numbers in 8-byte buffer at the beginning of
 * a PNG file.
 *
 * by Alexander Lehmann and Glenn Randers-Pehrson
 *
 * This is free software; you can redistribute it and/or modify it
 * without any restrictions.
 *
 */

int PNG_check_magic(unsigned char *magic)
{
    if (strncmp((char *)&magic[1],"PNG",3) != 0) {
             fprintf(stderr, "not a PNG file\n");
             return(ERROR);
            }

    if (magic[0] != 0x89 ||
         strncmp((char *)&magic[4],"\015\012\032\012",4) != 0) {
         fprintf(stderr, "PNG file is CORRUPTED.\n");

         /* this coding taken from Alexander Lehmanns checkpng code   */

        if(strncmp((char *)&magic[4],"\n\032",2) == 0) fprintf
             (stderr," It seems to have suffered DOS->unix conversion\n");
        else
        if(strncmp((char *)&magic[4],"\r\032",2) == 0) fprintf
             (stderr," It seems to have suffered DOS->Mac conversion\n");
        else
        if(strncmp((char *)&magic[4],"\r\r\032",3) == 0) fprintf
             (stderr," It seems to have suffered unix->Mac conversion\n");
        else
        if(strncmp((char *)&magic[4],"\n\n\032",3) == 0) fprintf
             (stderr," It seems to have suffered Mac-unix conversion\n");
        else
        if(strncmp((char *)&magic[4],"\r\n\032\r",4) == 0) fprintf
             (stderr," It seems to have suffered unix->DOS conversion\n");
        else
        if(strncmp((char *)&magic[4],"\r\r\n\032",4) == 0) fprintf
             (stderr," It seems to have suffered unix->DOS conversion\n");
        else
        if(strncmp((char *)&magic[4],"\r\n\032\n",4) != 0) fprintf
             (stderr," It seems to have suffered EOL conversion\n");

        if(magic[0]==9) fprintf
             (stderr," It was probably transmitted through a 7bit channel\n");
        else
        if(magic[0]!=0x89) fprintf
             (stderr,"  It was probably transmitted in text mode\n");
        /*  end of Alexander Lehmann's code  */
        return(ERROR);
        }
    return (OK);
}

/* (int)PNG_check_magic ((char*) magic)
 *
 * from Alex Lehmann
 *
 */

int PNG_check_chunk_name(char *chunk_name)
{
     if(!isalpha(chunk_name[0]) || !isalpha(chunk_name[1]) ||
        !isalpha(chunk_name[2]) || !isalpha(chunk_name[3])) {
         printf("chunk name %02x %02x %02x %02x doesn't comply to naming rules\n",
         chunk_name[0],chunk_name[1],chunk_name[2],chunk_name[3]);
         return (ERROR);
         }
     else return (OK);
}
