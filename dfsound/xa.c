/***************************************************************************
                            xa.c  -  description
                             -------------------
    begin                : Wed May 15 2002
    copyright            : (C) 2002 by Pete Bernert
    email                : BlackDove@addcom.de
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

#include "stdafx.h"
#define _IN_XA
#include <stdint.h>

// will be included from spu.c
#ifdef _IN_SPU
#define DBG_SPU1	7
#define DBG_SPU2	8
#define DBG_SPU3	9

#define CTRL_CD_PLAY					0x01
#define CTRL_CD_REVERB					0x02

////////////////////////////////////////////////////////////////////////
// XA GLOBALS
////////////////////////////////////////////////////////////////////////

static int gauss_ptr = 0;
static int gauss_window[8] = {0, 0, 0, 0, 0, 0, 0, 0};

#define gvall0 gauss_window[gauss_ptr]
#define gvall(x) gauss_window[(gauss_ptr+x)&3]
#define gvalr0 gauss_window[4+gauss_ptr]
#define gvalr(x) gauss_window[4+((gauss_ptr+x)&3)]

////////////////////////////////////////////////////////////////////////
// MIX XA & CDDA
////////////////////////////////////////////////////////////////////////

static int lastcd_lc, lastcd_rc;
#define ssat32_to_16(v) { \
  if (v < -32768) v = -32768; \
  else if (v > 32767) v = 32767; \
}

#ifdef SHOW_DEBUG
extern char txtbuffer[1024];
#endif // DISP_DEBUG

INLINE void MixXA(int *SSumLR, int ns_to, int decode_pos)
{
 int cursor = decode_pos;
 int ns;
 short l, r;
 uint32_t v = spu.XALastVal;

 if(spu.XAPlay != spu.XAFeed || spu.XARepeat > 0)
 {
  if(spu.XAPlay == spu.XAFeed)
   spu.XARepeat--;

  for(ns = 0; ns < ns_to*2; )
   {
    if(spu.XAPlay != spu.XAFeed) v=*spu.XAPlay++;
    if(spu.XAPlay == spu.XAEnd) spu.XAPlay=spu.XAStart;

    l = ((int)(short)v * spu.iLeftXAVol) >> 15;
    r = ((int)(short)(v >> 16) * spu.iRightXAVol) >> 15;
    SSumLR[ns++] += l;
    SSumLR[ns++] += r;

    spu.spuMem[cursor] = v;
    spu.spuMem[cursor + 0x400/2] = v >> 16;
    cursor = (cursor + 1) & 0x1ff;
   }
  spu.XALastVal = v;
 }

 cursor = decode_pos;
 if(spu.CDDAPlay != spu.CDDAFeed || spu.CDDARepeat > 0)
 {
   if (spu.CDDAPlay == spu.CDDAFeed)
     spu.CDDARepeat--;
   v = spu.CDDALastVal;
  for(ns = 0; ns < ns_to*2; )
   {
    if(spu.CDDAPlay != spu.CDDAFeed) v=*spu.CDDAPlay++;
    if(spu.CDDAPlay == spu.CDDAEnd) spu.CDDAPlay=spu.CDDAStart;

    l = ((int)(short)v * spu.iLeftXAVol) >> 15;
    r = ((int)(short)(v >> 16) * spu.iRightXAVol) >> 15;
    SSumLR[ns++] += l;
    SSumLR[ns++] += r;

    spu.spuMem[cursor] = v;
    spu.spuMem[cursor + 0x400/2] = v >> 16;
    cursor = (cursor + 1) & 0x1ff;
   }
  spu.CDDALastVal = v;
 }
}

////////////////////////////////////////////////////////////////////////
// small linux time helper... only used for watchdog
////////////////////////////////////////////////////////////////////////

static unsigned long timeGetTime_spu()
{
#if defined(NO_OS)
 return 0;
#elif defined(_WIN32)
 return GetTickCount();
#else
 struct timeval tv;
 gettimeofday(&tv, 0);                                 // well, maybe there are better ways
 return tv.tv_sec * 1000 + tv.tv_usec/1000;            // to do that, but at least it works
#endif
}

////////////////////////////////////////////////////////////////////////
// FEED XA
////////////////////////////////////////////////////////////////////////
#ifdef DISP_DEBUG
static int tmpIdx = 0;
#endif // DISP_DEBUG

INLINE void FeedXA(xa_decode_t *xap)
{
 int sinc,spos,i,iSize,iPlace,vl,vr;

 if(!spu.bSPUIsOpen) return;

 spu.xapGlobal = xap;                                  // store info for save states
 spu.XARepeat  = 3;                                    // set up repeat

#if 0//def XA_HACK
 iSize=((45500*xap->nsamples)/xap->freq);              // get size
#else
 //iSize=((48000*xap->nsamples)/xap->freq);              // get size
 iSize = xap->newSize;

#endif
 if(!iSize) return;                                    // none? bye

 if(spu.XAFeed<spu.XAPlay) iPlace=spu.XAPlay-spu.XAFeed; // how much space in my buf?
 else              iPlace=(spu.XAEnd-spu.XAFeed) + (spu.XAPlay-spu.XAStart);

 if(iPlace==0) return;                                 // no place at all
 #ifdef DISP_DEBUG
 //PRINT_LOG3("%d SPUplayADPCMchannel %d==%d=",tmpIdx++, iSize, xap->freq);
 #endif // DISP_DEBUG

 //----------------------------------------------------//
 /*if(spu_config.iXAPitch)                               // pitch change option?
  {
   static DWORD dwLT=0;
   static DWORD dwFPS=0;
   static int   iFPSCnt=0;
   static int   iLastSize=0;
   static DWORD dwL1=0;
   DWORD dw=timeGetTime_spu(),dw1,dw2;
   iPlace=iSize;
   dwFPS+=dw-dwLT;iFPSCnt++;
   dwLT=dw;
   if(iFPSCnt>=10)
    {
     if(!dwFPS) dwFPS=1;
     dw1=1000000/dwFPS;
     if(dw1>=(dwL1-100) && dw1<=(dwL1+100)) dw1=dwL1;
     else dwL1=dw1;
     dw2=(xap->freq*100/xap->nsamples);
     if((!dw1)||((dw2+100)>=dw1)) iLastSize=0;
     else
      {
       iLastSize=iSize*dw2/dw1;
       if(iLastSize>iPlace) iLastSize=iPlace;
       iSize=iLastSize;
      }
     iFPSCnt=0;dwFPS=0;
    }
   else
    {
     if(iLastSize) iSize=iLastSize;
    }
  }*/
 //----------------------------------------------------//

 spos=0x10000L;
 //sinc = (xap->nsamples << 16) / iSize;                 // calc freq by num / size
 #ifdef DISP_DEBUG
 //PRINT_LOG2("SPU FeedXA %d=%d", (xap->nsamples << 16) / iSize, xap->sinc);
 #endif // DISP_DEBUG
 sinc = xap->sinc;                 // calc freq by num / size

 if(xap->stereo)
{
   uint32_t * pS=(uint32_t *)xap->pcm;
   uint32_t l=0;

   /*if(spu_config.iXAPitch)
    {
     int32_t l1,l2;short s;
     for(i=0;i<iSize;i++)
      {
       if(spu_config.iUseInterpolation==2)
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           gauss_window[gauss_ptr] = (short)LOWORD(l);
           gauss_window[4+gauss_ptr] = (short)HIWORD(l);
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0) >> 15;
         vr+=(gauss[vl+1]*gvall(1)) >> 15;
         vr+=(gauss[vl+2]*gvall(2)) >> 15;
         vr+=(gauss[vl+3]*gvall(3)) >> 15;
         l= vr & 0xffff;
         vr=(gauss[vl]*gvalr0) >> 15;
         vr+=(gauss[vl+1]*gvalr(1)) >> 15;
         vr+=(gauss[vl+2]*gvalr(2)) >> 15;
         vr+=(gauss[vl+3]*gvalr(3)) >> 15;
         l |= vr << 16;
        }
       else
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           spos -= 0x10000L;
          }
        }
       s=(short)LOWORD(l);
       l1=s;
       l1=(l1*iPlace)/iSize;
       ssat32_to_16(l1);
       s=(short)HIWORD(l);
       l2=s;
       l2=(l2*iPlace)/iSize;
       ssat32_to_16(l2);
       l=(l1&0xffff)|(l2<<16);
       *spu.XAFeed++=l;
       if(spu.XAFeed==spu.XAEnd) spu.XAFeed=spu.XAStart;
       if(spu.XAFeed==spu.XAPlay)
        {
         if(spu.XAPlay!=spu.XAStart) spu.XAFeed=spu.XAPlay-1;
         break;
        }
       spos += sinc;
      }
    }
   else*/
    {
     for(i=0;i<iSize;i++)
      {
       /*if(spu_config.iUseInterpolation==2)
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           gauss_window[gauss_ptr] = (short)LOWORD(l);
           gauss_window[4+gauss_ptr] = (short)HIWORD(l);
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0) >> 15;
         vr+=(gauss[vl+1]*gvall(1)) >> 15;
         vr+=(gauss[vl+2]*gvall(2)) >> 15;
         vr+=(gauss[vl+3]*gvall(3)) >> 15;
         l= vr & 0xffff;
         vr=(gauss[vl]*gvalr0) >> 15;
         vr+=(gauss[vl+1]*gvalr(1)) >> 15;
         vr+=(gauss[vl+2]*gvalr(2)) >> 15;
         vr+=(gauss[vl+3]*gvalr(3)) >> 15;
         l |= vr << 16;
        }
       else*/
        {
         while(spos>=0x10000L)
          {
           l = *pS++;
           spos -= 0x10000L;
          }
        }

       *spu.XAFeed++=l;

       if(spu.XAFeed==spu.XAEnd) spu.XAFeed=spu.XAStart;
       if(spu.XAFeed==spu.XAPlay)
        {
         if(spu.XAPlay!=spu.XAStart) spu.XAFeed=spu.XAPlay-1;
         #ifdef SHOW_DEBUG
         DEBUG_print("FeedXA Buffer not enough", DBG_SPU2);
         #endif // DISP_DEBUG
         break;
        }

       spos += sinc;
      }
    }
  }
 else
  {
   unsigned short * pS=(unsigned short *)xap->pcm;
   uint32_t l;short s=0;

   /*if(spu_config.iXAPitch)
    {
     int32_t l1;
     for(i=0;i<iSize;i++)
      {
       if(spu_config.iUseInterpolation==2)
        {
         while(spos>=0x10000L)
          {
           gauss_window[gauss_ptr] = (short)*pS++;
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0) >> 15;
         vr+=(gauss[vl+1]*gvall(1)) >> 15;
         vr+=(gauss[vl+2]*gvall(2)) >> 15;
         vr+=(gauss[vl+3]*gvall(3)) >> 15;
         l1=s= vr;
         l1 &= 0xffff;
        }
       else
        {
         while(spos>=0x10000L)
          {
           s = *pS++;
           spos -= 0x10000L;
          }
         l1=s;
        }
       l1=(l1*iPlace)/iSize;
       ssat32_to_16(l1);
       l=(l1&0xffff)|(l1<<16);
       *spu.XAFeed++=l;
       if(spu.XAFeed==spu.XAEnd) spu.XAFeed=spu.XAStart;
       if(spu.XAFeed==spu.XAPlay)
        {
         if(spu.XAPlay!=spu.XAStart) spu.XAFeed=spu.XAPlay-1;
         break;
        }
       spos += sinc;
      }
    }
   else*/
    {
     for(i=0;i<iSize;i++)
      {
       /*if(spu_config.iUseInterpolation==2)
        {
         while(spos>=0x10000L)
          {
           gauss_window[gauss_ptr] = (short)*pS++;
           gauss_ptr = (gauss_ptr+1) & 3;
           spos -= 0x10000L;
          }
         vl = (spos >> 6) & ~3;
         vr=(gauss[vl]*gvall0) >> 15;
         vr+=(gauss[vl+1]*gvall(1)) >> 15;
         vr+=(gauss[vl+2]*gvall(2)) >> 15;
         vr+=(gauss[vl+3]*gvall(3)) >> 15;
         l=s= vr;
        }
       else*/
        {
         while(spos>=0x10000L)
          {
           s = *pS++;
           spos -= 0x10000L;
          }
         l=s;
        }

       l &= 0xffff;
       *spu.XAFeed++=(l|(l<<16));

       if(spu.XAFeed==spu.XAEnd) spu.XAFeed=spu.XAStart;
       if(spu.XAFeed==spu.XAPlay)
        {
         if(spu.XAPlay!=spu.XAStart) spu.XAFeed=spu.XAPlay-1;
         #ifdef SHOW_DEBUG
         DEBUG_print("FeedXA Buffer not enough", DBG_SPU2);
         #endif // DISP_DEBUG
         break;
        }

       spos += sinc;
      }
    }
  }
}

