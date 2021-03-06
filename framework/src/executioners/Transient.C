//* This file is part of the MOOSE framework
//* https://www.mooseframework.org
//*
//* All rights reserved, see COPYRIGHT for full restrictions
//* https://github.com/idaholab/moose/blob/master/COPYRIGHT
//*
//* Licensed under LGPL 2.1, please see LICENSE for details
//* https://www.gnu.org/licenses/lgpl-2.1.html

#include "Transient.h"

// MOOSE includes
#include "Factory.h"
#include "SubProblem.h"
#include "TimeStepper.h"
#include "MooseApp.h"
#include "Conversion.h"
#include "FEProblem.h"
#include "NonlinearSystem.h"
#include "Control.h"
#include "TimePeriod.h"
#include "MooseMesh.h"
#include "AllLocalDofIndicesThread.h"
#include "TimeIntegrator.h"

#include "libmesh/implicit_system.h"
#include "libmesh/nonlinear_implicit_system.h"
#include "libmesh/transient_system.h"
#include "libmesh/numeric_vector.h"

// C++ Includes
#include <iomanip>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

registerMooseObject("MooseApp", Transient);

template <>
InputParameters
validParams<Transient>()
{
  InputParameters params = validParams<Executioner>();
  std::vector<Real> sync_times(1);
  sync_times[0] = -std::numeric_limits<Real>::max();

  /**
   * For backwards compatibility we'll allow users to set the TimeIntegration scheme inside of the
   * executioner block
   * as long as the TimeIntegrator does not have any additional parameters.
   */
  MooseEnum schemes(
      "implicit-euler explicit-euler crank-nicolson bdf2 explicit-midpoint dirk explicit-tvd-rk-2",
      "implicit-euler");

  params.addParam<Real>("start_time", 0.0, "The start time of the simulation");
  params.addParam<Real>("end_time", 1.0e30, "The end time of the simulation");
  params.addParam<Real>("dt", 1., "The timestep size between solves");
  params.addParam<Real>("dtmin", 2.0e-14, "The minimum timestep size in an adaptive run");
  params.addParam<Real>("dtmax", 1.0e30, "The maximum timestep size in an adaptive run");
  params.addParam<bool>(
      "reset_dt", false, "Use when restarting a calculation to force a change in dt.");
  params.addParam<unsigned int>("num_steps",
                                std::numeric_limits<unsigned int>::max(),
                                "The number of timesteps in a transient run");
  params.addParam<int>("n_startup_steps", 0, "The number of timesteps during startup");

  params.addDeprecatedParam<bool>("trans_ss_check",
                                  false,
                                  "Whether or not to check for steady state conditions",
                                  "Use steady_state_detection instead");
  params.addDeprecatedParam<Real>("ss_check_tol",
                                  1.0e-08,
                                  "Whenever the relative residual changes by less "
                                  "than this the solution will be considered to be "
                                  "at steady state.",
                                  "Use steady_state_tolerance instead");
  params.addDeprecatedParam<Real>(
      "ss_tmin",
      0.0,
      "Minimum amount of time to run before checking for steady state conditions.",
      "Use steady_state_start_time instead");

  params.addParam<bool>(
      "steady_state_detection", false, "Whether or not to check for steady state conditions");
  params.addParam<Real>("steady_state_tolerance",
                        1.0e-08,
                        "Whenever the relative residual changes by less "
                        "than this the solution will be considered to be "
                        "at steady state.");
  params.addParam<Real>(
      "steady_state_start_time",
      0.0,
      "Minimum amount of time to run before checking for steady state conditions.");

  params.addParam<std::vector<std::string>>("time_periods", "The names of periods");
  params.addParam<std::vector<Real>>("time_period_starts", "The start times of time periods");
  params.addParam<std::vector<Real>>("time_period_ends", "The end times of time periods");
  params.addParam<bool>(
      "abort_on_solve_fail", false, "abort if solve not converged rather than cut timestep");
  params.addParam<MooseEnum>("scheme", schemes, "Time integration scheme used.");
  params.addParam<Real>("timestep_tolerance",
                        2.0e-14,
                        "the tolerance setting for final timestep size and sync times");

  params.addParam<bool>("use_multiapp_dt",
                        false,
                        "If true then the dt for the simulation will be "
                        "chosen by the MultiApps.  If false (the "
                        "default) then the minimum over the master dt "
                        "and the MultiApps is used");

  params.addParam<unsigned int>("picard_max_its",
                                1,
                                "Number of times each timestep will be solved.  Mainly used when "
                                "wanting to do Picard iterations with MultiApps that are set to "
                                "execute_on timestep_end or timestep_begin");
  params.addParam<Real>("picard_rel_tol",
                        1e-8,
                        "The relative nonlinear residual drop to shoot for "
                        "during Picard iterations.  This check is "
                        "performed based on the Master app's nonlinear "
                        "residual.");
  params.addParam<Real>("picard_abs_tol",
                        1e-50,
                        "The absolute nonlinear residual to shoot for "
                        "during Picard iterations.  This check is "
                        "performed based on the Master app's nonlinear "
                        "residual.");

  params.addParam<Real>("relaxation_factor",
                        1.0,
                        "Fraction of newly computed value to keep."
                        "Set between 0 and 2.");
  params.addParam<std::vector<std::string>>("relaxed_variables",
                                            std::vector<std::string>(),
                                            "List of variables to relax during Picard Iteration");

  params.addParamNamesToGroup(
      "steady_state_detection steady_state_tolerance steady_state_start_time",
      "Steady State Detection");

  params.addParamNamesToGroup("start_time dtmin dtmax n_startup_steps trans_ss_check ss_check_tol "
                              "ss_tmin abort_on_solve_fail timestep_tolerance use_multiapp_dt",
                              "Advanced");

  params.addParamNamesToGroup("time_periods time_period_starts time_period_ends", "Time Periods");

  params.addParamNamesToGroup(
      "picard_max_its picard_rel_tol picard_abs_tol relaxation_factor relaxed_variables", "Picard");

  params.addParam<bool>("verbose", false, "Print detailed diagnostics on timestep calculation");
  params.addParam<unsigned int>(
      "max_xfem_update",
      std::numeric_limits<unsigned int>::max(),
      "Maximum number of times to update XFEM crack topology in a step due to evolving cracks");
  params.addParam<bool>("update_xfem_at_timestep_begin",
                        false,
                        "Should XFEM update the mesh at the beginning of the timestep");

  return params;
}

