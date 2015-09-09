
/*************************************************************************
*
*      GSM AMR-NB speech codec   R98   Version 7.6.0   December 12, 2001
*                                R99   Version 3.3.0                
*                                REL-4 Version 4.1.0                
*
********************************************************************************
*
*      File             : decoder.c
*      Purpose          : Speech decoder main program.
*
********************************************************************************
*
*         Usage : decoder  bitstream_file  synth_file
*
*
*         Format for bitstream_file:
*             1 word (2-byte) for the frame type
*               (see frame.h for possible values)
*               Normally, the TX frame type is expected.
*               RX frame type can be forced with "-rxframetype"
*           244 words (2-byte) containing 244 bits.
*               Bit 0 = 0x0000 and Bit 1 = 0x0001
*             1 word (2-byte) for the mode indication
*               (see mode.h for possible values)
*             4 words for future use, currently unused
*
*         Format for synth_file:
*           Synthesis is written to a binary file of 16 bits data.
*
********************************************************************************
*/

/*
********************************************************************************
*                         INCLUDE FILES
********************************************************************************
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <alsa/asoundlib.h>   /*adding the ALSA head file, added by XRQ*/
#include "typedef.h"
#include "n_proc.h"
#include "cnst.h"
#include "mode.h"
#include "frame.h"
#include "strfunc.h"
#include "sp_dec.h"
#include "d_homing.h"

/*#define concerning ALSA, added by XRQ*/
#define ALSA_PCM_NEW_HW_PARAMES_API

const char decoder_id[] = "@(#)$Id $";

/* frame size in serial bitstream file (frame type + serial stream + flags) */
#define SERIAL_FRAMESIZE (1+MAX_SERIAL_SIZE+5)

/*
********************************************************************************
*                         LOCAL PROGRAM CODE
********************************************************************************
*/
static enum RXFrameType tx_to_rx (enum TXFrameType tx_type)
{
    switch (tx_type) {
      case TX_SPEECH_GOOD:      return RX_SPEECH_GOOD;
      case TX_SPEECH_DEGRADED:  return RX_SPEECH_DEGRADED;
      case TX_SPEECH_BAD:       return RX_SPEECH_BAD;
      case TX_SID_FIRST:        return RX_SID_FIRST;
      case TX_SID_UPDATE:       return RX_SID_UPDATE;
      case TX_SID_BAD:          return RX_SID_BAD;
      case TX_ONSET:            return RX_ONSET;
      case TX_NO_DATA:          return RX_NO_DATA;
      default:
        fprintf(stderr, "tx_to_rx: unknown TX frame type %d\n", tx_type);
        exit(1);
    }
}

snd_pcm_t *handle;    /*pcm handle used in the whole file*/

/*function declaration, added by XRQ*/
void waveformating(); 
void Dec2Hex(long size, char result[]);
int Convert(char a, char b);
int Hex2Dec(char a);
int ALSA_ini();


/*
********************************************************************************
*                             MAIN PROGRAM 
********************************************************************************
*/