#define PS_SPU_FREQ	48000
#define SINC (((u32)1 << 16) * 44100 / (PS_SPU_FREQ))
////////////////////////////////////////////////////////////////////////
// FEED CDDA
////////////////////////////////////////////////////////////////////////

INLINE int FeedCDDA(unsigned char *pcm, int nBytes)
{
 int space;
 space=(spu.CDDAPlay-spu.CDDAFeed-1)*4 & (CDDA_BUFFER_SIZE - 1);
 if(space<nBytes)
 {
     #ifdef SHOW_DEBUG
     //sprintf(txtbuffer, "FeedCDDA 0x7761 %ld %ld", space, nBytes);
     //DEBUG_print(txtbuffer, DBG_SPU2);
     #endif // DISP_DEBUG
     return 0x7761; // rearmed_wait
 }
 spu.CDDARepeat  = 3;
 while(nBytes>0)
  {
   if(spu.CDDAFeed==spu.CDDAEnd) spu.CDDAFeed=spu.CDDAStart;
   space=(spu.CDDAPlay-spu.CDDAFeed-1)*4 & (CDDA_BUFFER_SIZE - 1);
   if(spu.CDDAFeed+space/4>spu.CDDAEnd)
    space=(spu.CDDAEnd-spu.CDDAFeed)*4;
   if(space>nBytes)
    space=nBytes;

   memcpy(spu.CDDAFeed,pcm,space);
   spu.CDDAFeed+=space/4;
   nBytes-=space;
   pcm+=space;
  }

    /*int spos = 0x10000L;
    uint32_t *pS = (uint32_t *)pcm;
    uint32_t l = 0;
    int iSize = (PS_SPU_FREQ * (nBytes >> 2)) / 44100;
    int i;
    for (i = 0; i < iSize; i++)
    {
        while (spos >= 0x10000L)
        {
            l = *pS++;
            spos -= 0x10000L;
        }

        *spu.CDDAFeed++ = l;

        if (spu.CDDAFeed == spu.CDDAEnd) spu.CDDAFeed = spu.CDDAStart;

        while (spu.CDDAFeed == spu.CDDAPlay-1
               || (spu.CDDAFeed == spu.CDDAEnd - 1 && spu.CDDAPlay == spu.CDDAStart))
        {
            usleep(1000);
        }

        spos += SINC;
    }*/

 return 0x676f; // rearmed_go
}

#endif