Transient::Transient(const InputParameters & parameters)
  : Executioner(parameters),
    _problem(_fe_problem),
    _nl(_fe_problem.getNonlinearSystemBase()),
    _time_scheme(getParam<MooseEnum>("scheme").getEnum<Moose::TimeIntegratorType>()),
    _t_step(_problem.timeStep()),
    _time(_problem.time()),
    _time_old(_problem.timeOld()),
    _dt(_problem.dt()),
    _dt_old(_problem.dtOld()),
    _unconstrained_dt(declareRecoverableData<Real>("unconstrained_dt", -1)),
    _at_sync_point(declareRecoverableData<bool>("at_sync_point", false)),
    _first(declareRecoverableData<bool>("first", true)),
    _multiapps_converged(declareRecoverableData<bool>("multiapps_converged", true)),
    _last_solve_converged(declareRecoverableData<bool>("last_solve_converged", true)),
    _xfem_repeat_step(false),
    _xfem_update_count(0),
    _max_xfem_update(getParam<unsigned int>("max_xfem_update")),
    _update_xfem_at_timestep_begin(getParam<bool>("update_xfem_at_timestep_begin")),
    _end_time(getParam<Real>("end_time")),
    _dtmin(getParam<Real>("dtmin")),
    _dtmax(getParam<Real>("dtmax")),
    _num_steps(getParam<unsigned int>("num_steps")),
    _n_startup_steps(getParam<int>("n_startup_steps")),
    _steps_taken(0),
    _steady_state_detection(getParam<bool>("steady_state_detection")),
    _steady_state_tolerance(getParam<Real>("steady_state_tolerance")),
    _steady_state_start_time(getParam<Real>("steady_state_start_time")),
    _sln_diff_norm(declareRecoverableData<Real>("sln_diff_norm", 0.0)),
    _old_time_solution_norm(declareRecoverableData<Real>("old_time_solution_norm", 0.0)),
    _sync_times(_app.getOutputWarehouse().getSyncTimes()),
    _abort(getParam<bool>("abort_on_solve_fail")),
    _time_interval(declareRecoverableData<bool>("time_interval", false)),
    _start_time(getParam<Real>("start_time")),
    _timestep_tolerance(getParam<Real>("timestep_tolerance")),
    _target_time(declareRecoverableData<Real>("target_time", -1)),
    _use_multiapp_dt(getParam<bool>("use_multiapp_dt")),
    _picard_it(declareRecoverableData<int>("picard_it", 0)),
    _picard_max_its(getParam<unsigned int>("picard_max_its")),
    _picard_converged(declareRecoverableData<bool>("picard_converged", false)),
    _picard_initial_norm(declareRecoverableData<Real>("picard_initial_norm", 0.0)),
    _picard_timestep_begin_norm(declareRecoverableData<Real>("picard_timestep_begin_norm", 0.0)),
    _picard_timestep_end_norm(declareRecoverableData<Real>("picard_timestep_end_norm", 0.0)),
    _picard_rel_tol(getParam<Real>("picard_rel_tol")),
    _picard_abs_tol(getParam<Real>("picard_abs_tol")),
    _verbose(getParam<bool>("verbose")),
    _sln_diff(_nl.addVector("sln_diff", false, PARALLEL)),
    _relax_factor(getParam<Real>("relaxation_factor")),
    _relaxed_vars(getParam<std::vector<std::string>>("relaxed_variables")),
    _final_timer(registerTimedSection("final", 1))
{
  // Handl deprecated parameters
  if (!parameters.isParamSetByAddParam("trans_ss_check"))
    _steady_state_detection = getParam<bool>("trans_ss_check");

  if (!parameters.isParamSetByAddParam("ss_check_tol"))
    _steady_state_tolerance = getParam<Real>("ss_check_tol");

  if (!parameters.isParamSetByAddParam("ss_tmin"))
    _steady_state_start_time = getParam<Real>("ss_tmin");

  _nl.setDecomposition(_splitting);
  _t_step = 0;
  _dt = 0;
  _next_interval_output_time = 0.0;

  // Either a start_time has been forced on us, or we want to tell the App about what our start time
  // is (in case anyone else is interested.
  if (_app.hasStartTime())
    _start_time = _app.getStartTime();
  else if (parameters.isParamSetByUser("start_time"))
    _app.setStartTime(_start_time);

  _time = _time_old = _start_time;
  _problem.transient(true);

  if (!_restart_file_base.empty())
    _problem.setRestartFile(_restart_file_base);

  setupTimeIntegrator();

  if (_app.halfTransient()) // Cut timesteps and end_time in half...
  {
    _end_time /= 2.0;
    _num_steps /= 2.0;

    if (_num_steps == 0) // Always do one step in the first half
      _num_steps = 1;
  }

  // Set up relaxation
  if (_relax_factor != 1.0)
  {
    if (_relax_factor >= 2.0 || _relax_factor <= 0.0)
      mooseError("The Picard iteration relaxation factor should be between 0.0 and 2.0");

    // Store a copy of the previous solution here
    _nl.addVector("relax_previous", false, PARALLEL);
  }
  // This lets us know if we are at Picard iteration > 0, works for both master- AND sub-app.
  // Initialize such that _prev_time != _time for the first Picard iteration
  _prev_time = _time - 1.0;
}