int main ()
{
  Speech_Decode_FrameState *speech_decoder_state = NULL;
  
  Word16 serial[SERIAL_FRAMESIZE];   /* coded bits                    */
  Word16 synth[L_FRAME];             /* Synthesis                     */
  Word32 frame;

/*
  char *progname = argv[0];
*/
/*
  char *fileName = NULL;
  char *serialFileName = NULL;
*/

  char fileName[] = "test.out";
  char serialFileName[] = "test.cod";
  FILE *file_syn, *file_serial;

  int rxframetypeMode = 0;           /* use RX frame type codes       */
  enum Mode mode = (enum Mode)0;
  enum RXFrameType rx_type = (enum RXFrameType)0;
  enum TXFrameType tx_type = (enum TXFrameType)0;
     
  Word16 reset_flag = 0;
  Word16 reset_flag_old = 1;
  Word16 i;
  int temp;   /*a variable to get the return value of ALSA initialization function*/
  int rc;     /**a variable to get the return value of function snd_pcm_writei(). added by XRQ/
  
  proc_head ("Decoder");
/*abandon this judge because we do not need this*/

  /*----------------------------------------------------------------------*
   * process command line options                                         *
   *----------------------------------------------------------------------*/
  /*
  while (argc > 1) {
      if (strcmp(argv[1], "-rxframetype") == 0)
          rxframetypeMode = 1;
      else break;

      argc--;
      argv++;
  }
  */
  /*----------------------------------------------------------------------*
   * check number of arguments                                            *
   *----------------------------------------------------------------------*/
/*This is no need to judge the number of the arguments.
  if (argc != 3)
  {
    fprintf (stderr,
      " Usage:\n\n"
      "   decoder  [-rxframetype] bitstream_file synth_file\n\n"
      " -rxframetype expects the RX frame type in bitstream_file (instead of TX)\n\n");
      exit (1);
  }
*/

  /*ALSA initialization: initialization the sound hardware and setting parameters, added by XRQ*/
  temp = ALSA_ini();

  /*----------------------------------------------------------------------*
   * Open serial bit stream and output speech file                        *
   *----------------------------------------------------------------------*/
  if (strcmp(serialFileName, "-") == 0) {
     file_serial = stdin;
  }
  else if ((file_serial = fopen (serialFileName, "rb")) == NULL)
  {
      fprintf (stderr, "Input file '%s' does not exist !!\n", serialFileName);
      exit (0);
  }
  fprintf (stderr, "Input bitstream file:   %s\n", serialFileName);

  if (strcmp(fileName, "-") == 0) {
     file_syn = stdout;
  }
  else if ((file_syn = fopen (fileName, "wb")) == NULL)
  {
      fprintf (stderr, "Cannot create output file '%s' !!\n", fileName);
      exit (0);
  }
  fprintf (stderr, "Synthesis speech file:  %s\n", fileName);

  /*-----------------------------------------------------------------------*
   * Initialization of decoder                                             *
   *-----------------------------------------------------------------------*/
  if (Speech_Decode_Frame_init(&speech_decoder_state, "Decoder"))
      exit(-1);
    
  /*-----------------------------------------------------------------------*
   * process serial bitstream frame by frame                               *
   *-----------------------------------------------------------------------*/
  frame = 0;
  while (fread (serial, sizeof (Word16), SERIAL_FRAMESIZE, file_serial)
         == SERIAL_FRAMESIZE)
  {
     ++frame;
     if ( (frame%50) == 0) {
        fprintf (stderr, "\rframe=%d  ", frame);
     }

     /* get frame type and mode information from frame */
     if (rxframetypeMode) {
         rx_type = (enum RXFrameType)serial[0];
     } else {
         tx_type = (enum TXFrameType)serial[0];
         rx_type = tx_to_rx (tx_type);
     }
     mode = (enum Mode) serial[1+MAX_SERIAL_SIZE];
     if (rx_type == RX_NO_DATA) {
       mode = speech_decoder_state->prev_mode;
     }
     else {
       speech_decoder_state->prev_mode = mode;
     }

     /* if homed: check if this frame is another homing frame */
     if (reset_flag_old == 1)
     {
         /* only check until end of first subframe */
         reset_flag = decoder_homing_frame_test_first(&serial[1], mode);
     }
     /* produce encoder homing frame if homed & input=decoder homing frame */
     if ((reset_flag != 0) && (reset_flag_old != 0))
     {
         for (i = 0; i < L_FRAME; i++)
         {
             synth[i] = EHF_MASK;
         }
     }
     else
     {     
         /* decode frame */
         Speech_Decode_Frame(speech_decoder_state, mode, &serial[1],
                             rx_type, synth);
     }
     
     /* write synthesized speech to file */
     if (fwrite (synth, sizeof (Word16), L_FRAME, file_syn) != L_FRAME) {
         fprintf(stderr, "\nerror writing output file: %s\n",
                 strerror(errno));
     };

     /*the following lines are inherited from ALSA.c, driving the hardware to sound when getting data from array synth, added by XRQ*/
     rc = snd_pcm_writei(handle,synth,160);   
    if(rc == -EPIPE)
    {
      printf("under occurred\n");
      snd_pcm_prepare(handle);
    } else if (rc < 0) 
    {
      printf("error from writei: %s\n",snd_strerror(rc));
    } else if (rc != 160) 
    {
      printf("short write, write %d frames\n",rc);
    } 

     fflush(file_syn);

     /* if not homed: check whether current frame is a homing frame */
     if (reset_flag_old == 0)
     {
         /* check whole frame */
         reset_flag = decoder_homing_frame_test(&serial[1], mode);
     }
     /* reset decoder if current frame is a homing frame */
     if (reset_flag != 0)
     {
         Speech_Decode_Frame_reset(speech_decoder_state);
     }
     reset_flag_old = reset_flag;

  }
  fprintf (stderr, "\n%d frame(s) processed\n", frame);

  /*After driving the sound hardware, the PCM interface should be released*/
  snd_pcm_drain(handle);
  snd_pcm_close(handle);

  /*we are to form the .wav file in local storage. The following is added by XRQ*/
  fprintf(stderr, "The decoding is done, and the next step is to form the .wav file in local storage.\n");

  waveformating();
  fprintf(stderr, "We've successfully get the sound file(.wav)!\n");

  
  /*-----------------------------------------------------------------------*
   * Close down speech decoder                                             *
   *-----------------------------------------------------------------------*/
  Speech_Decode_Frame_exit(&speech_decoder_state);
  
  return 0;
}

