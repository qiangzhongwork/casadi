/*
 *    This file is part of CasADi.
 *
 *    CasADi -- A symbolic framework for dynamic optimization.
 *    Copyright (C) 2010 by Joel Andersson, Moritz Diehl, K.U.Leuven. All rights reserved.
 *
 *    CasADi is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU Lesser General Public
 *    License as published by the Free Software Foundation; either
 *    version 3 of the License, or (at your option) any later version.
 *
 *    CasADi is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    Lesser General Public License for more details.
 *
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with CasADi; if not, write to the Free Software
 *    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <interfaces/sundials/cvodes_integrator.hpp>
#include <interfaces/sundials/idas_integrator.hpp>
#include <interfaces/lapack/lapack_lu_dense.hpp>
#include <interfaces/superlu/superlu.hpp>
#include <casadi/stl_vector_tools.hpp>
#include <casadi/fx/simulator.hpp>
#include <casadi/fx/c_function.hpp>
#include <casadi/expression_tools.hpp>

#include <fstream>
#include <iostream>
#include <cassert>

using namespace CasADi;
using namespace std;

// Use CVodes or IDAS
const bool implicit_integrator = true;

// use plain c instead of SX
const bool plain_c = false;

// test adjoint sensitivities
const bool with_asens = true;

// use exact jacobian
const bool exact_jacobian = plain_c ? false : true;

// Calculate the forward sensitivities using finite differences
const bool finite_difference_fsens = !exact_jacobian;

// Calculate initial condition (for IDAS only)
const bool calc_ic = false;

// Perturb x or u
const bool perturb_u = true;

// Use a user_defined linear solver
const bool user_defined_solver = true;

// Use sparse direct solver (SuperLU)
const bool sparse_direct = true;

// The DAE residual in plain c (for IDAS)
void dae_res_c(double tt, const double *yy, const double* yydot, const double* pp, double* res){
  // Get the arguments
  double s = yy[0], v = yy[1], m = yy[2];
  double u = pp[0];
  double sdot = yydot[0], vdot = yydot[1], mdot = yydot[2];

  // Calculate the DAE residual
  res[0] = sdot - v;
  res[1] = vdot - (u-0.02*v*v)/m;
  res[2] = mdot - (-0.01*u*u);
}

// Wrap the function to allow creating an CasADi function
void dae_res_c_wrapper(CFunction &f, int fsens_order, int asens_order, void* user_data){
  if(fsens_order!=0 || asens_order!=0) throw CasadiException("this function does not contain derivative information");
  dae_res_c(f.getInputData(DAE_T)[0], &f.getInputData(DAE_Y)[0], &f.getInputData(DAE_YDOT)[0], &f.getInputData(DAE_P)[0], &f.getOutputData(DAE_RES)[0]);
}

// The ODE right-hand-side in plain c (for CVODES)
void ode_rhs_c(double tt, const double *yy, const double* pp, double* rhs){
  // Get the arguments
  double s = yy[0], v = yy[1], m = yy[2];
  double u = pp[0];

  // Calculate the DAE residual
  rhs[0] = v; // sdot
  rhs[1] = (u-0.02*v*v)/m; // vdot
  rhs[2] = (-0.01*u*u); // mdot
}

// Wrap the function to allow creating an CasADi function
void ode_rhs_c_wrapper(CFunction &f, int fsens_order, int asens_order, void* user_data){
  if(fsens_order!=0 || asens_order!=0) throw CasadiException("this function does not contain derivative information");
  ode_rhs_c(f.getInputData(ODE_T)[0], &f.getInputData(ODE_Y)[0], &f.getInputData(ODE_P)[0], &f.getOutputData(ODE_RHS)[0]);
}

// Create an IDAS instance (fully implicit integrator)
Integrator create_IDAS(){
  
  // Time 
  SX t("t");

  // Differential states
  SX s("s"), v("v"), m("m");
  vector<SX> y(3); 
  y[0] = s;
  y[1] = v;
  y[2] = m;
  
  // State derivatives
  SX sdot("sdot"), vdot("vdot"), mdot("mdot");
  vector<SX> ydot(3); 
  ydot[0] = sdot;
  ydot[1] = vdot;
  ydot[2] = mdot;

  // Control
  SX u("u");
  
  // Reference trajectory
  SX u_ref = 3-sin(t);
  
  // Square deviation from the state trajectory
  SX u_dev = u-u_ref;
  u_dev *= u_dev;
  
  // Differential equation (fully implicit form)
  vector<SX> res(3);
  res[0] = v - sdot;
  res[1] = (u-0.02*v*v)/m - vdot;
  res[2] = -0.01*u*u - mdot;

  // Input of the DAE residual function
  vector<vector<SX> > ffcn_in(DAE_NUM_IN);
  ffcn_in[DAE_T] = vector<SX>(1,t);
  ffcn_in[DAE_Y] = y;
  ffcn_in[DAE_YDOT] = ydot;
  ffcn_in[DAE_P] = vector<SX>(1,u);

  // DAE residual function
  FX ffcn = SXFunction(ffcn_in,res);
  ffcn.setOption("ad_order",1);

  // Overwrite ffcn with a plain c function (avoid this!)
  if(plain_c){
    // Use DAE residual defined in a c-function
    ffcn = CFunction(dae_res_c_wrapper);
    
    // Specify the number of inputs and outputs
    ffcn.setNumInputs(DAE_NUM_IN);
    ffcn.setNumOutputs(DAE_NUM_OUT);
    
    // Specify dimensions of inputs and outputs
    ffcn.input(DAE_T).setSize(1);
    ffcn.input(DAE_Y).setSize(3);
    ffcn.input(DAE_YDOT).setSize(3);
    ffcn.input(DAE_P).setSize(1);
    ffcn.output(DAE_RES).setSize(3);
  }
  
  // Quadrature function
  SXFunction qfcn(ffcn_in,u_dev);
  qfcn.setOption("ad_order",1);

  // Create an integrator
  Sundials::IdasIntegrator integrator(ffcn,qfcn);

  // Set IDAS specific options
  integrator.setOption("calc_ic",calc_ic);
  integrator.setOption("is_differential",vector<int>(3,1));

  // Formulate the Jacobian system
  if(user_defined_solver){
    // Assume we had an SX-function
    SXFunction f = shared_cast<SXFunction>(ffcn);
    assert(!f.isNull()); // make sure that the cast was successful
    
    // Get the Jacobian in the Newton iteration
    SX cj("cj");
    SXMatrix jac = f.jac(DAE_Y,DAE_RES) + cj*f.jac(DAE_YDOT,DAE_RES);
    
    // Jacobian function
    vector<vector<SX> > jac_in(Sundials::JAC_NUM_IN);
    jac_in[Sundials::JAC_T] = vector<SX>(1,t);
    jac_in[Sundials::JAC_Y] = y;
    jac_in[Sundials::JAC_YDOT] = ydot;
    jac_in[Sundials::JAC_P] = vector<SX>(1,u);
    jac_in[Sundials::JAC_CJ] = vector<SX>(1,cj);
    SXFunction J(jac_in,jac);
    
    // Create a linear solver (LAPACK LU or SuperLU)
    LinearSolver linsol;
    if(sparse_direct)
      linsol = SuperLU(jac.size1(),jac.size2(),jac.rowind,jac.col);
    else 
      linsol = LapackLUDense(jac.size1(),jac.size2(),jac.rowind,jac.col);
    
    // Pass to CVodes
    integrator.setLinearSolver(J,linsol);
  }

  
  
  // Return the integrator
  return integrator;
}

// Create an CVODES instance (ODE integrator)
Integrator create_CVODES(){
  
  // Time 
  SX t("t");

  // Differential states
  SX s("s"), v("v"), m("m");
  vector<SX> y(3); 
  y[0] = s;
  y[1] = v;
  y[2] = m;
  
  // Control
  SX u("u");
  
  // Reference trajectory
  SX u_ref = 3-sin(t);
  
  // Square deviation from the state trajectory
  SX u_dev = u-u_ref;
  u_dev *= u_dev;
  
  // Differential equation (fully implicit form)
  vector<SX> rhs(3);
  rhs[0] = v;
  rhs[1] = (u-0.02*v*v)/m;
  rhs[2] = -0.01*u*u;

  // Input of the DAE residual function
  vector<vector<SX> > ffcn_in(ODE_NUM_IN);
  ffcn_in[ODE_T] = vector<SX>(1,t);
  ffcn_in[ODE_Y] = y;
  ffcn_in[ODE_P] = vector<SX>(1,u);

  // DAE residual function
  FX ffcn = SXFunction(ffcn_in,rhs);
  ffcn.setOption("ad_order",1);

  // Overwrite ffcn with a plain c function (avoid this!)
  if(plain_c){
    // Use DAE residual defined in a c-function
    ffcn = CFunction(ode_rhs_c_wrapper);
    
    // Specify the number of inputs and outputs
    ffcn.setNumInputs(ODE_NUM_IN);
    ffcn.setNumOutputs(ODE_NUM_OUT);
    
    // Specify dimensions of inputs and outputs
    ffcn.input(ODE_T).setSize(1);
    ffcn.input(ODE_Y).setSize(3);
    ffcn.input(ODE_P).setSize(1);
    ffcn.output(ODE_RHS).setSize(3);
  }
  
  // Quadrature function
  SXFunction qfcn(ffcn_in,u_dev);
  qfcn.setOption("ad_order",1);

  // Create an integrator
  Sundials::CVodesIntegrator integrator(ffcn,qfcn);
  
  // Formulate the Jacobian system
  if(user_defined_solver){
    // Assume we had an SX-function
    SXFunction f = shared_cast<SXFunction>(ffcn);
    assert(!f.isNull()); // make sure that the cast was successful
    
    // Get the Jacobian in the Newton iteration
    SX gamma("gamma");
    SXMatrix jac = eye(3) - gamma * f.jac(ODE_Y,ODE_RHS);

    // Jacobian function
    vector<vector<SX> > jac_in(Sundials::M_NUM_IN);
    jac_in[Sundials::M_T] = vector<SX>(1,t);
    jac_in[Sundials::M_Y] = y;
    jac_in[Sundials::M_P] = vector<SX>(1,u);
    jac_in[Sundials::M_GAMMA] = vector<SX>(1,gamma);
    SXFunction M(jac_in,jac);
    
    // Create a linear solver (LAPACK LU)
    LinearSolver linsol;
    if(sparse_direct)
      linsol = SuperLU(jac.size1(),jac.size2(),jac.rowind,jac.col);
    else
      linsol = LapackLUDense(jac.size1(),jac.size2(),jac.rowind,jac.col);
    
    // Pass to CVodes
    integrator.setLinearSolver(M,linsol);
  }
  
  // Return the integrator
  return integrator;
}

int main(){
  // Time horizon
  double t0 = 0,  tf = 10;
  
  // Bounds on the control
  double u_lb = -0.5, u_ub = 1.3, u_init = 1;

  // Initial conditions
  vector<double> y0(3);
  y0[0] = 0;
  y0[1] = 0;
  y0[2] = 1;
  
  // Full state (includes quadratures)
  vector<double> x0=y0;
  x0.push_back(0);

  // Integrator
  Integrator integrator = implicit_integrator ? create_IDAS() : create_CVODES();
  
  // Set common integrator options
  integrator.setOption("ad_order",1);
  integrator.setOption("fsens_err_con",true);
  integrator.setOption("quad_err_con",true);
  integrator.setOption("abstol",1e-12);
  integrator.setOption("reltol",1e-12);
  integrator.setOption("fsens_abstol",1e-6);
  integrator.setOption("fsens_reltol",1e-6);
  integrator.setOption("asens_abstol",1e-6);
  integrator.setOption("asens_reltol",1e-6);
  integrator.setOption("exact_jacobian",exact_jacobian);
  integrator.setOption("finite_difference_fsens",finite_difference_fsens);
  integrator.setOption("max_num_steps",100000);
  
 if(user_defined_solver)
   integrator.setOption("linear_solver","user_defined");

  // Initialize the integrator
  integrator.init();
  
  // Set time horizon
  integrator.setInput(t0,INTEGRATOR_T0);
  integrator.setInput(tf,INTEGRATOR_TF);
 
  // Set parameters
  integrator.setInput(u_init,INTEGRATOR_P);
  
  // Set inital state
  integrator.setInput(x0,INTEGRATOR_X0);
  
  // Set initial state derivative (if not to be calculated)
  if(!calc_ic){
    double yp0[] = {0,1,-0.01,0};
    integrator.setInput(yp0,INTEGRATOR_XP0);
  }
  
  // Integrate
  integrator.evaluate();

  // Save the result
  vector<double> res0 = integrator.getOutputData();

  // Perturb in some direction
  if(perturb_u){
    double u_pert = u_init + 0.01;
    integrator.setInput(u_pert,INTEGRATOR_P);
  } else {
    vector<double> x_pert = x0;
    x_pert[1] += 0.01;
    integrator.setInput(x_pert,INTEGRATOR_X0);
  }
  
  // Integrate again
  integrator.evaluate();
  
  // Print statistics
  integrator.printStats();

  // Calculate finite difference approximation
  vector<double> fd = integrator.getOutputData();
  for(int i=0; i<fd.size(); ++i){
    fd[i] -= res0[i];
    fd[i] /= 0.01;
  }
  
  cout << "unperturbed                     " << res0 << endl;
  cout << "perturbed                       " << integrator.getOutputData() << endl;
  cout << "finite_difference approximation " << fd << endl;

  if(perturb_u){
    double u_seed = 1;
    integrator.setFwdSeed(u_seed,INTEGRATOR_P);
  } else {
    vector<double> x0_seed(x0.size(),0);
    x0_seed[1] = 1;
    integrator.setFwdSeed(x0_seed,INTEGRATOR_X0);
  }
  
  // Reset parameters
  integrator.setInput(u_init,INTEGRATOR_P);
  
  // Reset initial state
  integrator.setInput(x0,INTEGRATOR_X0);

  // forward seeds
  integrator.setFwdSeed(0.0,INTEGRATOR_T0);
  integrator.setFwdSeed(0.0,INTEGRATOR_TF);

  if(with_asens){
    // backward seeds
    vector<double> &bseed = integrator.getAdjSeedData(INTEGRATOR_XF);
    fill(bseed.begin(),bseed.end(),0);
    bseed[0] = 1;

    // evaluate with forward and adjoint sensitivities
    integrator.evaluate(1,1);
  } else {
    // evaluate with only forward sensitivities
    integrator.evaluate(1,0);
  }
    
  vector<double> &fsens = integrator.getFwdSensData();
  cout << "forward sensitivities           " << fsens << endl;

  if(with_asens){
    cout << "adjoint sensitivities           ";
    cout << integrator.getAdjSensData(INTEGRATOR_T0) << "; ";
    cout << integrator.getAdjSensData(INTEGRATOR_TF) << "; ";
    cout << integrator.getAdjSensData(INTEGRATOR_X0) << "; ";
    cout << integrator.getAdjSensData(INTEGRATOR_P) << "; ";
    cout << endl;
  }
  
  return 0;
  }

#if 0

//  vector<double> vv(1);
//  setv(0,vv);
  
  // Create a grid
  int ntimes = 20;
  // The desired output times
  vector<double> t_out(ntimes);
  linspace(t_out,0,10);
  
  // Create a simulator
  Simulator simulator(integrator,t_out);
  simulator.setOption("name","rocket simulator");
  simulator.setOption("ad_order",1);
  
  cout << "Simulator:" << endl;
  simulator.printOptions();
  
  // Initialize the simulator to allow evaluation
  simulator.init();
  
  // Pass initial condition
  simulator.setInput(x0,SIMULATOR_X0);

  // Pass parameter values
  simulator.setInput(u_init,SIMULATOR_P);
  
  // Integrate
  simulator.evaluate();
  
  // Print to screen
  vector<double> xout(x0.size()*ntimes);
  simulator.getOutput(xout);
  cout << "xout = " << xout << endl;

  // Finite difference approximation
  simulator.setInput(u_init+0.01,SIMULATOR_P);
  simulator.evaluate();
  vector<double> fd(xout.size());
  simulator.getOutput(fd);
  for(int i=0; i<xout.size(); ++i){
    fd[i] -= xout[i];
    fd[i] /= 0.01;
  }
  cout << endl << "finite difference approximation =   " << fd << endl;
  
  // Calculate internally with forward sensitivities
  simulator.setFwdSeed(1.0,SIMULATOR_P);
  
  // Integrate with forward sensitivities
  simulator.evaluate(1,0);
  
  // Print to screen
  vector<double> xsens(x0.size()*ntimes);
  simulator.getFwdSens(xsens);
  cout << endl << "forward sensitivity result = " << xsens << endl;

  // Pass backward seeds
  vector<double> bsens(xsens.size(),0.0);
  bsens[x0.size()*(ntimes-1)+1] = 1;
  simulator.setAdjSeed(bsens);

  // Integrate backwards
  simulator.evaluate(0,1);
  
  // Print results
  cout << "adjoint sensitivity result (p): " << simulator.input(SIMULATOR_P).data() << endl;
  cout << "adjoint sensitivity result (x0): "<< simulator.input(SIMULATOR_X0).data() << endl;
  
  
  #if 0
  // Parametrize controls into 20 uniform intervals
  Matrix u_disc = parametrizeControls(ocp,u,20);
//  Matrix udisc_guess = 2*ones(20,1);
//  ocp.guessSolution(u_disc, udisc_guess);


  // numeric
  vector<double> t_out_num(nt_out);
  for(int i=0; i<nt_out; ++i)
    t_out_num[i] = i*10/double(nt_out-1);
 
  // LAGRANGE TERM
//  int l_ind = ocp.addOutput(t_out, Matrix(), u*u, 1, u_disc); // quadrature and sensitivities, first order
//  int l_ind = ocp.addOutput(10, Matrix(), u*u); // just quadrature 
//  int l_ind = ocp.addOutput(10, 100*(1-m),Matrix()); // just quadrature 
  int l_ind = ocp.addOutput(10, m, Matrix(), 1, u_disc); // quadrature and sensitivities, first order

  // forward sensitivities of v with respect to u at time tf
//   ocp.addOutput(10, v, Matrix(), 1, u_disc);

  
  // Set output function
  int oind_s = ocp.addOutput(t_out, s);
  int oind_v = ocp.addOutput(t_out, v);
  int oind_m = ocp.addOutput(t_out, m);
  int oind_u = ocp.addOutput(t_out, u);

  // Eliminate dependent variables from the functions
  eliminateDependent(ocp);

  // Print to screen
  std::cout << ocp;

  // Allocate an integrator
  CvodesIntegrator integrator(ocp);

  // Print the possible options
  integrator->printOptions();

  // Set the linear solver
   integrator->setOption("linear_solver","dense");
//  integrator->setOption("linear_solver","band");
//  integrator->setOption("linear_solver","sparse");

  // Use exact jacobian
  integrator->setOption("exact_jacobian",false);

  // Upper and lower band-widths (only relevant when using a band linear solver)
//   integrator->setOption("mupper",1);
//   integrator->setOption("mlower",1);

  // set tolerances 
  integrator->setOption("reltol",1e-6);
  integrator->setOption("abstol",1e-8);

  // Initialize the integrator
  integrator->init();
  std::cout << "initialized" << std::endl;

  // Integrate once for visualization
  integrator->evaluate();
  std::cout << "solved" << std::endl;

  // Create a file for saving the results
  std::ofstream resfile;
  resfile.open ("results_rocket.txt");



  // Save results to file
  resfile << "t_out " << t_out_num << std::endl;
  resfile << "s " << integrator->output[oind_s].data() << endl;
  resfile << "v " << integrator->output[oind_v].data() << endl;
  resfile << "m " << integrator->output[oind_m].data() << endl;
  resfile << "u " << integrator->output[oind_u].data() << endl;
  resfile << "lfun " << integrator->output[l_ind].data() << endl;
  cout    << "lfun " << integrator->output[l_ind].data() << endl;

  // Close the results file
  resfile.close();

  return 0;
} catch (const char * str){
  std::cerr << str << std::endl;
  return 1;
}
#endif

}

#endif