void
Transient::init()
{
  if (!_time_stepper.get())
  {
    InputParameters pars = _app.getFactory().getValidParams("ConstantDT");
    pars.set<SubProblem *>("_subproblem") = &_problem;
    pars.set<Transient *>("_executioner") = this;

    /**
     * We have a default "dt" set in the Transient parameters but it's possible for users to set
     * other
     * parameters explicitly that could provide a better calculated "dt". Rather than provide
     * difficult
     * to understand behavior using the default "dt" in this case, we'll calculate "dt" properly.
     */
    if (!_pars.isParamSetByAddParam("end_time") && !_pars.isParamSetByAddParam("num_steps") &&
        _pars.isParamSetByAddParam("dt"))
      pars.set<Real>("dt") = (getParam<Real>("end_time") - getParam<Real>("start_time")) /
                             static_cast<Real>(getParam<unsigned int>("num_steps"));
    else
      pars.set<Real>("dt") = getParam<Real>("dt");

    pars.set<bool>("reset_dt") = getParam<bool>("reset_dt");
    _time_stepper = _app.getFactory().create<TimeStepper>("ConstantDT", "TimeStepper", pars);
  }

  _problem.initialSetup();

  _time_stepper->init();

  if (_app.isRestarting())
    _time_old = _time;

  _problem.outputStep(EXEC_INITIAL);

  if (_app.isRecovering()) // Recover case
  {
    if (_t_step == 0)
      mooseError("Internal error in Transient executioner: _t_step is equal to 0 while recovering "
                 "in init().");

    _dt_old = _dt;
  }
  else
  {
    if (_t_step != 0)
      mooseError("Internal error in Transient executioner: _t_step must be 0 without "
                 "recovering in init().");

    computeDT();
    _dt = getDT();
    if (_dt == 0)
      mooseError("Time stepper computed zero time step size on initial which is not allowed.\n"
                 "1. If you are using an existing time stepper, double check the values in your "
                 "input file or report an error.\n"
                 "2. If you are developing a new time stepper, make sure that initial time step "
                 "size in your code is computed correctly.");

    _nl.getTimeIntegrator()->init();

    ++_t_step;
  }
}

