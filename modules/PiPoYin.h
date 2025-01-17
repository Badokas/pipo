/**
 * @file PiPoYin.h
 * @author Diemo.Schwarz@ircam.fr
 *
 * @brief PiPo fundamental frequency estimation after de Cheveigne and Kawahara's yin algorithm
 * Estimates fundamental frequency and outputs energy, periodicity factor, and auto correlation coefficients.
 *
 * @ingroup pipomodules
 *
 * @copyright
 * Copyright (C) 2013-2014 by IRCAM – Centre Pompidou, Paris, France.
 * All rights reserved.
 *
 * License (BSD 3-clause)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _PIPO_YIN_
#define _PIPO_YIN_

#include "PiPo.h"

extern "C" {
#include "rta_yin.h"
#include <math.h>
}

#include <algorithm>
#include <cstdlib>

#define PIPO_YIN_DEBUG 1

static const unsigned int yin_max_mins = 128;

class PiPoYin : public PiPo
{
private:
  rta_yin_setup_t *yin_setup;
  float		  *buffer_;	// downsampled input window
  double	   sr_;		// effective sample rate
  int		   ac_size_;
  float		  *corr_;
  
public:
  PiPoScalarAttr<double>	minFreq;
  PiPoScalarAttr<PiPo::Enumerate> downSampling;
  PiPoScalarAttr<double>	yinThreshold;
  
  // constructor
  PiPoYin (Parent *parent, PiPo *receiver = NULL)
  : PiPo(parent, receiver),
  minFreq(this, "minfreq", "Minimum Frequency", true, 24.0),  // just ok for 2048 sample slices
  downSampling(this, "downsampling", "Downsampling Exponent", true, 2),
  yinThreshold(this, "threshold", "Yin Periodicity Threshold", true, 0.68),
  buffer_(NULL), corr_(NULL)
  {
    rta_yin_setup_new(&yin_setup, yin_max_mins);
    
    this->downSampling.addEnumItem("none", "No down sampling");
    this->downSampling.addEnumItem("2x", "Down sampling by 2");
    this->downSampling.addEnumItem("4x", "Down sampling by 4");
    this->downSampling.addEnumItem("8x", "Down sampling by 8");
  }
  
  ~PiPoYin (void)
  {
    rta_yin_setup_delete(yin_setup);
    free(buffer_);
    free(corr_);
  }
  
  int streamAttributes (bool hasTimeTags, double rate, double offset, unsigned int width, unsigned int height, const char **labels, bool hasVarSize, double domain, unsigned int maxFrames)
  {
#if PIPO_YIN_DEBUG
    printf("PiPoYin %p streamAttributes timetags %d  rate %f  offset %f  width %d  height %d  labels %s  varsize %d  domain %f  maxframes %d\n",
           this, hasTimeTags, rate, offset, width, height, labels ? labels[0] : "n/a", hasVarSize, domain, maxFrames);
#endif
    
    if (domain == 0)
    { // error: frames must have a duration
      signalError(std::string("input stream domain is zero"));
      return -1;
    }
    
    // we expect sliced input, so rate is the frame rate and the sampling rate is each row's duration
    double sampleRate = (double) height / domain;
    double down = 1 << std::max<int>(0, downSampling.get());	// downsampling factor
    int    downsize = height / down;		// downsampled input frame size
    sr_   = sampleRate / down;			// effective sample rate
    ac_size_ = (int) ceil(sr_ / minFreq.get()) + 2;
    
    /* check size */
    if (downsize > ac_size_)
    {
      buffer_ = (float *) realloc(buffer_, downsize * sizeof(float));
      corr_   = (float *) realloc(corr_,   ac_size_ * sizeof(float));
      
      const char *yinColNames[4];
      yinColNames[0] = "Frequency";
      yinColNames[1] = "Energy";
      yinColNames[2] = "Periodicity";
      yinColNames[3] = "AC1";
      
      return this->propagateStreamAttributes(hasTimeTags, rate, offset, 4, 1, yinColNames, 0, 0.0, 1);
    }
    else
    { // error: input frame size too small for minfreq
      signalError("input frame size too small for given minfreq");
      return -1;
    }
  }
  
  int reset (void)
  {
    return this->propagateReset();
  }
  
  // mean-based downsampling
  int downsample (float *in, int size, float *out, int downsamplingexp)
  {
    int downVectorSize = size >> downsamplingexp;
    int i, j;
    
    if (downVectorSize > 0)
    {
      switch(downsamplingexp)
      {
        case 3:
        {
          for (i = 0, j = 0; i < downVectorSize; i++, j += 8)
            out[i] = 0.125 * (in[j] + in[j + 1] + in[j + 2] + in[j + 3] + in[j + 4] + in[j + 5] + in[j + 6] + in[j + 7]);
        }
          break;
          
        case 2:
        {
          for (i = 0, j = 0; i < downVectorSize; i++, j += 4)
            out[i] = 0.25 * (in[j] + in[j + 1] + in[j + 2] + in[j + 3]);
        }
          break;
          
        case 1:
        {
          for (i = 0, j = 0; i < downVectorSize; i++, j += 2)
            out[i] = 0.5 * (in[j] + in[j + 1]);
        }
          break;
          
        default:
          break;
      }
      
      return downVectorSize;
    }
    else
    {
      float sum = 0.0;
      
      for (i = 0; i < size; i++)
        sum += in[i];
      
      out[0] = sum / (float) size;
      
      return 1;
    }
  }
  
  int frames (double time, double weight, float *values, unsigned int size, unsigned int num)
  {
    float min;
    float period;
    float ac1_over_ac0; /* autocorrelation[1] / autocorrelation[0] */
    float periodicity; /* 1.0 - sqrt(min) */
    float energy; /* sqrt(autocorrelation[0]/ (size - ac_size)) */
    float outvalues[4];
    
    if (buffer_ == NULL)
      return -1;
    
    int downsize = downsample(values, size, buffer_, std::max<int>(0, downSampling.get()));
    
    if (downsize <= ac_size_)
    { // error: input frame size too small for minfreq
      signalError("input frame size too small for given minfreq");
      return -1;
    }
    
    period = rta_yin(&min, corr_, ac_size_, buffer_, downsize, yin_setup, yinThreshold.get());
    
    if (corr_[0] != 0.0)
      ac1_over_ac0 = corr_[1] / corr_[0];
    else
      ac1_over_ac0 = 0.0;
    
    if (min > 0.0)
      periodicity = (min < 1.) ? 1.0 - sqrt(min) : 0.0;
    else
      periodicity = 1.0;
    
    energy = sqrt(corr_[0] / (downsize - ac_size_));
    
    outvalues[0] = (float) sr_ / period;
    outvalues[1] = (float) energy;
    outvalues[2] = (float) periodicity;
    outvalues[3] = (float) ac1_over_ac0;
    
    return propagateFrames(time, 1.0, outvalues, 4, 1);
  }
};

#endif