/*The following is the function defination, added by XRQ*/
void waveformating()
{
  /*codes written by XRQ*/
  int len, i = 0, handle = 0,header4 = 0,header5 = 0,header6 = 0,header7 = 0,header40 = 0,header41 = 0,header42 = 0,header43 = 0;   /*8 sensitive variable concerning the size of the .wav file*/
  long size,fullsize,f_size,b_size;   /*f_size and b_size are Bytes at offset 04H and 28H*/
  char f_sizeHex[8], b_sizeHex[8],buffer[1024],header[44];
  FILE * pFile, * qFile;

  /*the .wav sound file head concerning the sound file info.*/
  /*
	according to http://stackoverflow.com/questions/160960/error-initializer-element-is-not-computable-at-load-time, I have to change the folloing code.
  char header[44] = {
    (char)82,(char)73,(char)70,(char)70,(char)header4,(char)header5,(char)header6,(char)header7,(char)87,(char)65,(char)86,
    (char)69,(char)102,(char)109,(char)116,(char)32,(char)16,(char)0,(char)0,(char)0,(char)1,(char)0,
    (char)1,(char)0,(char)64,(char)31,(char)0,(char)0,(char)-128,(char)62,(char)0,(char)0,(char)2,
    (char)0,(char)16,(char)0,(char)100,(char)97,(char)116,(char)97,(char)header40,(char)header41,(char)header42,(char)header43};
 */

  header[0]=(char)82;
  header[1]=(char)73;
  header[2]=(char)70;
  header[3]=(char)70;
  header[4]=(char)header4;
  header[5]=(char)header5;
  header[6]=(char)header6;
  header[7]=(char)header7;
  header[8]=(char)87;
  header[9]=(char)65;
  header[10]=(char)86;
  header[11]=(char)69;
  header[12]=(char)102;
  header[13]=(char)109;
  header[14]=(char)116;
  header[15]=(char)32;
  header[16]=(char)16;
  header[17]=(char)0;
  header[18]=(char)0;
  header[19]=(char)0;
  header[20]=(char)1;
  header[21]=(char)0;
  header[22]=(char)1;
  header[23]=(char)0;
  header[24]=(char)64;
  header[25]=(char)31;
  header[26]=(char)0;
  header[27]=(char)0;
  header[28]=(char)-128;
  header[29]=(char)62;
  header[30]=(char)0;
  header[31]=(char)0;
  header[32]=(char)2;
  header[33]=(char)0;
  header[34]=(char)16;
  header[35]=(char)0;
  header[36]=(char)100;
  header[37]=(char)97;
  header[38]=(char)116;
  header[39]=(char)97;
  header[40]=(char)header40;
  header[41]=(char)header41;
  header[42]=(char)header42;
  header[43]=(char)header43;

  /*get the size of the decoding file. Attention: make sure the previous file stream has been closed.*/
  pFile = fopen("test.out","rb");
  if (pFile == NULL)
  {
  printf("open files when trying to get file size failed\n");
  }
  else
  {
    fseek(pFile,0,SEEK_END);
    size = ftell(pFile);    /*the size of the decoding file in Bytes*/
    fclose(pFile);
  }

  fullsize = size + 44;   /*finally the size of the .wav size should be 44 bytes bigger than the decoding file.*/
  f_size = fullsize - 8;    /*Bytes at offset 04H*/
  b_size = fullsize - 44;   /*Bytes at offset 28H*/
  Dec2Hex(f_size,f_sizeHex);    /*get the f_size of the .wav in Hex.*/
  Dec2Hex(b_size,b_sizeHex);    /*get the b_size of the .wav in Hex.*/


  /*Get the most confusing 8 Bytes in the 44 Bytes according to the file size*/
  header4 = Convert(f_sizeHex[6], f_sizeHex[7]);
  header5 = Convert(f_sizeHex[4], f_sizeHex[5]);
  header6 = Convert(f_sizeHex[2], f_sizeHex[3]);
  header7 = Convert(f_sizeHex[0], f_sizeHex[1]);
  header40 = Convert(b_sizeHex[6], b_sizeHex[7]);
  header41 = Convert(b_sizeHex[4], b_sizeHex[5]);
  header42 = Convert(b_sizeHex[2], b_sizeHex[3]);
  header43 = Convert(b_sizeHex[0], b_sizeHex[1]);
  header[4] = (char)header4;
  header[5] = (char)header5;
  header[6] = (char)header6;
  header[7] = (char)header7;
  header[40] = (char)header40;
  header[41] = (char)header41;
  header[42] = (char)header42;
  header[43] = (char)header43;

/*Here starts the .wav sound file formating*/
  pFile = fopen("out.wav","wb+");
  if(pFile == NULL)
  {
    printf("opening out.wav failed.\n");
    return;
  }
  len = fwrite(header,1,44,pFile);  /*copy the .wav sound file header(44 Bytes)*/

  
  /*The following progress is about copying the decoding data to the output file*/
  qFile = fopen("test.out","rb");
  len = fread(buffer,1,1024,qFile);
  while(len != 0)
  {
    len = fwrite(buffer,1,1024,pFile);  /*This assignment to the variable 'len' is not a must.*/
    len = fread(buffer,1,1024,qFile);
    i++;
  }

  fclose(pFile);
  fclose(qFile);
}

