/*
*****************************************************************************
*
*      GSM AMR-NB speech codec   R98   Version 7.6.0   December 12, 2001
*                                R99   Version 3.3.0                
*                                REL-4 Version 4.1.0                
*
*****************************************************************************
*
*      File             : sp_enc.c
*      Purpose          : Pre filtering and encoding of one speech frame.
*
*****************************************************************************
*/
 
/*
*****************************************************************************
*                         MODULE INCLUDE FILE AND VERSION ID
*****************************************************************************
*/
#include "sp_enc.h"
const char sp_enc_id[] = "@(#)$Id $" sp_enc_h;
 
/*
*****************************************************************************
*                         INCLUDE FILES
*****************************************************************************
*/
#include <stdlib.h>
#include <stdio.h>
#include "typedef.h"
#include "basic_op.h"
#include "cnst.h"
#include "count.h"
#include "set_zero.h"
#include "pre_proc.h"
#include "prm2bits.h"
#include "mode.h"
#include "cod_amr.h"

/*
*****************************************************************************
*                         LOCAL VARIABLES AND TABLES
*****************************************************************************
*/
/*---------------------------------------------------------------*
 *    Constants (defined in "cnst.h")                            *
 *---------------------------------------------------------------*
 * L_FRAME     :
 * M           :
 * PRM_SIZE    :
 * AZ_SIZE     :
 * SERIAL_SIZE :
 *---------------------------------------------------------------*/

/*
*****************************************************************************
*                         PUBLIC PROGRAM CODE
*****************************************************************************
*/
 
/*************************************************************************
*
*  Function:   Speech_Encode_Frame_init
*  Purpose:    Allocates memory for filter structure and initializes
*              state memory
*
**************************************************************************
*/
int Speech_Encode_Frame_init (Speech_Encode_FrameState **state,
                              Flag dtx,
                              char *id)
{
  Speech_Encode_FrameState* s;
 
  if (state == (Speech_Encode_FrameState **) NULL){
      fprintf(stderr, "Speech_Encode_Frame_init: invalid parameter\n");
      return -1;
  }
  *state = NULL;
 
  /* allocate memory */
  if ((s= (Speech_Encode_FrameState *) malloc(sizeof(Speech_Encode_FrameState))) == NULL){
      fprintf(stderr, "Speech_Encode_Frame_init: can not malloc state "
                      "structure\n");
      return -1;
  }

  s->complexityCounter = getCounterId(id);

  s->pre_state = NULL;
  s->cod_amr_state = NULL;
  s->dtx = dtx;

  if (Pre_Process_init(&s->pre_state) ||
      cod_amr_init(&s->cod_amr_state, s->dtx)) {
      Speech_Encode_Frame_exit(&s);
      return -1;
  }

  Speech_Encode_Frame_reset(s);
  *state = s;
  
  return 0;
}
 
/*************************************************************************
*
*  Function:   Speech_Encode_Frame_reset
*  Purpose:    Resetses state memory
*
**************************************************************************
*/
int Speech_Encode_Frame_reset (Speech_Encode_FrameState *state)
{
  if (state == (Speech_Encode_FrameState *) NULL){
      fprintf(stderr, "Speech_Encode_Frame_reset: invalid parameter\n");
      return -1;
  }
  
  Pre_Process_reset(state->pre_state);
  cod_amr_reset(state->cod_amr_state);

  setCounter(state->complexityCounter);
  Init_WMOPS_counter();
  setCounter(0); /* set counter to global counter */

  return 0;
}
 
/*************************************************************************
*
*  Function:   Speech_Encode_Frame_exit
*  Purpose:    The memory used for state memory is freed
*
**************************************************************************
*/
void Speech_Encode_Frame_exit (Speech_Encode_FrameState **state)
{
  if (state == NULL || *state == NULL)
      return;
 
  Pre_Process_exit(&(*state)->pre_state);
  cod_amr_exit(&(*state)->cod_amr_state);

  setCounter((*state)->complexityCounter);
  WMOPS_output(0);
  setCounter(0); /* set counter to global counter */
 
  /* deallocate memory */
  free(*state);
  *state = NULL;
  
  return;
}
 
 
int Speech_Encode_Frame_First (
    Speech_Encode_FrameState *st,  /* i/o : post filter states       */
    Word16 *new_speech)            /* i   : speech input             */
{
#if !defined(NO13BIT)
   Word16 i;
#endif

   setCounter(st->complexityCounter);

#if !defined(NO13BIT)
  /* Delete the 3 LSBs (13-bit input) */
  for (i = 0; i < L_NEXT; i++) 
  {
     new_speech[i] = new_speech[i] & 0xfff8;    move16 (); logic16 ();
  }
#endif

  /* filter + downscaling */
  Pre_Process (st->pre_state, new_speech, L_NEXT);

  cod_amr_first(st->cod_amr_state, new_speech);

  Init_WMOPS_counter (); /* reset WMOPS counter for the new frame */

  return 0;
}

int Speech_Encode_Frame (
    Speech_Encode_FrameState *st, /* i/o : post filter states          */
    enum Mode mode,               /* i   : speech coder mode           */
    Word16 *new_speech,           /* i   : speech input                */
    Word16 *serial,               /* o   : serial bit stream           */
    enum Mode *usedMode           /* o   : used speech coder mode */
    )
{
  Word16 prm[MAX_PRM_SIZE];   /* Analysis parameters.                  */
  Word16 syn[L_FRAME];        /* Buffer for synthesis speech           */
  Word16 i;

  setCounter(st->complexityCounter);
  Reset_WMOPS_counter (); /* reset WMOPS counter for the new frame */

  /* initialize the serial output frame to zero */
  for (i = 0; i < MAX_SERIAL_SIZE; i++)   
  {
    serial[i] = 0;                                           move16 ();
  }

#if !defined(NO13BIT)
  /* Delete the 3 LSBs (13-bit input) */
  for (i = 0; i < L_FRAME; i++)   
  {
     new_speech[i] = new_speech[i] & 0xfff8;    move16 (); logic16 ();
  }
#endif

  /* filter + downscaling */
  Pre_Process (st->pre_state, new_speech, L_FRAME);           
  
  /* Call the speech encoder */
  cod_amr(st->cod_amr_state, mode, new_speech, prm, usedMode, syn);
  
  /* Parameters to serial bits */
  Prm2bits (*usedMode, prm, &serial[0]); 

  fwc();
  setCounter(0); /* set counter to global counter */

  return 0;
}