void
Transient::preStep()
{
  _time_stepper->preStep();
}

void
Transient::postStep()
{
  _time_stepper->postStep();
}

void
Transient::execute()
{

  preExecute();

  // NOTE: if you remove this line, you will see a subset of tests failing. Those tests might have a
  // wrong answer and might need to be regolded.
  // The reason is that we actually move the solution back in time before we actually start solving
  // (which I think is wrong).  So this call here
  // is to maintain backward compatibility and so that MOOSE is giving the same answer.  However, we
  // might remove this call and regold the test
  // in the future eventually.
  if (!_app.isRecovering())
    _problem.advanceState();

  // Start time loop...
  while (true)
  {
    if (_first != true)
      incrementStepOrReject();

    _first = false;

    if (!keepGoing())
      break;

    preStep();
    computeDT();
    takeStep();
    endStep();
    postStep();

    _steps_taken++;
  }

  if (!_app.halfTransient())
  {
    TIME_SECTION(_final_timer);

    _problem.outputStep(EXEC_FINAL);
    _problem.execute(EXEC_FINAL);
  }

  // This method is to finalize anything else we want to do on the problem side.
  _problem.postExecute();

  // This method can be overridden for user defined activities in the Executioner.
  postExecute();
}

void
Transient::computeDT()
{
  _time_stepper->computeStep(); // This is actually when DT gets computed
}

