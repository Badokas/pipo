/**
 * @file PiPoTemporalModeling.h
 * @author Diemo.Schwarz@ircam.fr
 *
 * @brief generate several PiPo module classes doing temporal modelings
 *
 * @ingroup pipomodules
 *
 * @copyright
 * Copyright (C) 2012-2014 by IRCAM – Centre Pompidou, Paris, France.
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

#ifndef _PIPO_TEMPMOD_
#define _PIPO_TEMPMOD_

#include "PiPo.h"

extern "C" {
#include "rta_configuration.h"
#include "rta_selection.h"
}

#include "TempMod.h"
#include <vector>
#include <string>

template<bool MIN = false, bool MAX = false, bool MEAN = false, bool STD = false, bool DURATION = false>
class PiPoTemporalModeling : public PiPo
{
private:
  double onsettime;
  bool reportduration;
  bool segison;
  TempModArray tempmod_;
  int input_width_ = 0;
  bool pass_input_ = true;
  std::vector<unsigned int> input_columns_;
  std::vector<PiPoValue> selected_values_;
  std::vector<PiPoValue> output_values_;
  
public:
  PiPoVarSizeAttr<PiPo::Atom> columns_attr_;

  PiPoTemporalModeling (Parent *parent, PiPo *receiver = NULL)
  : PiPo(parent, receiver),
    columns_attr_(this, "columns", "List of Column Names or Indices to Use (empty for all)", true, 0)
  {
    this->onsettime = 0;
    this->reportduration = false;
    this->segison = false;
  }
  
  ~PiPoTemporalModeling (void)
  { }
  
  int streamAttributes (bool hastimetags, double rate, double offset,
                        unsigned int width, unsigned int size, const char **labels,
                        bool hasvarsize, double domain, unsigned int maxframes) override
  {
    std::vector<const char *> selected_labels;

    if (columns_attr_.getSize() == 0)
    { // no column choice: set pass through flag for efficiency
      pass_input_    = true;
      input_width_   = width;
    }
    else
    {
      pass_input_    = false;
      input_columns_ = lookup_column_indices(columns_attr_, width, labels);
      input_width_   = input_columns_.size();
      selected_values_.resize(input_width_);

      if (labels)
      { // copy selected labels for tempmod_ to append suffix
	selected_labels.resize(input_width_);
	
	for (int j = 0; j < input_width_; j++)
	  selected_labels[j] = labels[input_columns_[j]];

	labels = &selected_labels[0];
      }
    }
    
    this->onsettime = 0;
    this->reportduration = DURATION;
    
    /* resize temporal models */
    tempmod_.enable(MIN, MAX, MEAN, STD);
    tempmod_.resize(input_width_);
      
    /* get output size */
    unsigned int outputsize = tempmod_.getNumValues();
      
    /* alloc output vector for duration and temporal modelling output */
    output_values_.resize(outputsize + this->reportduration);
      
    /* get labels */
    char *mem = new char[outputsize * 64 + 64];
    char **outlabels = new char*[outputsize + 1];
      
    for (unsigned int i = 0; i <= outputsize; i++)
      outlabels[i] = mem + i * 64;
      
    if (this->reportduration)
      snprintf(outlabels[0], 64, "Duration");
    tempmod_.getLabels(labels, input_width_, &outlabels[this->reportduration], 64, outputsize);
      
    int ret = this->propagateStreamAttributes(true, rate, 0.0, outputsize + this->reportduration, 1,
					      (const char **) &outlabels[0],
					      false, 0.0, 1);
      
    delete [] mem;
    delete [] outlabels;
    
    return ret;
  } // streamAttributes
  
  int reset (void) override
  {
    this->onsettime = 0;
    this->segison = false;
    tempmod_.reset();
    
    return this->propagateReset();
  } // reset

  // receives descriptor data to calculate stats on (until segment() is received)
  int frames (double time, double weight, PiPoValue *values, unsigned int size, unsigned int num) override
  {
    for (unsigned int i = 0; i < num; i++)
    { // for all frames: feed temporal modelling when within segment (is on)
      if (this->segison)
      {
	if (pass_input_)
	  tempmod_.input(values, size);
	else
	{ // copy selected input columns
	  for (int j = 0; j < input_width_; j++)
	    selected_values_[j] = values[input_columns_[j]];

	  tempmod_.input(&selected_values_[0], input_width_);
	}
      }
      values += size;
    }
    
    return 0;
  } // frames

  // segmenter decided start/end of segment: output current stats, if frames have been sent since last segment() call
  int segment (double time, bool start) override
  { 
    if (DURATION)
      output_values_[0] = time - this->onsettime;

    long outputsize = output_values_.size();
          
    /* get temporal modelling */
    if (outputsize > 1)
      tempmod_.getValues(&output_values_[DURATION], outputsize - DURATION, true);

    // remember segment status
    onsettime = time;
    segison = start;
    
    /* report segment data, don't pass on segment() call */
    int ret = this->propagateFrames(time, 0.0, &output_values_[0], outputsize, 1);
    
    return ret;
  } // segment  
  
  int finalize (double inputend) override
  {
    // treat end of input like last segment end
    int ret = segment(inputend, false);
    return ret  &&  this->propagateFinalize(inputend);
  } // finalize
}; // end class PiPoTemporalModeling


// define individual temporal modeling classes
using PiPoSegMin       = PiPoTemporalModeling<1, 0, 0, 0>;
using PiPoSegMax       = PiPoTemporalModeling<0, 1, 0, 0>;
using PiPoSegMinMax    = PiPoTemporalModeling<1, 1, 0, 0>;
using PiPoSegMean      = PiPoTemporalModeling<0, 0, 1, 0>;
using PiPoSegStd       = PiPoTemporalModeling<0, 0, 0, 1>;
using PiPoSegMeanStd   = PiPoTemporalModeling<0, 0, 1, 1>;
using PiPoSegDuration  = PiPoTemporalModeling<0, 0, 0, 0, 1>;
using PiPoSegStats     = PiPoTemporalModeling<1, 1, 1, 1, 1>;
// later: using PiPoSegMedian  = PiPoTemporalModeling<0, 0, 0, 0, 0, 1>;


/** EMACS **
 * Local variables:
 * mode: c++
 * c-basic-offset:2
 * End:
 */

#endif // _PIPO_TEMPMOD_
