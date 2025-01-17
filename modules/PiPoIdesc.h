/**
 * @file PiPoIdesc.h
 * @author Diemo.Schwarz@ircam.fr
 *
 * @brief PiPo wrapper for maxpipo project using idesc ircamdescriptor API
 *
 * @copyright
 * Copyright (C) 2012 by IMTR IRCAM – Centre Pompidou, Paris, France.
 * All rights reserved.
 */

#ifndef _PIPO_IDESC_
#define _PIPO_IDESC_

#define IDESC_DEBUG DEBUG*2

#include "PiPo.h"
#include <vector>
#include <map>
#include <stdexcept>


#ifdef WIN32
#define snprintf sprintf_s
#endif

//#define post printf
#define IDESC_REAL_TYPE float	/* pipo with mubu works with float output data */
#include "idesc.h"

using namespace std;

class idescx;

class PiPoIdesc : public PiPo
{
public:
  // define parameter fields (= attributes)
#define IDESC_PARAM(TYPE, NAME, DEFAULT, BLURB)	\
  PiPoScalarAttr<TYPE> NAME;
#define  IDESC_BANDS_READ(NAME, ATTR, MAXSIZE, BLURB)	\
  PiPoVarSizeAttr<float> ATTR;
#include "ircamdescriptor~params.h"

  PiPoScalarAttr<enum PiPo::Enumerate> window;
  PiPoScalarAttr<enum PiPo::Enumerate> windowunit;
  PiPoVarSizeAttr<enum PiPo::Enumerate> descriptors; // list of descriptor name symbols asked for by user

  PiPoIdesc(PiPo::Parent *parent, PiPo *receiver = NULL);
  ~PiPoIdesc(void);

  int streamAttributes(bool hasTimeTags, double rate, double offset,
                       unsigned int width, unsigned int size, const char **labels,
                       bool hasVarSize, double domain, unsigned int maxFrames);
  int finalize (double inputEnd);
  int reset(void);
  int frames(double time, double weight, float *values, unsigned int size, unsigned int num);

private:
  static void datacallback (int descrid, int varnum, int numval, IDESC_REAL_TYPE *values, void* obj);
  static void endcallback (double frame_time_sec, void* obj);

  idescx		       *idesc_;
  float			       *outbuf_;
  bool				initialised_;
  int				status_;
  std::map<int, int>		doffset_;
  std::map<int, int>		dwidth_;
  int				numcols_;
  const char                  **colnames_;

  void clearcolnames();
};


class idescx : public idesc::idesc
{
public:
  idescx (double sr, double winsize, double hopsize, PiPoIdesc *_pipo)
  : idesc(sr, winsize, hopsize),
  pipo(_pipo)
  { };

  // dummy setters to keep IDESC_PARAM macro happy
  void set_WindowSize (int ws) {};
  void set_HopSize (int hs) {};

  // set chroma limits as one band: fixed num = 1!
  void set_chromarange_band_limits (int num, float *bands)
  {
    if (num == 1)
      set_ChromaRange(bands[0], bands[1]);
  };

  /** always returns 1 */
  int  get_chromarange_band_num () { return 1; };

  /** get chroma limits as one band */
  void get_chromarange_band_limits (/*out*/ float *bands)
  { // get limits from first and last band
    bands[0] = pipo->ChromaRange.getDbl(0);
    bands[1] = pipo->ChromaRange.getDbl(1);
  };

private:
  PiPoIdesc *pipo;	// pointer to containing object to query chroma range
};



PiPoIdesc::PiPoIdesc(PiPo::Parent *parent, PiPo *receiver)
: PiPo(parent, receiver),