void
Transient::incrementStepOrReject()
{
  if (lastSolveConverged())
  {
    if (_xfem_repeat_step)
    {
      _time = _time_old;
    }
    else
    {
#ifdef LIBMESH_ENABLE_AMR
      _problem.adaptMesh();
#endif

      _time_old = _time; // = _time_old + _dt;
      _t_step++;

      _problem.advanceState();

      /*
       * Call the multi-app executioners endStep and
       * postStep methods when doing Picard. We do not perform these calls for
       * loose coupling because Transient::endStep and Transient::postStep get
       * called from TransientMultiApp::solveStep in that case.
       */
      if (_picard_max_its > 1)
      {
        _problem.finishMultiAppStep(EXEC_TIMESTEP_BEGIN);
        _problem.finishMultiAppStep(EXEC_TIMESTEP_END);
      }
      /*
       * Ensure that we increment the sub-application time steps so that
       * when dt selection is made in the master application, we are using
       * the correct time step information
       */
      _problem.incrementMultiAppTStep(EXEC_TIMESTEP_BEGIN);
      _problem.incrementMultiAppTStep(EXEC_TIMESTEP_END);
    }
  }
  else
  {
    _problem.restoreMultiApps(EXEC_TIMESTEP_BEGIN, true);
    _problem.restoreMultiApps(EXEC_TIMESTEP_END, true);
    _time_stepper->rejectStep();
    _time = _time_old;
  }

  _first = false;
}

void
Transient::takeStep(Real input_dt)
{
  _picard_it = 0;

  _problem.backupMultiApps(EXEC_TIMESTEP_BEGIN);
  _problem.backupMultiApps(EXEC_TIMESTEP_END);

  while (_picard_it < _picard_max_its && _picard_converged == false)
  {
    // For every iteration other than the first, we need to restore the state of the MultiApps
    if (_picard_it > 0)
    {
      _problem.restoreMultiApps(EXEC_TIMESTEP_BEGIN);
      _problem.restoreMultiApps(EXEC_TIMESTEP_END);
    }

    solveStep(input_dt);

    // If the last solve didn't converge then we need to exit this step completely (even in the case
    // of Picard)
    // So we can retry...
    if (!lastSolveConverged())
      return;

    ++_picard_it;
  }
}

