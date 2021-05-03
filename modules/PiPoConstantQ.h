/** -*- mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * @file PiPoConstantQ.h
 * @author Diemo Schwarz, Tristan Carsault
 * 
 * @brief Constant Q Transform PiPo using Essentia's implementation
 * 
 * @ingroup pipomodules
 *
 * @copyright
 * Copyright (C) 2012-2021 by IRCAM – Centre Pompidou, Paris, France.
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

#ifndef _PIPO_CQT_
#define _PIPO_CQT_

#define MOCKUP 1 /////////

#include <vector>
#include "PiPo.h"
#include "PiPoSlice.h"
#include "constantq.h" // from essentia

#ifdef __cplusplus
extern "C" {
#endif

//#include <float.h>
#include <math.h>

#ifdef __cplusplus
}
#endif


// calculate constant Q transform on a slice of audio input
class PiPoCQT : public PiPo
{
public:
  enum OutputMode { ComplexFft, MagnitudeFft, PowerFft, LogPowerFft, NumOutputMode };

private:
  essentia::standard::ConstantQ *constantq_; // essentia class
  double input_samplerate_;
  enum OutputMode output_mode_;
  int num_bins_;

public:
  PiPoScalarAttr<PiPo::Enumerate> mode_attr_;
  //PiPoScalarAttr<bool>            norm_attr_;

  // essentia constantQ parameters
  PiPoScalarAttr<double> minFrequency_attr_;
  PiPoScalarAttr<int>    numberBins_attr_; 
  PiPoScalarAttr<int>    binsPerOctave_attr_;
//  PiPoScalarAttr<double> sampleRate_attr_; 
  PiPoScalarAttr<double> threshold_attr_; 
  PiPoScalarAttr<double> scale_attr_; 
  PiPoScalarAttr<PiPo::Enumerate> windowType_attr_; 
  PiPoScalarAttr<double> minimumKernelSize_attr_; 
  PiPoScalarAttr<bool>   zeroPhase_attr_;

public:
  PiPoCQT(Parent *parent, PiPo *receiver = NULL)
  : PiPo(parent), constantq_(NULL), input_samplerate_(0),
    num_bins_(0), output_mode_(PowerFft),
    mode_attr_(this, "mode", "FFT Mode", true, output_mode_),  
    minFrequency_attr_     (this, "minFrequency",      "minimum frequency [Hz]", !false, 32.7),
    numberBins_attr_       (this, "numberBins",        "number of frequency bins, starting at minFrequency", true, 84),
    binsPerOctave_attr_    (this, "binsPerOctave",     "number of bins per octave", !false, 12),
///  sampleRate_attr_       (this, "sampleRate",        "FFT sampling rate [Hz]", true, 44100.),
    threshold_attr_        (this, "threshold",         "bins whose magnitude is below this quantile are discarded", !false, 0.01),
    scale_attr_            (this, "scale",             "filters scale. Larger values use longer windows", !false, (double) 1.0),
    minimumKernelSize_attr_(this, "minimumKernelSize", "minimum size allowed for frequency kernels", !false, 4),
    zeroPhase_attr_        (this, "zeroPhase",         "a boolean value that enables zero-phase windowing. Input audio frames should be windowed with the same phase mode", !false, false),
    windowType_attr_       (this, "windowType",        "the window type", !false, 1) // default: hann

  //"{hamming,hann,hannnsgcq,triangular,square,blackmanharris62,blackmanharris70,blackmanharris74,blackmanharris92}", "hann");

    //??? norm_attr_(this, "norm", "Normalize FFT", true, true)
  {
    mode_attr_.addEnumItem("complex", "Complex output");
    mode_attr_.addEnumItem("magnitude", "Magnitude spectrum");
    mode_attr_.addEnumItem("power", "Power spectrum");
    mode_attr_.addEnumItem("logpower", "Logarithmic power spectrum");

    windowType_attr_.addEnumItem("hamming",          "hamming window");
    windowType_attr_.addEnumItem("hann",             "hann window");
    windowType_attr_.addEnumItem("hannnsgcq",        "hannn sg cq????");
    windowType_attr_.addEnumItem("triangular",       "triangular");
    windowType_attr_.addEnumItem("square",           "square");
    windowType_attr_.addEnumItem("blackmanharris62", "blackmanharris62");
    windowType_attr_.addEnumItem("blackmanharris70", "blackmanharris70");
    windowType_attr_.addEnumItem("blackmanharris74", "blackmanharris74");
    windowType_attr_.addEnumItem("blackmanharris92", "blackmanharris92");

    essentia::init(); // init essentia algorithm factory
    constantq_ = new essentia::standard::ConstantQ(); // then we can use an algorithm
    constantq_->declareParameters(); // why do we have to call this explicitly? (but without, no parameter can be set...)
  }
  
  ~PiPoCQT(void)
  {
    essentia::shutdown();
  }
  
  int streamAttributes(bool hasTimeTags, double framerate, double offset, unsigned int width, unsigned int height, const char **labels, bool hasVarSize, double domain, unsigned int maxFrames)
  {  
    //bool		norm		   = norm_attr_.get();
    enum OutputMode     new_output_mode    = (enum OutputMode) mode_attr_.get();
    double              new_samplerate     = (double) height / domain; // retrieve input audio sampling rate
    int			input_size	   = width * height; // number of samples in slice (width is #channels)
    int                 outwidth;
    const char	       *outcolnames[2];

    if (new_output_mode >= NumOutputMode)
      new_output_mode = (enum OutputMode) (NumOutputMode - 1);
    
    switch (new_output_mode)
    {
      case ComplexFft:
      {
        outcolnames[0] = "Real";
        outcolnames[1] = "Imag";
        outwidth = 2;
        break;
      }
        
      case MagnitudeFft:
      {
        outcolnames[0] = "Magnitude";
        outwidth = 1;
        break;
      }
        
      case PowerFft:
      {
        outcolnames[0] = "Power";
        outwidth = 1;
        break;
      }
        
      case LogPowerFft:
      {
        outcolnames[0] = "LogPower";
        outwidth = 1;
        break;
      }
    }

    // set all essentia cqt parameters from pipo attrs
    essentia::ParameterMap params;
    params.add("sampleRate", essentia::Parameter(input_samplerate_ = new_samplerate));
    params.add("numberBins", essentia::Parameter(num_bins_         = numberBins_attr_.get()));
    // minFrequency = minFrequency_attr_.get();
    // ....
    constantq_->setParameters(params);
    constantq_->configure();

    /*todo: check if maybe configure can be skipped for most params??? then attrs can actually set false for changesStream param
    if (new_samplerate != input_samplerate_  ||  num_bins_ != numberBins_attr_.get())
    { // structure-changing parameters have changed, setup cqt
      constantq_.configure();
    }
    */
    
    output_mode_ = new_output_mode;
    
    return propagateStreamAttributes(0, framerate, offset, outwidth, num_bins_, outcolnames, 0, 0.5 * new_samplerate, 1);
  }
  
  int frames (double time, double weight, PiPoValue *values, unsigned int size, unsigned int num)
  {
    // values points to num frames of input audio slice of size total elements,
    
    for (int i = 0; i < num; i++)
    {
      // copy values to or set input vector for cqt
      // ...

      constantq_->compute();

      PiPoValue   *cqt_frame;   // get pointer to cqt output frame (num_bins_ x 2)
      PiPoValue   *out_frame;
      int          out_width;

#if MOCKUP
      cqt_frame = values; // use input data as pseudo-output
#endif

      // copy cqt output to output frame, possibly transforming to output_mode_
      switch (output_mode_)
      {
        case ComplexFft:
        {
          out_width = 2;
          out_frame = cqt_frame; // output cqt frame unchanged
        }
        break;
            
        case MagnitudeFft:
        {
          float re, im;
            
          out_width = 1;
          out_frame = cqt_frame; // overwrite first half
            
          for (int i = 0; i < num_bins_; i++)
          {
            re = cqt_frame[i * 2];
            im = cqt_frame[i * 2 + 1];
            out_frame[i] = 2 * sqrtf(re * re + im * im);
          }
        }
        break;
            
        case PowerFft:
        {
          float re, im;
            
          out_width = 1;
          out_frame = cqt_frame; // overwrite first half
            
          for (int i = 0; i < num_bins_; i++)
          {
            re = cqt_frame[i * 2];
            im = cqt_frame[i * 2 + 1];
            out_frame[i] = 4 * (re * re + im * im); // why 4?
          }
        }
        break;
            
        case LogPowerFft:
        {
          const double minLogValue = 1e-48;
          const double minLog = -480.0;
          float re, im, pow;
            
          out_width = 1;
          out_frame = cqt_frame; // overwrite first half
            
          for (int i = 0; i < num_bins_; i++)
          {
            re = cqt_frame[i * 2];
            im = cqt_frame[i * 2 + 1];
            pow = re * re + im * im;
            out_frame[i] = pow > minLogValue  ?  10.0f * log10f(pow)  :  minLog;
          }
        }
        break;
      }
        
      int ret = propagateFrames(time, weight, out_frame, out_width * num_bins_, 1);
        
      if (ret != 0)
        return ret;
      
      values += size;
    }
    return 0;
  }
}; // PiPoCQT


