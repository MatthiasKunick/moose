//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#include "IdealGasFluidPropertiesTest.h"
#include "SinglePhaseFluidPropertiesTestUtils.h"

TEST_F(IdealGasFluidPropertiesTest, testAll)
{
  const Real T = 120. + 273.15; // K
  const Real p = 101325;        // Pa

  const Real rho = _fp->rho_from_p_T(p, T);
  const Real v = 1 / rho;
  const Real e = _fp->e_from_p_rho(p, rho);
  const Real s = _fp->s_from_v_e(v, e);
  const Real h = _fp->h_from_p_T(p, T);

  REL_TEST(_fp->p_from_h_s(h, s), p, REL_TOL_CONSISTENCY);
  DERIV_TEST(_fp->p_from_h_s, h, s, REL_TOL_DERIVATIVE);

  REL_TEST(_fp->rho_from_p_s(p, s), rho, REL_TOL_CONSISTENCY);
  DERIV_TEST(_fp->rho_from_p_s, p, s, REL_TOL_DERIVATIVE);

  REL_TEST(_fp->p_from_v_e(v, e), p, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->p_from_v_e, v, e, REL_TOL_DERIVATIVE);

  REL_TEST(_fp->T_from_v_e(v, e), T, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->T_from_v_e, v, e, REL_TOL_DERIVATIVE);

  REL_TEST(_fp->c_from_v_e(v, e), 398.896207251962, REL_TOL_SAVED_VALUE);
  REL_TEST(_fp->cp_from_v_e(v, e), 987.13756097561, REL_TOL_SAVED_VALUE);
  REL_TEST(_fp->cv_from_v_e(v, e), 700.09756097561, REL_TOL_SAVED_VALUE);
  REL_TEST(_fp->mu_from_v_e(v, e), 0.0, 1e-15);
  REL_TEST(_fp->k_from_v_e(v, e), 0.0, 1e-15);

  REL_TEST(_fp->beta_from_p_T(p, T), 2.54355843825512e-3, REL_TOL_SAVED_VALUE);

  REL_TEST(_fp->s_from_v_e(v, e), 2.58890011905277e3, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->s_from_v_e, v, e, REL_TOL_DERIVATIVE);

  ABS_TEST(_fp->rho_from_p_T(p, T), 0.897875065343506, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->rho_from_p_T, p, T, REL_TOL_DERIVATIVE);

  ABS_TEST(_fp->v_from_p_T(p, T), 1.0 / 0.897875065343506, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->v_from_p_T, p, T, REL_TOL_DERIVATIVE);

  ABS_TEST(_fp->e_from_p_rho(p, rho), 2.75243356097561e5, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->e_from_p_rho, p, rho, REL_TOL_DERIVATIVE);

  ABS_TEST(_fp->h_from_p_T(p, T), 3.88093132097561e5, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->h_from_p_T, p, T, REL_TOL_DERIVATIVE);

  ABS_TEST(_fp->s_from_p_T(p, T), 2.588900119052767e3, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->s_from_p_T, p, T, REL_TOL_DERIVATIVE);
  ABS_TEST(_fp->s_from_h_p(h, p), 2.588900119052767e3, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->s_from_h_p, h, p, REL_TOL_DERIVATIVE);

  ABS_TEST(_fp->e_from_p_T(p, T), 2.75243356097561e5, REL_TOL_SAVED_VALUE);
  DERIV_TEST(_fp->e_from_p_T, p, T, REL_TOL_DERIVATIVE);

  ABS_TEST(_fp->molarMass(), 34.522988492890422, REL_TOL_SAVED_VALUE);

  ABS_TEST(_fp->T_from_p_h(p, h), T, REL_TOL_CONSISTENCY);
}
