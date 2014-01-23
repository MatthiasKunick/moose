//  Rogers-Stallybrass-Clements version of effective saturation as a function of CAPILLARY pressure.
//  valid for residual saturations = 0, and viscosityOil = 2*viscosityWater.  (the "2" is important here!).
// C Rogers, MP Stallybrass and DL Clements "On two phase filtration under gravity and with boundary infiltration: application of a Backlund transformation" Nonlinear Analysis Theory Methods and Applications 7 (1983) 785--799.
//
#ifndef RICHARDSSEFFRSC_H
#define RICHARDSSEFFRSC_H

#include "GeneralUserObject.h"

class RichardsSeffRSC
{
 public:
  RichardsSeffRSC();
  static Real seff(Real pc, Real shift, Real scale);
  static Real dseff(Real pc, Real shift, Real scale);
  static Real d2seff(Real pc, Real shift, Real scale);

};

#endif // RICHARDSSEFFRSC_H