void Dec2Hex(long size, char result[])    /*convert a decimal number to a hex number as a char array, 4 Bytes(8 elements) maximum.*/
{
  int a[8]={0,0,0,0,0,0,0,0}, remainder, i = 0, j = 0;
  char hex[16] = {'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
  while(size > 0)
  {
    remainder = size % 16;
    a[i++] = remainder;
    size = size / 16;
  }
  for (; j < 8; j++)
  {
    result[j] = hex[a[7 - j]];
  }
  return;
}
int Hex2Dec(char a)   /*conver a single hex char to a binary. eg: Hex('E') = 14*/
{
  int result;
  switch(a) {
  case '0': result = 0; break;
  case '1': result = 1; break;
  case '2': result = 2; break;
  case '3': result = 3; break;
  case '4': result = 4; break;
  case '5': result = 5; break;
  case '6': result = 6; break;
  case '7': result = 7; break;
  case '8': result = 8; break;
  case '9': result = 9; break;
  case 'A': result = 10; break;
  case 'B': result = 11; break;
  case 'C': result = 12; break;
  case 'D': result = 13; break;
  case 'E': result = 14; break;
  case 'F': result = 15; break;
  }
  return result;
}
int Convert(char a, char b)   /*combining 2 hex char and conver to a decimal. eg:Conver('7','D')=125*/
{
  int a_dec, b_dec;
  a_dec = Hex2Dec(a);
  b_dec = Hex2Dec(b);
  return a_dec * 16 + b_dec;
}


int ALSA_ini()    /*hardware initialization and data setting. Composed by XRQ*/
{
  int rc;
  int size;
/*  snd_pcm_t *handle;*/
  snd_pcm_hw_params_t *params;
  unsigned int val;
  int dir;
  snd_pcm_uframes_t frames;

  /* Display version information */
  printf("ALSA library version: %s\n", SND_LIB_VERSION_STR);

  /* Open PCM device for playback. */
  rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
  if (rc < 0) {
    printf("unable to open pcm device: %s\n",snd_strerror(rc));
    exit(1);
  }

  /* Allocate a hardware parameters object. */
  snd_pcm_hw_params_alloca(&params);

  /* Fill it in with default values. */
  snd_pcm_hw_params_any(handle, params);

  /* Set the desired hardware parameters. */
  /* Interleaved mode */
  snd_pcm_hw_params_set_access(handle, params,SND_PCM_ACCESS_RW_INTERLEAVED);

  /* Signed 16-bit little-endian format */
  snd_pcm_hw_params_set_format(handle, params,SND_PCM_FORMAT_S16_LE);

  /* One channel (mono) */
  snd_pcm_hw_params_set_channels(handle, params, 1);
    
  /* 8000 bits/second sampling rate. phone quality */
  val = 8000;
  snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir);

  /* Set period size to 32 frames. */
  frames = 160;

  snd_pcm_hw_params_set_period_size_near(handle,params, &frames, &dir);

  /* Write the parameters to the driver */
  rc = snd_pcm_hw_params(handle, params);
  if (rc < 0) {
    printf("unable to set hw parameters: %s\n",snd_strerror(rc));
    exit(1);
  }

  /* Display current PCM interface information. */
  printf("PCM handle name = '%s\n'", snd_pcm_name(handle));
  
  printf("PCM state = %s\n", snd_pcm_state_name(snd_pcm_state(handle)));

  snd_pcm_hw_params_get_access(params, (snd_pcm_access_t *) &val);
  printf("Access type = %s\n", snd_pcm_access_name((snd_pcm_access_t)val));
/*
  snd_pcm_hw_params_get_format(params, &val);
  printf("format = '%s' (%s)\n", snd_pcm_format_name((snd_pcm_format_t)val), snd_pcm_format_description((snd_pcm_format_t)val));
*/
  snd_pcm_hw_params_get_subformat(params, (snd_pcm_subformat_t *)&val);
  printf("subformat = '%s' (%s)\n", snd_pcm_subformat_name((snd_pcm_subformat_t)val), snd_pcm_subformat_description((snd_pcm_subformat_t)val));

  snd_pcm_hw_params_get_channels(params, &val);
  printf("channels = %d\n", val);

  snd_pcm_hw_params_get_rate(params, &val, &dir);
  printf("rate = %d bps\n", val);

   
  snd_pcm_hw_params_get_period_size(params, &frames,&dir);
  printf("The value of variable frames = %d\n", frames);    
  snd_pcm_hw_params_get_period_time(params, &val, &dir);
    
  printf("All the initializations have been done!\n");

  return 1;
}