void
Transient::solveStep(Real input_dt)
{
  _dt_old = _dt;

  if (input_dt == -1.0)
    _dt = computeConstrainedDT();
  else
    _dt = input_dt;

  Real current_dt = _dt;

  if (_picard_it == 0)
    _problem.onTimestepBegin();

  // Increment time
  _time = _time_old + _dt;

  if (_picard_max_its > 1)
  {
    _console << "\nBeginning Picard Iteration " << _picard_it << "\n" << std::endl;

    if (_picard_it == 0) // First Picard iteration - need to save off the initial nonlinear residual
    {
      _picard_initial_norm = _problem.computeResidualL2Norm();
      _console << "Initial Picard Norm: " << _picard_initial_norm << '\n';
    }
  }

  _problem.execTransfers(EXEC_TIMESTEP_BEGIN);
  _multiapps_converged = _problem.execMultiApps(EXEC_TIMESTEP_BEGIN, _picard_max_its == 1);

  if (!_multiapps_converged)
    return;

  if (_problem.haveXFEM() && _update_xfem_at_timestep_begin)
    _problem.updateMeshXFEM();

  preSolve();
  _time_stepper->preSolve();

  _problem.timestepSetup();

  _problem.execute(EXEC_TIMESTEP_BEGIN);

  if (_picard_max_its > 1)
  {
    _picard_timestep_begin_norm = _problem.computeResidualL2Norm();

    _console << "Picard Norm after TIMESTEP_BEGIN MultiApps: " << _picard_timestep_begin_norm
             << '\n';
  }

  // Perform output for timestep begin
  _problem.outputStep(EXEC_TIMESTEP_BEGIN);

  // Update warehouse active objects
  _problem.updateActiveObjects();

  // Prepare to relax variables.
  // _prev_time == _time is like _picard_it > 0, but it also works for the sub-app
  if (_prev_time == _time && _relax_factor != 1.0)
  {
    NumericVector<Number> & solution = _nl.solution();
    NumericVector<Number> & relax_previous = _nl.getVector("relax_previous");

    // Save off the current solution
    relax_previous = solution;

    // Snag all of the local dof indices for all of these variables
    System & libmesh_nl_system = _nl.system();
    AllLocalDofIndicesThread aldit(libmesh_nl_system, _relaxed_vars);
    ConstElemRange & elem_range = *_fe_problem.mesh().getActiveLocalElementRange();
    Threads::parallel_reduce(elem_range, aldit);

    _relaxed_dofs = aldit._all_dof_indices;
  }

  _time_stepper->step();

  // Relax the "relaxed_variables" if this is not the first Picard iteration of the timestep.
  // _prev_time == _time is like _picard_it > 0, but it also works for the sub-app
  if (_prev_time == _time && _relax_factor != 1.0)
  {
    NumericVector<Number> & solution = _nl.solution();
    NumericVector<Number> & relax_previous = _nl.getVector("relax_previous");
    for (const auto & dof : _relaxed_dofs)
      solution.set(dof,
                   (relax_previous(dof) * (1.0 - _relax_factor)) + (solution(dof) * _relax_factor));
    solution.close();
    _nl.update();
  }
  // This keeps track of Picard iteration, even if this is the sub-app.
  // It is used for relaxation logic
  _prev_time = _time;

  // We know whether or not the nonlinear solver thinks it converged, but we need to see if the
  // executioner concurs
  if (lastSolveConverged())
  {
    _console << COLOR_GREEN << " Solve Converged!" << COLOR_DEFAULT << std::endl;

    if (_problem.haveXFEM() && (_xfem_update_count < _max_xfem_update) && _problem.updateMeshXFEM())
    {
      _console << "XFEM modifying mesh, repeating step" << std::endl;
      _xfem_repeat_step = true;
      ++_xfem_update_count;
    }
    else
    {
      if (_problem.haveXFEM())
      {
        _xfem_repeat_step = false;
        _xfem_update_count = 0;
        _console << "XFEM not modifying mesh, continuing" << std::endl;
      }

      if (_picard_max_its <= 1)
        _time_stepper->acceptStep();

      _sln_diff_norm = relativeSolutionDifferenceNorm();
      _solution_change_norm = _sln_diff_norm / _dt;

      _problem.onTimestepEnd();
      _problem.execute(EXEC_TIMESTEP_END);

      _problem.execTransfers(EXEC_TIMESTEP_END);
      _multiapps_converged = _problem.execMultiApps(EXEC_TIMESTEP_END, _picard_max_its == 1);

      if (!_multiapps_converged)
        return;
    }
  }
  else
  {
    _console << COLOR_RED << " Solve Did NOT Converge!" << COLOR_DEFAULT << std::endl;

    // Perform the output of the current, failed time step (this only occurs if desired)
    _problem.outputStep(EXEC_FAILED);
  }

  postSolve();
  _time_stepper->postSolve();

  if (_picard_max_its > 1 && lastSolveConverged())
  {
    _picard_timestep_end_norm = _problem.computeResidualL2Norm();

    _console << "Picard Norm after TIMESTEP_END MultiApps: " << _picard_timestep_end_norm << '\n';

    if (picardConverged())
    {
      _console << "Picard converged!" << std::endl;

      _picard_converged = true;
      _time_stepper->acceptStep();
      return;
    }
  }

  _dt = current_dt; // _dt might be smaller than this at this point for multistep methods
  _time = _time_old;
}

bool
Transient::picardConverged() const
{
  Real max_norm = std::max(_picard_timestep_begin_norm, _picard_timestep_end_norm);

  Real max_relative_drop = max_norm / _picard_initial_norm;

  return (max_norm < _picard_abs_tol || max_relative_drop < _picard_rel_tol);
}