// declare and initialise parameter fields (= attributes)
#define IDESC_PARAM(TYPE, NAME, DEFAULT, BLURB)	\
  NAME(this, #NAME, BLURB, true, DEFAULT),	// "changesStream" is true for most of them
#define IDESC_BANDS_READ(NAME, ATTR, MAXSIZE, BLURB)	\
  ATTR(this, #ATTR, BLURB, true),
#include "ircamdescriptor~params.h"

  window(this, "window", "Analysis window type", false, 0),
  windowunit(this, "windowunit", "Analysis window and hop size unit", true, 0),
  descriptors(this, "descriptors", "Descriptors to calculate", true, 0)
{
  idesc_  = NULL;
  outbuf_ = NULL;
  colnames_ = NULL;

  // set up and query idesc library
  idesc::idesc::init_library();	// no deinit_library needed within max session
  int num_descr_available = idesc::idesc::get_num_descriptors();

  // set up descriptor enum (relying on it starting at 0)
  for (int i = 0; i < num_descr_available; i++)
    descriptors.addEnumItem(idesc::idesc::get_descriptor_name(i));

  // set up window type and unit enums
  window.addEnumItem("blackman");
  window.addEnumItem("hamming");
  window.addEnumItem("hanning");
  window.addEnumItem("hanning2");
  windowunit.addEnumItem("source");
  windowunit.addEnumItem("resampled");
  windowunit.addEnumItem("msec");

  // get default band limit lists to populate attrs
# define IDESC_BANDS_READ(NAME, ATTR, MAXSIZE, BLURB)			\
  ATTR.setSize(idesc_ ? 2 * idesc_->get_ ## NAME ## _band_num(): 0);	\
  if (idesc_) idesc_->get_ ## NAME ## _band_limits(ATTR.getPtr());

# include "ircamdescriptor~params.h"

  initialised_ = true;
}

PiPoIdesc::~PiPoIdesc(void)
{
  if (idesc_)
  {
    delete idesc_;
    idesc_ = NULL;
  }
  free(outbuf_);
  if (colnames_ != NULL)
    clearcolnames();	//FIXME: free strings
}

void PiPoIdesc::clearcolnames ()
{
  for (int i = 0; i < numcols_; i++)
    free((void *) colnames_[i]);

  free((void *) colnames_);
  colnames_ = NULL;
}

///////////////////////////////////////////////////////////////////////////////
//
// init module
//

int PiPoIdesc::streamAttributes (bool hasTimeTags, double rate, double offset,
                                 unsigned int width, unsigned int size,
                                 const char **labels, bool hasVarSize,
                                 double domain, unsigned int maxFrames)
{
  double factor;

  switch (windowunit.get())
  {
    default:
    case 0:	factor = rate;			break;  // relative to input sr
    case 1:	factor = ResampleTo.get();	break;  // relative to resampled sr
    case 2:	factor = 1000;			break;  // in milliseconds
  }

  double winlen = WindowSize.getDbl() / factor;	// window and hop size in sec
  double hoplen = HopSize.getDbl()    / factor;	// hop size in sec
  int    ndescr = descriptors.getSize();
  numcols_ = 0;

#if IDESC_DEBUG >= 1
  post("PiPoIdesc streamAttributes timetags %d  rate %.0f  offset %f  width %d  size %d  labels %s  "
       "varsize %d  domain %f  maxframes %d --> win %d = %f s  hop %d = %f s  numdescr %d\n",
       hasTimeTags, rate, offset, (int) width, (int) size, labels ? labels[0] : "n/a", (int) hasVarSize, domain, (int) maxFrames,
       (int) WindowSize.getInt(), winlen, (int) HopSize.getInt(), hoplen, ndescr);
#endif

  if (initialised_  &&  ndescr > 0)
  {
    try {
      // init idesc
      idesc_ = new idescx(rate, winlen, hoplen, this);

      // set up idesc params from pipo attrs
#     define IDESC_PARAM(TYPE, NAME, DEFAULT, BLURB) \
      idesc_->set_ ## NAME(NAME.get());
#     define IDESC_BANDS_WRITE(NAME, ATTR, MAXSIZE, BLURB)		\
      { int sz = ATTR.getSize();					\
	if (sz > 1) idesc_->set_ ## NAME ## _band_limits(sz / 2, ATTR.getPtr()); /* at least 2 */ \
	if (sz & 1) signalWarning(#ATTR " needs pairs of values, truncating"); \
      }
      /* As it is not possible with pipoattrs to get called directly
       when an attr changes, the following semantics is not
       possible: when num bands are set explicitly, the user-set
       bands are reinitialised to default values.
       The only way to clear user-defined bands is not to set them as attributes, or to set them with 0 length.
       #        define IDESC_BANDN_UPDATED(_name, _bands) \
       */

#     include "ircamdescriptor~params.h"

      if (colnames_ != NULL) clearcolnames();
      colnames_ = (const char **) malloc(ndescr * sizeof(char *));

      // set window type
      idesc_->set_window(window.getStr());

      // set up idesc descriptors
      for (int i = 0; i < ndescr; i++)
      {
        const char *dname = descriptors.getStr(i);
        int         did   = descriptors.getInt(i);

        if (dname  &&  did >= 0)
        {
          colnames_[i] = strdup(dname);
          idesc_->set_descriptor(did, idesc::idesc::get_default_variation(did));
        }
        else
        {
          throw std::invalid_argument(std::string("unknown descriptor name at index ")); //C++11: + std::to_string(i));
        }

#if IDESC_DEBUG >= 1
        post("colnames descr %2d/%2d: %s\n", i, ndescr, colnames_[i]);
#endif
      }

      // build idesc graph
      idesc_->build_descriptors();

      // query output sizes
      for (int i = 0; i < ndescr; i++)
      {
        int did = descriptors.getInt(i);
        int dcols = idesc_->get_dimensions(did);
        dwidth_[did] = dcols;
        doffset_[did] = numcols_;
        numcols_ += dcols;
      }
      outbuf_ = (float *) realloc(outbuf_, numcols_ * sizeof(float));

      if (numcols_ > ndescr)
      { // generate column names with index for non-singleton descriptors
        colnames_ = (const char **) realloc(colnames_, numcols_ * sizeof(char *));
        for (int i = ndescr - 1; i >= 0; i--)
        {
          int did = descriptors.getInt(i);
          //post("  descr %d width %d -> col %d (numcols %d): %s\n", i, dwidth_[did], doffset_[did], numcols_, colnames_[i]);

          if (dwidth_[did] == 1)
            colnames_[doffset_[did]] = colnames_[i];	// move strdup'ed string pointer
          else
          {
            char nbuf[128];
            int  w = dwidth_[did] > 9  ?  2  :  1;

            for (int j = dwidth_[did] - 1; j >= 0; j--)
            {
              snprintf(nbuf, 128, "%s%0*d", colnames_[i], w, j);
              colnames_[doffset_[did] + j] = strdup(nbuf);

              //post("    colname %d: %s\n", doffset_[did] + j, colnames_[doffset_[did] + j]);
            }
          }
        }
      }

      bool varsize = false; // HarmonicModel in descriptors?
      status_ = 0;

      return this->propagateStreamAttributes(true, 1 / hoplen, offset, numcols_, 1,
                                             colnames_, varsize, winlen * 1000., 1);
    } catch (std::exception& e) {
#if IDESC_DEBUG > 0
      post("pipo.ircamdescriptor error: IrcamDescriptor library: %s\n", e.what ());
#endif
      signalError(std::string("pipo.ircamdescriptor error: IrcamDescriptor library: ") + e.what());

      if (idesc_)
      {
        delete idesc_;
        idesc_ = NULL;
      }
      status_ = -1;
      return -1;
    }
  }
  else
    return -1; // TBD: signalWarning?
}

int PiPoIdesc::finalize (double inputEnd)
{
#if IDESC_DEBUG >= 1
  post("PiPoIdesc finalize %f\n", inputEnd);
#endif
  return this->propagateFinalize(inputEnd);
};


int PiPoIdesc::reset (void)
{
#if IDESC_DEBUG >= 1
  post("PiPoIdesc reset\n");
#endif

  if (idesc_)
  {
    try {
      // rebuild idesc graph to start over (necessary after finalize)
      // assume sizes have not changed
      idesc_->build_descriptors();
    } catch (std::exception& e) {
#if IDESC_DEBUG > 0
      post("pipo.ircamdescriptor reset error: IrcamDescriptor library: %s\n", e.what ());
#endif
      signalError(std::string("pipo.ircamdescriptor reset error: IrcamDescriptor library: ") + e.what());
      return -1;
    }

    status_ = 0;

    return this->propagateReset();
  }
  else
    return -1;
};


///////////////////////////////////////////////////////////////////////////////
//
// compute and output data
//

void PiPoIdesc::datacallback (int descrid, int varnum, int numval,
                              IDESC_REAL_TYPE *values, void* obj)
{
  PiPoIdesc *self = (PiPoIdesc *) obj;

  // gather data for each descriptor
  int offset = self->doffset_[descrid];
  for (int i = 0; i < self->dwidth_[descrid]; i++)
    self->outbuf_[offset + i] = values[i];
}

void PiPoIdesc::endcallback (double frame_time_sec, void* obj)
{
  PiPoIdesc *self = (PiPoIdesc *) obj;

  // propagate gathered frame data
  self->status_ = self->propagateFrames(frame_time_sec * 1000., 1., self->outbuf_, self->numcols_, 1);
}

int PiPoIdesc::frames (double time, double weight, float *values, unsigned int size, unsigned int num)
{
#if IDESC_DEBUG >= 2
  post("PiPoIdesc::frames time %f  values %p  size %d  num %d\n",
       time, values, size, num);
#endif
  float *mono;

  if (idesc_)
  {
    if (size > 1)
    { // pick mono channel
      mono = (float *) alloca(num * sizeof(float));

      for (unsigned int i = 0, j = 0; i < num; i++, j += size)
        mono[i] = values[j];

      values = mono;
    }

    try {
      idesc_->compute(num, values, datacallback, NULL, endcallback, this);
    } catch (std::exception& e) {
#if IDESC_DEBUG >= 2
      post("pipo.ircamdescriptor frames error: IrcamDescriptor library: %s\n", e.what ());
      printf("pipo.ircamdescriptor frames error: IrcamDescriptor library: %s\n", e.what ());
#endif
      signalError(std::string("pipo.ircamdescriptor frames error: IrcamDescriptor library: ") + e.what());
      return -1;
    }
    
    return status_;
  }
  else
    return -1;
}

/** EMACS **
 * Local variables:
 * mode: c++
 * c-basic-offset:2
 * End:
 */

#endif /* _PIPO_IDESC_ */