// The Constant Q Transform module combining slice and cqt
class PiPoConstantQ : public PiPoSlice // slice is getting the audio input
{
private:
  PiPoCQT cqt_;

public:
  PiPoConstantQ (Parent *parent, PiPo *receiver = NULL)
  : PiPoSlice(parent, &cqt_), // chain ourselves (we are the slicer) to propagate our slices to the cqt module
    cqt_(parent, receiver)   // the cqt module outputs to the receiver
  {
    // make slice and cqt attrs visible to host
    addAttr(this, "hop",  "Hop Size", &hop, true); // first one clears list 
    addAttr(this, "size", "CQT Output Size", &cqt_.numberBins_attr_); // will also set slice.size
    addAttr(this, "minfreq", "CQT minimum frequency [Hz]", &cqt_.minFrequency_attr_);
    addAttr(this, "octavebins", "CQT number of bins per octave", &cqt_.binsPerOctave_attr_);
    addAttr(this, "threshold", "CQT threshold", &cqt_.threshold_attr_);
    addAttr(this, "scale", "CQT filters scale", &cqt_.scale_attr_);
    addAttr(this, "window", "CQT window type", &cqt_.windowType_attr_);
    addAttr(this, "minkernelsize", "CQT minimum kernel size", &cqt_.minimumKernelSize_attr_);
    addAttr(this, "zeroPhase", "CQT zero-phase windowing", &cqt_.zeroPhase_attr_);

    // init and fix other slice attributes for cqt
    hop.set(512);
    size.set(2048);
    unit.set(PiPoSlice::SamplesUnit);
    wind.set(PiPoSlice::NoWindow);      // windowing is done in PiPoCQT
    norm.set(PiPoSlice::NoNorm);
  }

  // the receiver of our little pipo chain will receive the output of the cqt module
  void setReceiver (PiPo *receiver, bool add) { cqt_.setReceiver(receiver, add); };

  int streamAttributes (bool hasTimeTags, double rate, double offset, unsigned int width, unsigned int height, const char **labels, bool hasVarSize, double domain, unsigned int maxFrames)
  {  
    // set interdependent slice parameters from cqt's pipo attrs (without calling streamattr on slice)
    size.set(cqt_.numberBins_attr_.get(), true);

    // call PiPoSlice base class streamAttributes, which will propagate to cqt_.streamAttributes (it's receiver)
    return PiPoSlice::streamAttributes(hasTimeTags, rate, offset, width, height, labels, hasVarSize, domain, maxFrames);
  }
  
  // don't need to override frames(), we use PiPoSlice::frames(), which propagates to cqt_.frames()
}; // PiPoConstantQ

#endif