void
Transient::endStep(Real input_time)
{
  if (input_time == -1.0)
    _time = _time_old + _dt;
  else
    _time = input_time;

  _picard_converged = false;

  _last_solve_converged = lastSolveConverged();

  if (_last_solve_converged && !_xfem_repeat_step)
  {
    _nl.getTimeIntegrator()->postStep();

    // Compute the Error Indicators and Markers
    _problem.computeIndicators();
    _problem.computeMarkers();

    // Perform the output of the current time step
    _problem.outputStep(EXEC_TIMESTEP_END);

    // output
    if (_time_interval && (_time + _timestep_tolerance >= _next_interval_output_time))
      _next_interval_output_time += _time_interval_output_interval;
  }
}

Real
Transient::computeConstrainedDT()
{
  //  // If start up steps are needed
  //  if (_t_step == 1 && _n_startup_steps > 1)
  //    _dt = _input_dt/(double)(_n_startup_steps);
  //  else if (_t_step == 1+_n_startup_steps && _n_startup_steps > 1)
  //    _dt = _input_dt;

  Real dt_cur = _dt;
  std::ostringstream diag;

  // After startup steps, compute new dt
  if (_t_step > _n_startup_steps)
    dt_cur = getDT();

  else
  {
    diag << "Timestep < n_startup_steps, using old dt: " << std::setw(9) << std::setprecision(6)
         << std::setfill('0') << std::showpoint << std::left << _dt << " tstep: " << _t_step
         << " n_startup_steps: " << _n_startup_steps << std::endl;
  }
  _unconstrained_dt = dt_cur;

  if (_verbose)
    _console << diag.str();

  diag.str("");
  diag.clear();

  // Allow the time stepper to limit the time step
  _at_sync_point = _time_stepper->constrainStep(dt_cur);

  // Don't let time go beyond next time interval output if specified
  if ((_time_interval) && (_time + dt_cur + _timestep_tolerance >= _next_interval_output_time))
  {
    dt_cur = _next_interval_output_time - _time;
    _at_sync_point = true;

    diag << "Limiting dt for time interval output at time: " << std::setw(9) << std::setprecision(6)
         << std::setfill('0') << std::showpoint << std::left << _next_interval_output_time
         << " dt: " << std::setw(9) << std::setprecision(6) << std::setfill('0') << std::showpoint
         << std::left << dt_cur << std::endl;
  }

  // Adjust to a target time if set
  if (_target_time > 0 && _time + dt_cur + _timestep_tolerance >= _target_time)
  {
    dt_cur = _target_time - _time;
    _at_sync_point = true;

    diag << "Limiting dt for target time: " << std::setw(9) << std::setprecision(6)
         << std::setfill('0') << std::showpoint << std::left << _next_interval_output_time
         << " dt: " << std::setw(9) << std::setprecision(6) << std::setfill('0') << std::showpoint
         << std::left << dt_cur << std::endl;
  }

  // Constrain by what the multi apps are doing
  Real multi_app_dt = _problem.computeMultiAppsDT(EXEC_TIMESTEP_BEGIN);
  if (_use_multiapp_dt || multi_app_dt < dt_cur)
  {
    dt_cur = multi_app_dt;
    _at_sync_point = false;
    diag << "Limiting dt for MultiApps: " << std::setw(9) << std::setprecision(6)
         << std::setfill('0') << std::showpoint << std::left << dt_cur << std::endl;
  }
  multi_app_dt = _problem.computeMultiAppsDT(EXEC_TIMESTEP_END);
  if (multi_app_dt < dt_cur)
  {
    dt_cur = multi_app_dt;
    _at_sync_point = false;
    diag << "Limiting dt for MultiApps: " << std::setw(9) << std::setprecision(6)
         << std::setfill('0') << std::showpoint << std::left << dt_cur << std::endl;
  }

  if (_verbose)
    _console << diag.str();

  return dt_cur;
}

Real
Transient::getDT()
{
  return _time_stepper->getCurrentDT();
}

bool
Transient::keepGoing()
{
  bool keep_going = !_problem.isSolveTerminationRequested();

  // Check for stop condition based upon steady-state check flag:
  if (lastSolveConverged() && !_xfem_repeat_step && _steady_state_detection == true &&
      _time > _steady_state_start_time)
  {
    // Check solution difference relative norm against steady-state tolerance
    if (_sln_diff_norm < _steady_state_tolerance)
    {
      _console << "Steady-State Solution Achieved at time: " << _time << std::endl;
      // Output last solve if not output previously by forcing it
      keep_going = false;
    }
    else // Keep going
    {
      // Update solution norm for next time step
      _old_time_solution_norm = _nl.currentSolution()->l2_norm();
      // Print steady-state relative error norm
      _console << "Steady-State Relative Differential Norm: " << _sln_diff_norm << std::endl;
    }
  }

  // Check for stop condition based upon number of simulation steps and/or solution end time:
  if (static_cast<unsigned int>(_t_step) > _num_steps)
    keep_going = false;

  if ((_time > _end_time) || (fabs(_time - _end_time) <= _timestep_tolerance))
    keep_going = false;

  if (!lastSolveConverged() && _abort)
  {
    _console << "Aborting as solve did not converge and input selected to abort" << std::endl;
    keep_going = false;
  }

  return keep_going;
}

void
Transient::estimateTimeError()
{
}

bool
Transient::lastSolveConverged()
{
  return _multiapps_converged && _time_stepper->converged();
}

void
Transient::preExecute()
{
  _time_stepper->preExecute();
}

void
Transient::postExecute()
{
  _time_stepper->postExecute();
}

void
Transient::setTargetTime(Real target_time)
{
  _target_time = target_time;
}

Real
Transient::getSolutionChangeNorm()
{
  return _solution_change_norm;
}

void
Transient::setupTimeIntegrator()
{
  if (_pars.isParamSetByUser("scheme") && _problem.hasTimeIntegrator())
    mooseError("You cannot specify time_scheme in the Executioner and independently add a "
               "TimeIntegrator to the system at the same time");

  if (!_problem.hasTimeIntegrator())
  {
    // backwards compatibility
    std::string ti_str;
    using namespace Moose;

    switch (_time_scheme)
    {
      case TI_IMPLICIT_EULER:
        ti_str = "ImplicitEuler";
        break;
      case TI_EXPLICIT_EULER:
        ti_str = "ExplicitEuler";
        break;
      case TI_CRANK_NICOLSON:
        ti_str = "CrankNicolson";
        break;
      case TI_BDF2:
        ti_str = "BDF2";
        break;
      case TI_EXPLICIT_MIDPOINT:
        ti_str = "ExplicitMidpoint";
        break;
      case TI_LSTABLE_DIRK2:
        ti_str = "LStableDirk2";
        break;
      case TI_EXPLICIT_TVD_RK_2:
        ti_str = "ExplicitTVDRK2";
        break;
      default:
        mooseError("Unknown scheme: ", _time_scheme);
        break;
    }

    InputParameters params = _app.getFactory().getValidParams(ti_str);
    _problem.addTimeIntegrator(ti_str, ti_str, params);
  }
}

std::string
Transient::getTimeStepperName()
{
  if (_time_stepper)
  {
    TimeStepper & ts = *_time_stepper;
    return demangle(typeid(ts).name());
  }
  else
    return std::string();
}

Real
Transient::relativeSolutionDifferenceNorm()
{
  const NumericVector<Number> & current_solution = *_nl.currentSolution();
  const NumericVector<Number> & old_solution = _nl.solutionOld();

  _sln_diff = current_solution;
  _sln_diff -= old_solution;

  return (_sln_diff.l2_norm() / current_solution.l2_norm());
}
