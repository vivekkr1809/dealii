/* $Id$ */
/* Author: Martin Kronbichler, Uppsala University,
   Wolfgang Bangerth, Texas A&M University 2007, 2008 */

/*    $Id$       */
/*                                                                */
/*    Copyright (C) 2007, 2008 by the deal.II authors */
/*                                                                */
/*    This file is subject to QPL and may not be  distributed     */
/*    without copyright and license information. Please refer     */
/*    to the file deal.II/doc/license.html for the  text  and     */
/*    further information on this license.                        */

				 // @sect3{Include files}

				 // The first step, as always, is to include
				 // the functionality of these well-known
				 // deal.II library files and some C++ header
				 // files.
#include <base/quadrature_lib.h>
#include <base/logstream.h>
#include <base/utilities.h>

#include <lac/full_matrix.h>
#include <lac/solver_gmres.h>
#include <lac/solver_cg.h>

#include <grid/tria.h>
#include <grid/grid_generator.h>
#include <grid/tria_accessor.h>
#include <grid/tria_iterator.h>
#include <grid/grid_tools.h>
#include <grid/grid_refinement.h>

#include <dofs/dof_handler.h>
#include <dofs/dof_renumbering.h>
#include <dofs/dof_accessor.h>
#include <dofs/dof_tools.h>
#include <dofs/dof_constraints.h>

#include <fe/fe_q.h>
#include <fe/fe_system.h>
#include <fe/fe_values.h>

#include <numerics/vectors.h>
#include <numerics/data_out.h>
#include <numerics/error_estimator.h>
#include <numerics/solution_transfer.h>

				 // Then we need to include some header files
				 // that provide vector, matrix, and
				 // preconditioner classes that implement
				 // interfaces to the respective Trilinos
				 // classes. In particular, we will need
				 // interfaces to the matrix and vector
				 // classes based on Trilinos as well as
				 // Trilinos preconditioners:
#include <lac/trilinos_sparse_matrix.h>
#include <lac/trilinos_block_sparse_matrix.h>
#include <lac/trilinos_vector.h>
#include <lac/trilinos_block_vector.h>
#include <lac/trilinos_precondition.h>

				 // Finally, here are two C++ headers that
				 // haven't been included yet by one of the
				 // aforelisted header files:
#include <fstream>
#include <sstream>


				 // At the end of this top-matter, we import
				 // all deal.II names into the global
				 // namespace:
using namespace dealii;


				 // @sect3{Equation data}

				 // Again, the next stage in the program is
				 // the definition of the equation data, that
				 // is, the various boundary conditions, the
				 // right hand sides and the initial condition
				 // (remember that we're about to solve a
				 // time-dependent system). The basic strategy
				 // for this definition is the same as in
				 // step-22. Regarding the details, though,
				 // there are some differences.

				 // The first thing is that we don't set any
				 // non-homogenous boundary conditions on the
				 // velocity, since as is explained in the
				 // introduction we will use no-flux
				 // conditions
				 // $\mathbf{n}\cdot\mathbf{u}=0$. So what is
				 // left are <code>dim-1</code> conditions for
				 // the tangential part of the normal
				 // component of the stress tensor,
				 // $\textbf{n} \cdot [p \textbf{1} -
				 // \eta\varepsilon(\textbf{u})]$; we assume
				 // homogenous values for these components,
				 // i.e. a natural boundary condition that
				 // requires no specific action (it appears as
				 // a zero term in the right hand side of the
				 // weak form).
				 //
				 // For the temperature <i>T</i>, we assume no
				 // thermal energy flux, i.e. $\mathbf{n}
				 // \cdot \kappa \nabla T=0$. This, again, is
				 // a boundary condition that does not require
				 // us to do anything in particular.
				 //
				 // Secondly, we have to set initial
				 // conditions for the temperature (no initial
				 // conditions are required for the velocity
				 // and pressure, since the Stokes equations
				 // for the quasi-stationary case we consider
				 // here have no time derivatives of the
				 // velocity or pressure). Here, we choose a
				 // very simple test case, where the initial
				 // temperature is zero, and all dynamics are
				 // driven by the temperature right hand side.
				 //
				 // Thirdly, we need to define the right hand
				 // side of the temperature equation. We
				 // choose it to be constant within three
				 // circles (or spheres in 3d) somewhere at
				 // the bottom of the domain, as explained in
				 // the introduction, and zero outside.
				 // 
				 // Finally, or maybe firstly, at the top of
				 // this namespace, we define the various
				 // material constants we need ($\eta,\kappa$
				 // and the Rayleigh number $Ra$):
namespace EquationData
{
  const double eta = 1;
  const double kappa = 1e-6;
  const double Rayleigh_number = 10;


  template <int dim>
  class TemperatureInitialValues : public Function<dim>
  {
    public:
      TemperatureInitialValues () : Function<dim>(1) {}

      virtual double value (const Point<dim>   &p,
			    const unsigned int  component = 0) const;

      virtual void vector_value (const Point<dim> &p,
				 Vector<double>   &value) const;
  };


  template <int dim>
  double
  TemperatureInitialValues<dim>::value (const Point<dim>  &,
					const unsigned int) const
  {
    return 0;
  }


  template <int dim>
  void
  TemperatureInitialValues<dim>::vector_value (const Point<dim> &p,
					       Vector<double>   &values) const
  {
    for (unsigned int c=0; c<this->n_components; ++c)
      values(c) = TemperatureInitialValues<dim>::value (p, c);
  }


  template <int dim>
  class TemperatureRightHandSide : public Function<dim>
  {
    public:
      TemperatureRightHandSide () : Function<dim>(1) {}

      virtual double value (const Point<dim>   &p,
			    const unsigned int  component = 0) const;

      virtual void vector_value (const Point<dim> &p,
				 Vector<double>   &value) const;
  };


  template <int dim>
  double
  TemperatureRightHandSide<dim>::value (const Point<dim>  &p,
					const unsigned int component) const
  {
    Assert (component == 0,
	    ExcMessage ("Invalid operation for a scalar function."));
    
    Assert ((dim==2) || (dim==3), ExcNotImplemented());
    
    static const Point<dim> source_centers[3]
      = { (dim == 2 ? Point<dim>(.3,.1) : Point<dim>(.3,.5,.1)),
	  (dim == 2 ? Point<dim>(.45,.1) : Point<dim>(.45,.5,.1)),
	  (dim == 2 ? Point<dim>(.75,.1) : Point<dim>(.75,.5,.1)) };
    static const double source_radius
      = (dim == 2 ? 1./32 : 1./8);
      
    return ((source_centers[0].distance (p) < source_radius)
	    ||
	    (source_centers[1].distance (p) < source_radius)
	    ||
	    (source_centers[2].distance (p) < source_radius)
	    ?
	    1
	    :
	    0);
  }


  template <int dim>
  void
  TemperatureRightHandSide<dim>::vector_value (const Point<dim> &p,
					       Vector<double>   &values) const
  {
    for (unsigned int c=0; c<this->n_components; ++c)
      values(c) = TemperatureRightHandSide<dim>::value (p, c);
  }
}



				 // @sect3{Linear solvers and preconditioners}

				 // This section introduces some objects
				 // that are used for the solution of the
				 // linear equations of the Stokes system
				 // that we need to solve in each time
				 // step. Many of the ideas used here are
				 // the same as in step-20, where Schur
				 // complement based preconditioners and
				 // solvers have been introduced, with the
				 // actual interface taken from step-22 (in
				 // particular the discussion in the
				 // "Results" section of step-22, in which
				 // we introduce alternatives to the direct
				 // Schur complement approach). Note,
				 // however, that here we don't use the
				 // Schur complement to solve the Stokes
				 // equations, though an approximate Schur
				 // complement (the mass matrix on the
				 // pressure space) appears in the
				 // preconditioner.
namespace LinearSolvers
{

				   // @sect4{The <code>InverseMatrix</code> class template}

				   // This class is an interface to
				   // calculate the action of an
				   // "inverted" matrix on a vector
				   // (using the <code>vmult</code>
				   // operation) in the same way as
				   // the corresponding class in
				   // step-22: when the product of an
				   // object of this class is
				   // requested, we solve a linear
				   // equation system with that matrix
				   // using the CG method, accelerated
				   // by a preconditioner of
				   // (templated) class
				   // <code>Preconditioner</code>.
				   //
				   // In a minor deviation from the
				   // implementation of the same class in
				   // step-22 (and step-20), we make the
				   // <code>vmult</code> function take any
				   // kind of vector type (it will yield
				   // compiler errors, however, if the matrix
				   // does not allow a matrix-vector product
				   // with this kind of vector).
				   //
				   // Secondly, we catch any exceptions that
				   // the solver may have thrown. The reason
				   // is as follows: When debugging a program
				   // like this one occasionally makes a
				   // mistake of passing an indefinite or
				   // non-symmetric matrix or preconditioner
				   // to the current class. The solver will,
				   // in that case, not converge and throw a
				   // run-time exception. If not caught here
				   // it will propagate up the call stack and
				   // may end up in <code>main()</code> where
				   // we output an error message that will say
				   // that the CG solver failed. The question
				   // then becomes: Which CG solver? The one
				   // that inverted the mass matrix? The one
				   // that inverted the top left block with
				   // the Laplace operator? Or a CG solver in
				   // one of the several other nested places
				   // where we use linear solvers in the
				   // current code? No indication about this
				   // is present in a run-time exception
				   // because it doesn't store the stack of
				   // calls through which we got to the place
				   // where the exception was generated.
				   //
				   // So rather than letting the exception
				   // propagate freely up to
				   // <code>main()</code> we realize that
				   // there is little that an outer function
				   // can do if the inner solver fails and
				   // rather convert the run-time exception
				   // into an assertion that fails and
				   // triggers a call to <code>abort()</code>,
				   // allowing us to trace back in a debugger
				   // how we got to the current place.
  template <class Matrix, class Preconditioner>
  class InverseMatrix : public Subscriptor
  {
    public:
      InverseMatrix (const Matrix         &m,
		     const Preconditioner &preconditioner);


      template <typename VectorType>
      void vmult (VectorType       &dst,
		  const VectorType &src) const;

    private:
      const SmartPointer<const Matrix> matrix;
      const Preconditioner &preconditioner;
  };


  template <class Matrix, class Preconditioner>
  InverseMatrix<Matrix,Preconditioner>::
  InverseMatrix (const Matrix &m,
		 const Preconditioner &preconditioner)
		  :
		  matrix (&m),
		  preconditioner (preconditioner)
  {}



  template <class Matrix, class Preconditioner>
  template <typename VectorType>
  void
  InverseMatrix<Matrix,Preconditioner>::
  vmult (VectorType       &dst,
	 const VectorType &src) const
  {
    SolverControl solver_control (src.size(), 1e-7*src.l2_norm());
    SolverCG<VectorType> cg (solver_control);

    dst = 0;

    try
      {
	cg.solve (*matrix, dst, src, preconditioner);
      }
    catch (std::exception &e)
      {
	Assert (false, ExcMessage(e.what()));
      }
  }

				   // @sect4{Schur complement preconditioner}

				   // This is the implementation of
				   // the Schur complement
				   // preconditioner as described in
				   // detail in the introduction. As
				   // opposed to step-20 and step-22,
				   // we solve the block system
				   // all-at-once using GMRES, and use
				   // the Schur complement of the
				   // block structured matrix to build
				   // a good preconditioner instead.
				   //
				   // Let's have a look at the ideal
				   // preconditioner matrix
				   // $P=\left(\begin{array}{cc} A & 0 \\ B &
				   // -S \end{array}\right)$
				   // described in the introduction. If
				   // we apply this matrix in the
				   // solution of a linear system,
				   // convergence of an iterative
				   // GMRES solver will be
				   // governed by the matrix
				   // @f{eqnarray*}
				   // P^{-1}\left(\begin{array}{cc} A
				   // & B^T \\ B & 0
				   // \end{array}\right) =
				   // \left(\begin{array}{cc} I &
				   // A^{-1} B^T \\ 0 & 0
				   // \end{array}\right), @f} 
				   //
				   // which indeed is very simple. A
				   // GMRES solver based on exact
				   // matrices would converge in two
				   // iterations, since there are only
				   // two distinct eigenvalues. Such
				   // a preconditioner for the blocked
				   // Stokes system has been proposed
				   // by Silvester and Wathen ("Fast
				   // iterative solution of stabilised
				   // Stokes systems part II.  Using
				   // general block preconditioners",
				   // SIAM J. Numer. Anal., 31 (1994),
				   // pp. 1352-1367).
				   // 
				   // Replacing <i>P</i> by
				   // $\tilde{P}$ does not change the
				   // situation dramatically. The
				   // product $P^{-1} A$ will still be
				   // close to a matrix with
				   // eigenvalues 0 and 1, which lets
				   // us hope to be able to get a
				   // number of GMRES iterations that
				   // does not depend on the problem
				   // size.
				   //
				   // The deal.II users who have already gone
				   // through the step-20 and step-22
				   // tutorials can certainly imagine how
				   // we're going to implement this.  We
				   // replace the exact inverse matrices in
				   // $P^{-1}$ by some approximate inverses
				   // built from the InverseMatrix class, and
				   // the inverse Schur complement will be
				   // approximated by the pressure mass matrix
				   // $M_p$ (weighted by $\eta^{-1}$ as
				   // mentioned in the introduction). As
				   // pointed out in the results section of
				   // step-22, we can replace the exact
				   // inverse of <i>A</i> by just the
				   // application of a preconditioner, in this
				   // case on a vector Laplace matrix as was
				   // explained in the introduction. This does
				   // increase the number of (outer) GMRES
				   // iterations, but is still significantly
				   // cheaper than an exact inverse, which
				   // would require between 20 and 35 CG
				   // iterations for <em>each</em> outer
				   // solver step (using the AMG
				   // preconditioner).
				   // 
				   // Having the above explanations in
				   // mind, we define a preconditioner
				   // class with a <code>vmult</code>
				   // functionality, which is all we
				   // need for the interaction with
				   // the usual solver functions
				   // further below in the program
				   // code.
				   // 
				   // First the declarations. These
				   // are similar to the definition of
				   // the Schur complement in step-20,
				   // with the difference that we need
				   // some more preconditioners in the
				   // constructor and that the
				   // matrices we use here are built
				   // upon Trilinos:
  template <class PreconditionerA, class PreconditionerMp>
  class BlockSchurPreconditioner : public Subscriptor
  {
    public:
      BlockSchurPreconditioner (
	const TrilinosWrappers::BlockSparseMatrix     &S,
	const InverseMatrix<TrilinosWrappers::SparseMatrix,
     	                    PreconditionerMp>         &Mpinv,
	const PreconditionerA                         &Apreconditioner);

      void vmult (TrilinosWrappers::BlockVector       &dst,
		  const TrilinosWrappers::BlockVector &src) const;

    private:
      const SmartPointer<const TrilinosWrappers::BlockSparseMatrix> stokes_matrix;
      const SmartPointer<const InverseMatrix<TrilinosWrappers::SparseMatrix,
					     PreconditionerMp > > m_inverse;
      const PreconditionerA &a_preconditioner;

      mutable TrilinosWrappers::Vector tmp;
  };



  template <class PreconditionerA, class PreconditionerMp>
  BlockSchurPreconditioner<PreconditionerA, PreconditionerMp>::
  BlockSchurPreconditioner(const TrilinosWrappers::BlockSparseMatrix  &S,
			   const InverseMatrix<TrilinosWrappers::SparseMatrix,
			                       PreconditionerMp>      &Mpinv,
			   const PreconditionerA                      &Apreconditioner)
		  :
		  stokes_matrix           (&S),
		  m_inverse               (&Mpinv),
		  a_preconditioner        (Apreconditioner),
		  tmp                     (stokes_matrix->block(1,1).matrix->RowMap())
  {}


				   // Next is the <code>vmult</code>
				   // function. We implement the action of
				   // $P^{-1}$ as described above in three
				   // successive steps.  In formulas, we want
				   // to compute $Y=P^{-1}X$ where $X,Y$ are
				   // both vectors with two block components.
				   //
				   // The first step multiplies the velocity
				   // part of the vector by a preconditioner
				   // of the matrix <i>A</i>, i.e. we compute
				   // $Y_0={\tilde A}^{-1}X_0$.  The resulting
				   // velocity vector is then multiplied by
				   // $B$ and subtracted from the pressure,
				   // i.e. we want to compute $X_1-BY_0$.
				   // This second step only acts on the
				   // pressure vector and is accomplished by
				   // the residual function of our matrix
				   // classes, except that the sign is
				   // wrong. Consequently, we change the sign
				   // in the temporary pressure vector and
				   // finally multiply by the inverse pressure
				   // mass matrix to get the final pressure
				   // vector, completing our work on the
				   // Stokes preconditioner:
  template <class PreconditionerA, class PreconditionerMp>
  void BlockSchurPreconditioner<PreconditionerA, PreconditionerMp>::vmult (
    TrilinosWrappers::BlockVector       &dst,
    const TrilinosWrappers::BlockVector &src) const
  {
    a_preconditioner.vmult (dst.block(0), src.block(0));
    stokes_matrix->block(1,0).residual(tmp, dst.block(0), src.block(1));
    tmp *= -1;
    m_inverse->vmult (dst.block(1), tmp);
  }
}



				 // @sect3{The <code>BoussinesqFlowProblem</code> class template}

				 // The definition of the class that defines
				 // the top-level logic of solving the
				 // time-dependent Boussinesq problem is
				 // mainly based on the step-22 tutorial
				 // program. The main differences are that now
				 // we also have to solve for the temperature
				 // equation, which forces us to have a second
				 // DoFHandler object for the temperature
				 // variable as well as matrices, right hand
				 // sides, and solution vectors for the
				 // current and previous time steps. As
				 // mentioned in the introduction, all linear
				 // algebra objects are going to use wrappers
				 // of the corresponding Trilinos
				 // functionality.
				 //
				 // The member functions of this class are
				 // reminiscent of step-21, where we also used
				 // a staggered scheme that first solve the
				 // flow equations (here the Stokes equations,
				 // in step-21 Darcy flow) and then update
				 // the advected quantity (here the
				 // temperature, there the saturation). The
				 // functions that are new are mainly
				 // concerned with determining the time step,
				 // as well as the proper size of the
				 // artificial viscosity stabilization.
				 //
				 // The last three variables indicate whether
				 // the various matrices or preconditioners
				 // need to be rebuilt the next time the
				 // corresponding build functions are
				 // called. This allows us to move the
				 // corresponding <code>if</code> into the
				 // respective function and thereby keeping
				 // our main <code>run()</code> function clean
				 // and easy to read.
template <int dim>
class BoussinesqFlowProblem
{
  public:
    BoussinesqFlowProblem ();
    void run ();

  private:
    void setup_dofs ();
    void assemble_stokes_preconditioner ();
    void build_stokes_preconditioner ();
    void assemble_stokes_system ();
    void assemble_temperature_system ();
    void assemble_temperature_matrix ();
    double get_maximal_velocity () const;
    std::pair<double,double> get_extrapolated_temperature_range () const;
    void solve ();
    void output_results () const;
    void refine_mesh (const unsigned int max_grid_level);

    static
    double
    compute_viscosity(const std::vector<double>          &old_temperature,
		      const std::vector<double>          &old_old_temperature,
		      const std::vector<Tensor<1,dim> >  &old_temperature_grads,
		      const std::vector<Tensor<1,dim> >  &old_old_temperature_grads,
		      const std::vector<Tensor<2,dim> >  &old_temperature_hessians,
		      const std::vector<Tensor<2,dim> >  &old_old_temperature_hessians,
		      const std::vector<Vector<double> > &present_stokes_values,
		      const std::vector<double>          &gamma_values,
		      const double                        global_u_infty,
		      const double                        global_T_variation,
		      const double                        global_Omega_diameter,
		      const double                        cell_diameter,
		      const double                        old_time_step);


    Triangulation<dim>                  triangulation;

    const unsigned int                  stokes_degree;
    FESystem<dim>                       stokes_fe;
    DoFHandler<dim>                     stokes_dof_handler;
    ConstraintMatrix                    stokes_constraints;

    std::vector<unsigned int>           stokes_block_sizes;
    TrilinosWrappers::BlockSparseMatrix stokes_matrix;
    TrilinosWrappers::BlockSparseMatrix stokes_preconditioner_matrix;

    TrilinosWrappers::BlockVector       stokes_solution;
    TrilinosWrappers::BlockVector       stokes_rhs;


    const unsigned int                  temperature_degree;    
    FE_Q<dim>                           temperature_fe;
    DoFHandler<dim>                     temperature_dof_handler;
    ConstraintMatrix                    temperature_constraints;

    TrilinosWrappers::SparseMatrix      temperature_mass_matrix;
    TrilinosWrappers::SparseMatrix      temperature_stiffness_matrix;
    TrilinosWrappers::SparseMatrix      temperature_matrix;

    TrilinosWrappers::Vector            temperature_solution;
    TrilinosWrappers::Vector            old_temperature_solution;
    TrilinosWrappers::Vector            old_old_temperature_solution;
    TrilinosWrappers::Vector            temperature_rhs;


    double time_step;
    double old_time_step;
    unsigned int timestep_number;

    boost::shared_ptr<TrilinosWrappers::PreconditionAMG> Amg_preconditioner;
    boost::shared_ptr<TrilinosWrappers::PreconditionIC>  Mp_preconditioner;

    bool rebuild_stokes_matrix;
    bool rebuild_temperature_matrices;
    bool rebuild_stokes_preconditioner;
};


				 // @sect3{BoussinesqFlowProblem class implementation}

				 // @sect4{BoussinesqFlowProblem::BoussinesqFlowProblem}
				 // 
				 // The constructor of this class is an
				 // extension of the constructor in
				 // step-22. We need to add the various
				 // variables that concern the temperature. As
				 // discussed in the introduction, we are
				 // going to use $Q_2\times Q_1$ (Taylor-Hood)
				 // elements again for the Stokes part, and
				 // $Q_2$ elements for the
				 // temperature. However, by using variables
				 // that store the polynomial degree of the
				 // Stokes and temperature finite elements, it
				 // is easy to consistently modify the degree
				 // of the elements as well as all quadrature
				 // formulas used on them
				 // downstream. Moreover, we initialize the
				 // time stepping as well as the options for
				 // matrix assembly and preconditioning:
template <int dim>
BoussinesqFlowProblem<dim>::BoussinesqFlowProblem ()
                :
		triangulation (Triangulation<dim>::maximum_smoothing),

                stokes_degree (1),
                stokes_fe (FE_Q<dim>(stokes_degree+1), dim,
			   FE_Q<dim>(stokes_degree), 1),
		stokes_dof_handler (triangulation),

		temperature_degree (2),
		temperature_fe (temperature_degree),
                temperature_dof_handler (triangulation),

                time_step (0),
		old_time_step (0),
		timestep_number (0),
		rebuild_stokes_matrix (true),
		rebuild_temperature_matrices (true),
		rebuild_stokes_preconditioner (true)
{}



				 // @sect4{BoussinesqFlowProblem::get_maximal_velocity}

				 // Starting the real functionality of this
				 // class is a helper function that determines
				 // the maximum ($L_\infty$) velocity in the
				 // domain (at the quadrature points, in
				 // fact). How it works should be relatively
				 // obvious to all who have gotten to this
				 // point of the tutorial.
				 //
				 // The only point worth thinking about a bit
				 // is how to choose the quadrature points we
				 // use here. Since the goal of this function
				 // is to find the maximal velocity over a
				 // domain by looking at quadrature points on
				 // each cell. So we should ask how we should
				 // best choose these quadrature points on
				 // each cell. To this end, recall that if we
				 // had a single $Q_1$ field (rather than the
				 // vector-valued field of higher order) then
				 // the maximum would be attained at a vertex
				 // of the mesh. In other words, we should use
				 // the QTrapez class that has quadrature
				 // points only at the vertices of cells.
				 //
				 // For higher order shape functions, the
				 // situation is more complicated: the maxima
				 // and minima may be attained at points
				 // between the support points of shape
				 // functions (for the usual $Q_p$ elements
				 // the support points are the equidistant
				 // Lagrange interpolation points);
				 // furthermore, since we are looking for the
				 // maximum magnitude of a vector-valued
				 // quantity, we can even less say with
				 // certainty where the set of potential
				 // maximal points are. Nevertheless,
				 // intuitively if not provably, the Lagrange
				 // interpolation points appear to be a better
				 // choice than the Gauss points.
				 //
				 // There are now different methods to produce
				 // a quadrature formula with quadrature
				 // points equal to the interpolation points
				 // of the finite element. One option would be
				 // to use the
				 // FiniteElement::get_unit_support_points()
				 // function, reduce the output to a unique
				 // set of points to avoid duplicate function
				 // evaluations, and create a Quadrature
				 // object using these points. Another option,
				 // chosen here, is to use the QTrapez class
				 // and combine it with the QIterated class
				 // that repeats the QTrapez formula on a
				 // number of sub-cells in each coordinate
				 // direction. To cover all support points, we
				 // need to iterate it
				 // <code>stokes_degree+1</code> times since
				 // this is the polynomial degree of the
				 // Stokes element in use:
template <int dim>
double BoussinesqFlowProblem<dim>::get_maximal_velocity () const
{
  const QIterated<dim> quadrature_formula (QTrapez<1>(),
					   stokes_degree+1);
  const unsigned int n_q_points = quadrature_formula.size();

  FEValues<dim> fe_values (stokes_fe, quadrature_formula, update_values);
  std::vector<Vector<double> > stokes_values(n_q_points,
					     Vector<double>(dim+1));
  double max_velocity = 0;

  typename DoFHandler<dim>::active_cell_iterator
    cell = stokes_dof_handler.begin_active(),
    endc = stokes_dof_handler.end();
  for (; cell!=endc; ++cell)
    {
      fe_values.reinit (cell);
      fe_values.get_function_values (stokes_solution, stokes_values);

      for (unsigned int q=0; q<n_q_points; ++q)
        {
          Tensor<1,dim> velocity;
          for (unsigned int i=0; i<dim; ++i)
            velocity[i] = stokes_values[q](i);

          max_velocity = std::max (max_velocity, velocity.norm());
        }
    }

  return max_velocity;
}




				 // @sect4{BoussinesqFlowProblem::get_extrapolated_temperature_range}

				 // Next a function that determines the
				 // minimum and maximum temperature at
				 // quadrature points inside $\Omega$ when
				 // extrapolated from the two previous time
				 // steps to the current one. We need this
				 // information in the computation of the
				 // artificial viscosity parameter $\nu$ as
				 // discussed in the introduction.
				 //
				 // The formula for the extrapolated
				 // temperature is
				 // $\left(1+\frac{k_n}{k_{n-1}}
				 // \right)T^{n-1} + \frac{k_n}{k_{n-1}}
				 // T^{n-2}$. The way to compute it is to loop
				 // over all quadrature points and update the
				 // maximum and minimum value if the current
				 // value is bigger/smaller than the previous
				 // one. We initialize the variables that
				 // store the max and min before the loop over
				 // all quadrature points by bounding
				 // $\left(1+\frac{k_n}{k_{n-1}}
				 // \right)T^{n-1}({\mathbf x}_s) +
				 // \frac{k_n}{k_{n-1}} T^{n-2}({\mathbf x}_s)
				 // \le \max_{{\mathbf
				 // x}'_s}\left(1+\frac{k_n}{k_{n-1}}
				 // \right)T^{n-1}({\mathbf x}'_s) +
				 // \max_{{\mathbf x}'_s} \frac{k_n}{k_{n-1}}
				 // T^{n-2}({\mathbf x}'_s)$, where ${\mathbf
				 // x}_s$ is the set of the support points
				 // (i.e. nodal points, but note that the
				 // maximum of a finite element function can
				 // be attained at a point that's not a
				 // support point unless one is using $Q_1$
				 // elements). So if we initialize the minimal
				 // value by this upper bound, and the maximum
				 // value by the negative of this upper bound,
				 // then we know for a fact that it is
				 // larger/smaller than the minimum/maximum
				 // and that the loop over all quadrature
				 // points is ultimately going to update the
				 // initial value with the correct one.
				 //
				 // The only other complication worth
				 // mentioning here is that in the first time
				 // step, $T^{k-2}$ is not yet available of
				 // course. In that case, we can only use
				 // $T^{k-1}$ which we have from the initial
				 // temperature. As quadrature points, we use
				 // the same choice as in the previous
				 // function though with the difference that
				 // now the number of repetitions is
				 // determined by the polynomial degree of the
				 // temperature field.
template <int dim>
std::pair<double,double>
BoussinesqFlowProblem<dim>::get_extrapolated_temperature_range () const
{
  const QIterated<dim> quadrature_formula (QTrapez<1>(),
					   temperature_degree);
  const unsigned int n_q_points = quadrature_formula.size();

  FEValues<dim> fe_values (temperature_fe, quadrature_formula,
                           update_values);
  std::vector<double> old_temperature_values(n_q_points);
  std::vector<double> old_old_temperature_values(n_q_points);

  if (timestep_number != 0)
    {
      double min_temperature = (1. + time_step/old_time_step) *
			       old_temperature_solution.linfty_norm()
			       +
			       time_step/old_time_step *
			       old_old_temperature_solution.linfty_norm(),
	     max_temperature = -min_temperature;

      typename DoFHandler<dim>::active_cell_iterator
	cell = temperature_dof_handler.begin_active(),
	endc = temperature_dof_handler.end();
      for (; cell!=endc; ++cell)
	{
	  fe_values.reinit (cell);
	  fe_values.get_function_values (old_temperature_solution,
					 old_temperature_values);
	  fe_values.get_function_values (old_old_temperature_solution,
					 old_old_temperature_values);

	  for (unsigned int q=0; q<n_q_points; ++q)
	    {
	      const double temperature = 
		(1. + time_step/old_time_step) * old_temperature_values[q]-
		time_step/old_time_step * old_old_temperature_values[q];

	      min_temperature = std::min (min_temperature, temperature);
	      max_temperature = std::max (max_temperature, temperature);
	    }
	}

      return std::make_pair(min_temperature, max_temperature);
    }
  else
    {
      double min_temperature = old_temperature_solution.linfty_norm(),
	     max_temperature = -min_temperature;

      typename DoFHandler<dim>::active_cell_iterator
	cell = temperature_dof_handler.begin_active(),
	endc = temperature_dof_handler.end();
      for (; cell!=endc; ++cell)
	{
	  fe_values.reinit (cell);
	  fe_values.get_function_values (old_temperature_solution,
					 old_temperature_values);

	  for (unsigned int q=0; q<n_q_points; ++q)
	    {
	      const double temperature = old_temperature_values[q];

	      min_temperature = std::min (min_temperature, temperature);
	      max_temperature = std::max (max_temperature, temperature);
	    }
	}
  
      return std::make_pair(min_temperature, max_temperature);
    }    
}



				 // @sect4{BoussinesqFlowProblem::compute_viscosity}

				 // The last of the tool functions computes
				 // the artificial viscosity parameter
				 // $\nu|_K$ on a cell $K$ as a function of
				 // the extrapolated temperature, its gradient
				 // and Hessian (second derivatives), the
				 // velocity, the right hand side $\gamma$ all
				 // on the quadrature points of the current
				 // cell, and various other parameters as
				 // described in detail in the introduction.
				 //
				 // There are some universal constants
				 // worth mentioning here. First, we
				 // need to fix $\beta$; we choose
				 // $\beta=0.015\cdot dim$, a choice
				 // discussed in detail in the results
				 // section of this tutorial
				 // program. The second is the
				 // exponent $\alpha$; $\alpha=1$
				 // appears to work fine for the
				 // current program, even though some
				 // additional benefit might be
				 // expected from chosing $\alpha =
				 // 2$. Finally, there is one thing
				 // that requires special casing: In
				 // the first time step, the velocity
				 // equals zero, and the formula for
				 // $\nu|_K$ is not defined. In that
				 // case, we return $\nu|_K=5\cdot
				 // 10^3 \cdot h_K$, a choice
				 // admittedly more motivated by
				 // heuristics than anything else (it
				 // is in the same order of magnitude,
				 // however, as the value returned for
				 // most cells on the second time
				 // step).
				 //
				 // The rest of the function should be
				 // mostly obvious based on the
				 // material discussed in the
				 // introduction:
template <int dim>
double
BoussinesqFlowProblem<dim>::
compute_viscosity (const std::vector<double>          &old_temperature,
		   const std::vector<double>          &old_old_temperature,
		   const std::vector<Tensor<1,dim> >  &old_temperature_grads,
		   const std::vector<Tensor<1,dim> >  &old_old_temperature_grads,
		   const std::vector<Tensor<2,dim> >  &old_temperature_hessians,
		   const std::vector<Tensor<2,dim> >  &old_old_temperature_hessians,
		   const std::vector<Vector<double> > &present_stokes_values,
		   const std::vector<double>          &gamma_values,
		   const double                        global_u_infty,
		   const double                        global_T_variation,
		   const double                        global_Omega_diameter,
		   const double                        cell_diameter,
		   const double                        old_time_step)
{
  const double beta = 0.015 * dim;
  const double alpha = 1;
  
  if (global_u_infty == 0)
    return 5e-3 * cell_diameter;
  
  const unsigned int n_q_points = old_temperature.size();
  
  double max_residual = 0;
  double max_velocity = 0;
  
  for (unsigned int q=0; q < n_q_points; ++q)
    {
      Tensor<1,dim> u;
      for (unsigned int d=0; d<dim; ++d)
	u[d] = present_stokes_values[q](d);
      
      const double dT_dt = (old_temperature[q] - old_old_temperature[q])
			   / old_time_step;
      const double u_grad_T = u * (old_temperature_grads[q] +
				   old_old_temperature_grads[q]) / 2;
      
      const double kappa_Delta_T = EquationData::kappa
				   * (trace(old_temperature_hessians[q]) +
				      trace(old_old_temperature_hessians[q])) / 2;

      const double residual
	= std::abs((dT_dt + u_grad_T - kappa_Delta_T - gamma_values[q]) *
		   std::pow((old_temperature[q]+old_old_temperature[q]) / 2,
			    alpha-1.));

      max_residual = std::max (residual,        max_residual);
      max_velocity = std::max (std::sqrt (u*u), max_velocity);
    }
  
  const double global_scaling = global_u_infty * global_T_variation /
				std::pow(global_Omega_diameter, alpha - 2.);

  return (beta *
	  max_velocity *
	  std::min (cell_diameter,
		    std::pow(cell_diameter,alpha) *
		    max_residual / global_scaling));
}



				 // @sect4{BoussinesqFlowProblem::setup_dofs}
				 // 
				 // This is the function that sets up the
				 // DoFHandler objects we have here (one for
				 // the Stokes part and one for the
				 // temperature part) as well as set to the
				 // right sizes the various objects required
				 // for the linear algebra in this
				 // program. Its basic operations are similar
				 // to what we do in step-22.
				 // 
				 // The body of the function first enumerates
				 // all degrees of freedom for the Stokes and
				 // temperature systems. In either case, it
				 // then renumbers them according to the
				 // Cuthill-McKee algorithm to improve the
				 // behavior of preconditioners; for the
				 // Stokes part, degrees of freedom are then
				 // also renumbered to ensure that velocities
				 // precede pressure DoFs so that we can
				 // partition the Stokes matrix into a
				 // $2\times 2$ matrix.
				 // 
				 // We then proceed with the generation of the
				 // hanging node constraints that arise from
				 // adaptive grid refinement for both
				 // DoFHandler objects. For the velocity, we
				 // impose no-flux boundary conditions
				 // $\mathbf{u}\cdot \mathbf{n}=0$ by adding
				 // constraints to the object that already
				 // stores the hanging node constraints
				 // matrix. The second parameter in the
				 // function describes the first of the
				 // velocity components in the total dof
				 // vector, which is zero here. The variable
				 // <code>no_normal_flux_boundaries</code>
				 // denotes the boundary indicators for which
				 // to set the no flux boundary conditions;
				 // here, this is boundary indicator zero.
				 //
				 // After having done so, we count the number
				 // of degrees of freedom in the various
				 // blocks:
template <int dim>
void BoussinesqFlowProblem<dim>::setup_dofs ()
{
  std::vector<unsigned int> stokes_sub_blocks (dim+1,0);
  stokes_sub_blocks[dim] = 1;
  
  {
    stokes_dof_handler.distribute_dofs (stokes_fe);
    DoFRenumbering::Cuthill_McKee (stokes_dof_handler);
    DoFRenumbering::component_wise (stokes_dof_handler, stokes_sub_blocks);
    
    stokes_constraints.clear ();
    DoFTools::make_hanging_node_constraints (stokes_dof_handler,
					     stokes_constraints);
    std::set<unsigned char> no_normal_flux_boundaries;
    no_normal_flux_boundaries.insert (0);
    VectorTools::compute_no_normal_flux_constraints (stokes_dof_handler, 0,
						     no_normal_flux_boundaries,
						     stokes_constraints);
    stokes_constraints.close ();
  }
  {
    temperature_dof_handler.distribute_dofs (temperature_fe);
    DoFRenumbering::Cuthill_McKee (temperature_dof_handler);

    temperature_constraints.clear ();
    DoFTools::make_hanging_node_constraints (temperature_dof_handler,
					     temperature_constraints);
    temperature_constraints.close ();
  }
  
  std::vector<unsigned int> stokes_dofs_per_block (2);
  DoFTools::count_dofs_per_block (stokes_dof_handler, stokes_dofs_per_block,
				  stokes_sub_blocks);
  
  const unsigned int n_u = stokes_dofs_per_block[0],
                     n_p = stokes_dofs_per_block[1],
		     n_T = temperature_dof_handler.n_dofs();

  std::cout << "Number of active cells: "
            << triangulation.n_active_cells()
	    << " (on "
	    << triangulation.n_levels()
	    << " levels)"
            << std::endl
            << "Number of degrees of freedom: "
            << n_u + n_p + n_T
            << " (" << n_u << '+' << n_p << '+'<< n_T <<')'
            << std::endl
            << std::endl;
  
				   // The next step is to create the sparsity
				   // pattern for the Stokes and temperature
				   // system matrices as well as the
				   // preconditioner matrix from which we
				   // build the Stokes preconditioner. As in
				   // step-22, we choose to create the pattern
				   // not as in the first few tutorial
				   // programs, but by using the blocked
				   // version of CompressedSetSparsityPattern.
				   // The reason for doing this is mainly
				   // memory, that is, the SparsityPattern
				   // class would consume too much memory when
				   // used in three spatial dimensions as we
				   // intend to do for this program.
				   // 
				   // So, we first release the memory stored
				   // in the matrices, then set up an object
				   // of type
				   // BlockCompressedSetSparsityPattern
				   // consisting of $2\times 2$ blocks (for
				   // the Stokes system matrix and
				   // preconditioner) or
				   // CompressedSparsityPattern (for the
				   // temperature part). We then fill these
				   // sparsity patterns with the nonzero
				   // pattern, taking into account that for
				   // the Stokes system matrix, there are no
				   // entries in the pressure-pressure block
				   // (but all velocity vector components
				   // couple with each other and with the
				   // pressure). Similarly, in the Stokes
				   // preconditioner matrix, only the diagonal
				   // blocks are nonzero, since we use the
				   // vector Laplacian as discussed in the
				   // introduction. This operator only couples
				   // each vector component of the Laplacian
				   // with itself, but not with the other
				   // vector components. (Application of the
				   // constraints resulting from the no-flux
				   // boundary conditions will couple vector
				   // components at the boundary again,
				   // however.)
				   //
				   // When generating the sparsity pattern, we
				   // directly apply the constraints from
				   // hanging nodes and no-flux boundary
				   // conditions. This approach was already
				   // used in step-27, but is different from
				   // the one in early tutorial programs where
				   // we first built the original sparsity
				   // pattern and only then added the entries
				   // resulting from constraints. The reason
				   // for doing so is that later during
				   // assembly we are going to distribute the
				   // constraints immediately when
				   // transferring local to global
				   // dofs. Consequently, there will be no
				   // data written at positions of constrained
				   // degrees of freedom, so we can let the
				   // DoFTools::make_sparsity_pattern function
				   // omit these entries by setting the last
				   // boolean flag to <tt>false</tt>. Once the
				   // sparsity pattern is ready, we can use it
				   // to initialize the Trilinos
				   // matrices. Note that the Trilinos
				   // matrices store the sparsity pattern
				   // internally, so there is no need to keep
				   // the sparsity pattern around after the
				   // initialization of the matrix.
  stokes_block_sizes.resize (2);
  stokes_block_sizes[0] = n_u;
  stokes_block_sizes[1] = n_p;
  {
    stokes_matrix.clear ();

    BlockCompressedSetSparsityPattern csp (2,2);
 
    csp.block(0,0).reinit (n_u, n_u);
    csp.block(0,1).reinit (n_u, n_p);
    csp.block(1,0).reinit (n_p, n_u);
    csp.block(1,1).reinit (n_p, n_p);

    csp.collect_sizes ();

    Table<2,DoFTools::Coupling> coupling (dim+1, dim+1);

    for (unsigned int c=0; c<dim+1; ++c)
      for (unsigned int d=0; d<dim+1; ++d)
	if (! ((c==dim) && (d==dim)))
	  coupling[c][d] = DoFTools::always;
	else
	  coupling[c][d] = DoFTools::none;

    DoFTools::make_sparsity_pattern (stokes_dof_handler, coupling, csp,
				     stokes_constraints, false);

    stokes_matrix.reinit (csp);
  }

  {
    Amg_preconditioner.reset ();
    Mp_preconditioner.reset ();
    stokes_preconditioner_matrix.clear ();

    BlockCompressedSetSparsityPattern csp (2,2);
 
    csp.block(0,0).reinit (n_u, n_u);
    csp.block(0,1).reinit (n_u, n_p);
    csp.block(1,0).reinit (n_p, n_u);
    csp.block(1,1).reinit (n_p, n_p);
      
    csp.collect_sizes ();

    Table<2,DoFTools::Coupling> coupling (dim+1, dim+1);
    for (unsigned int c=0; c<dim+1; ++c)
      for (unsigned int d=0; d<dim+1; ++d)
	if (c == d)
	  coupling[c][d] = DoFTools::always;
	else
	  coupling[c][d] = DoFTools::none;

    DoFTools::make_sparsity_pattern (stokes_dof_handler, coupling, csp,
				     stokes_constraints, false);

    stokes_preconditioner_matrix.reinit (csp);
  }

				   // The creation of the temperature matrix
				   // (or, rather, matrices, since we
				   // provide a temperature mass matrix and
				   // a temperature stiffness matrix, that
				   // will be added together for time
				   // discretization) follows the generation
				   // of the Stokes matrix &ndash; except
				   // that it is much easier here since we
				   // do not need to take care of any
				   // blocks or coupling between components:
  {
    temperature_mass_matrix.clear ();
    temperature_stiffness_matrix.clear ();
    temperature_matrix.clear ();

    CompressedSetSparsityPattern csp (n_T, n_T);      
    DoFTools::make_sparsity_pattern (temperature_dof_handler, csp,
				     temperature_constraints, false);

    temperature_matrix.reinit (csp);
    temperature_mass_matrix.reinit (csp);
    temperature_stiffness_matrix.reinit (csp);
  }

				   // Lastly, we set the vectors for the
				   // solution $\mathbf u$ and $T^k$, the old
				   // solutions $T^{k-1}$ and $T^{k-2}$
				   // (required for time stepping) and the
				   // system right hand sides to their correct
				   // sizes and block structure:
  stokes_solution.reinit (stokes_block_sizes);
  stokes_rhs.reinit (stokes_block_sizes);

  temperature_solution.reinit (temperature_dof_handler.n_dofs());
  old_temperature_solution.reinit (temperature_dof_handler.n_dofs());
  old_old_temperature_solution.reinit (temperature_dof_handler.n_dofs());

  temperature_rhs.reinit (temperature_dof_handler.n_dofs());
}



				 // @sect4{BoussinesqFlowProblem::assemble_stokes_preconditioner}
				 // 
                                 // This function assembles the matrix we use
                                 // for preconditioning the Stokes
                                 // system. What we need are a vector Laplace
                                 // matrix on the velocity components and a
                                 // mass matrix weighted by $\eta^{-1}$ on the
                                 // pressure component. We start by generating
                                 // a quadrature object of appropriate order,
                                 // the FEValues object that can give values
                                 // and gradients at the quadrature points
                                 // (together with quadrature weights). Next
                                 // we create data structures for the cell
                                 // matrix and the relation between local and
                                 // global DoFs. The vectors
                                 // <tt>phi_grad_u</tt> and <tt>phi_p</tt> are
                                 // going to hold the values of the basis
                                 // functions in order to faster build up the
                                 // local matrices, as was already done in
                                 // step-22. Before we start the loop over all
                                 // active cells, we have to specify which
                                 // components are pressure and which are
                                 // velocity.
template <int dim>
void
BoussinesqFlowProblem<dim>::assemble_stokes_preconditioner ()
{
  stokes_preconditioner_matrix = 0;

  QGauss<dim>   quadrature_formula(stokes_degree+2);
  FEValues<dim> stokes_fe_values (stokes_fe, quadrature_formula,
				  update_JxW_values |
				  update_values |
				  update_gradients);

  const unsigned int   dofs_per_cell   = stokes_fe.dofs_per_cell;
  const unsigned int   n_q_points      = quadrature_formula.size();

  FullMatrix<double>   local_matrix (dofs_per_cell, dofs_per_cell);
  std::vector<unsigned int> local_dof_indices (dofs_per_cell);

  std::vector<Tensor<2,dim> > phi_grad_u (dofs_per_cell);
  std::vector<double>         phi_p      (dofs_per_cell);

  const FEValuesExtractors::Vector velocities (0);
  const FEValuesExtractors::Scalar pressure (dim);

  typename DoFHandler<dim>::active_cell_iterator
    cell = stokes_dof_handler.begin_active(),
    endc = stokes_dof_handler.end();
  for (; cell!=endc; ++cell)
    {
      stokes_fe_values.reinit (cell);
      local_matrix = 0;

				       // The creation of the local matrix is
				       // rather simple. There are only a
				       // Laplace term (on the velocity) and a
				       // mass matrix weighted by $\eta^{-1}$
				       // to be generated, so the creation of
				       // the local matrix is done in two
				       // lines. Once the local matrix is
				       // ready (loop over rows and columns in
				       // the local matrix on each quadrature
				       // point), we get the local DoF indices
				       // and write the local information into
				       // the global matrix. We do this as in
				       // step-27, i.e. we directly apply the
				       // constraints from hanging nodes
				       // locally. By doing so, we don't have
				       // to do that afterwards, and we don't
				       // also write into entries of the
				       // matrix that will actually be set to
				       // zero again later when eliminating
				       // constraints.
      for (unsigned int q=0; q<n_q_points; ++q)
	{
	  for (unsigned int k=0; k<dofs_per_cell; ++k)
	    {
	      phi_grad_u[k] = stokes_fe_values[velocities].gradient(k,q);
	      phi_p[k]      = stokes_fe_values[pressure].value (k, q);
	    }
	  
	  for (unsigned int i=0; i<dofs_per_cell; ++i)
	    for (unsigned int j=0; j<dofs_per_cell; ++j)
	      local_matrix(i,j) += (EquationData::eta *
				    scalar_product (phi_grad_u[i], phi_grad_u[j])
				    +
				    (1./EquationData::eta) *
				    phi_p[i] * phi_p[j])
				   * stokes_fe_values.JxW(q);
	}

      cell->get_dof_indices (local_dof_indices);
      stokes_constraints.distribute_local_to_global (local_matrix,
						     local_dof_indices,
						     stokes_preconditioner_matrix);
    }
}



				 // @sect4{BoussinesqFlowProblem::assemble_stokes_preconditioner}
				 // 
				 // This function generates the inner
				 // preconditioners that are going to be
				 // used for the Schur complement block
				 // preconditioner. Since the
				 // preconditioners need only to be
				 // regenerated when the matrices change,
				 // this function does not have to do
				 // anything in case the matrices have not
				 // changed (i.e., the flag
				 // <tt>rebuild_stokes_preconditioner</tt>
				 // has the value <tt>false</tt>).
				 // 
				 // Next, we set up the preconditioner for
				 // the velocity-velocity matrix
				 // <i>A</i>. As explained in the
				 // introduction, we are going to use an
				 // AMG preconditioner based on a vector
				 // Laplace matrix $\hat{A}$ (which is
				 // spectrally close to the Stokes matrix
				 // <i>A</i>). Usually, the
				 // TrilinosWrappers::PreconditionAMG
				 // class can be seen as a good black-box
				 // preconditioner which does not need any
				 // special knowledge. In this case,
				 // however, we have to be careful: since
				 // we build an AMG for a vector problem,
				 // we have to tell the preconditioner
				 // setup which dofs belong to which
				 // vector component. We do this using the
				 // function
				 // DoFTools::extract_constant_modes, a
				 // function that generates a bunch of
				 // <tt>dim</tt> vectors, where each one
				 // has ones in the respective component
				 // of the vector problem and zeros
				 // elsewhere. Hence, these are the
				 // constant modes on each component,
				 // which explains the name of the
				 // variable.
template <int dim>
void
BoussinesqFlowProblem<dim>::build_stokes_preconditioner ()
{
  if (rebuild_stokes_preconditioner == false)
    return;
  
  std::cout << "   Rebuilding Stokes preconditioner..." << std::flush;
      
  assemble_stokes_preconditioner ();
      
  Amg_preconditioner = boost::shared_ptr<TrilinosWrappers::PreconditionAMG>
		       (new TrilinosWrappers::PreconditionAMG());

  std::vector<std::vector<bool> > constant_modes;
  std::vector<bool>  velocity_components (dim+1,true);
  velocity_components[dim] = false;
  DoFTools::extract_constant_modes (stokes_dof_handler, velocity_components, 
				    constant_modes);
  TrilinosWrappers::PreconditionAMG::AdditionalData amg_data;
  amg_data.constant_modes = constant_modes;

				   // Next, we set some more options of the
				   // AMG preconditioner. In particular,
				   // need to tell the AMG setup that we use
				   // quadratic basis functions for the
				   // velocity matrix (this implies more
				   // nonzero elements in the matrix, so
				   // that a more rubust algorithm needs to
				   // be chosen internally). Moreover, we
				   // want to be able to control how the
				   // coarsening structure is build up. The
				   // way AMG does this is to look which
				   // matrix entries are of similar size
				   // than the diagonal entry in order to
				   // algebraically build a coarse-grid
				   // structure. By setting the parameter
				   // <tt>aggregation_threshold</tt> to
				   // 0.05, we specify that all entries that
				   // are more than five precent of size of
				   // some diagonal pivots in that row
				   // should form one coarse grid
				   // point. This parameter is rather
				   // ad-hoc, and some fine-tuning of it can
				   // influence the performance of the
				   // preconditioner. As a rule of thumb,
				   // larger values of
				   // <tt>aggregation_threshold</tt> will
				   // decrease the number of iterations, but
				   // increase the costs per iteration.
				   // 
				   // Eventually, we initialize the
				   // preconditioner for the inversion of
				   // the pressure mass matrix. This matrix
				   // is symmetric and well-behaved, so we
				   // can chose a simple preconditioner. We
				   // stick with an incomple Cholesky (IC)
				   // factorization preconditioner, which is
				   // designed for symmetric matrices. We
				   // wrap the preconditioners into a
				   // boost::shared_ptr pointer, which makes
				   // it easier to recreate the
				   // preconditioner.
  amg_data.elliptic = true;
  amg_data.higher_order_elements = true;
  amg_data.aggregation_threshold = 5e-2;
  Amg_preconditioner->initialize(stokes_preconditioner_matrix.block(0,0),
				 amg_data);
      
  Mp_preconditioner = boost::shared_ptr<TrilinosWrappers::PreconditionIC>
		      (new TrilinosWrappers::PreconditionIC());
  Mp_preconditioner->initialize(stokes_preconditioner_matrix.block(1,1));

  std::cout << std::endl;

  rebuild_stokes_preconditioner = false;
}



				 // @sect4{BoussinesqFlowProblem::assemble_stokes_system}
				 // 
				 // The time lag scheme we use for advancing
				 // the coupled Stokes-temperature system
				 // forces us to split up the assembly (and
				 // the solution of linear systems) into two
				 // step. The first one is to create the
				 // Stokes system matrix and right hand
				 // side, and the second is to create matrix
				 // and right hand sides for the temperature
				 // dofs, which depends on the result for
				 // the velocity.
				 // 
				 // This function does the first of these
				 // two tasks. There are two different
				 // situations for calling this
				 // function. The first one is when we reset
				 // the mesh, and both the matrix and the
				 // right hand side have to be
				 // generated. The second situation only
				 // sets up the right hand side. The reason
				 // for having two different accesses is
				 // that the matrix of the Stokes system
				 // does not change in time unless the mesh
				 // is changed, so we can save a
				 // considerable amount of work by doing the
				 // full assembly only when it is needed.
				 // 
				 // Regarding the technical details of
				 // implementation, not much has changed
				 // from step-22. We reset matrix and
				 // vector, create a quadrature formula on
				 // the cells and one on cell faces (for
				 // implementing Neumann boundary
				 // conditions). Then, we create a
				 // respective FEValues object for both the
				 // cell and the face integration. For the
				 // the update flags of the first, we
				 // perform the calculations of basis
				 // function derivatives only in case of a
				 // full assembly, since they are not needed
				 // otherwise, which makes the call of the
				 // FEValues::reinit function further down
				 // in the program more efficient.
                                 //
				 // There is one thing that needs to be
				 // commented &ndash; since we have a
				 // individual finite element and DoFHandler
				 // for the temperature, we need to generate
				 // a second FEValues object for the proper
				 // evaluation of the temperature
				 // solution. This isn't too complicated to
				 // realize here: just use the temperature
				 // structures and set an update flag for
				 // the basis function values which we need
				 // for evaluation of the temperature
				 // solution.
				 // 
				 // The declarations proceed with some
				 // shortcuts for array sizes, the creation
				 // of the local matrix and right hand side
				 // as well as the vector for the indices of
				 // the local dofs compared to the global
				 // system.
template <int dim>
void BoussinesqFlowProblem<dim>::assemble_stokes_system ()
{
  std::cout << "   Assembling..." << std::flush;

  if (rebuild_stokes_matrix == true)
    stokes_matrix=0;

  stokes_rhs=0;

  QGauss<dim>   quadrature_formula (stokes_degree+2);

  FEValues<dim> stokes_fe_values (stokes_fe, quadrature_formula,
				  update_values    |
				  update_quadrature_points  |
				  update_JxW_values |
				  (rebuild_stokes_matrix == true
				   ?
				   update_gradients
				   :
				   UpdateFlags(0)));

  FEValues<dim> temperature_fe_values (temperature_fe, quadrature_formula,
				       update_values);

  const unsigned int   dofs_per_cell   = stokes_fe.dofs_per_cell;
  const unsigned int   n_q_points      = quadrature_formula.size();

  FullMatrix<double>   local_matrix (dofs_per_cell, dofs_per_cell);
  Vector<double>       local_rhs    (dofs_per_cell);

  std::vector<unsigned int> local_dof_indices (dofs_per_cell);

				   // The vector
				   // <code>old_solution_values</code>
				   // evaluates the temperature solution at
				   // the old time level at the quadrature
				   // points, which is needed for building
				   // the source term in the right hand side
				   // of the momentum equation.
				   // 
				   // The set of vectors we create next hold
				   // the evaluations of the basis functions
				   // that will be used for creating the
				   // matrices. This gives faster access to
				   // that data, which increases the
				   // performance of the assembly. See
				   // step-22 for details.
				   // 
				   // The last two declarations are used to
				   // extract the individual blocks
				   // (velocity, pressure, temperature) from
				   // the total FE system.
  std::vector<double>               old_temperature_values(n_q_points);

  std::vector<Tensor<1,dim> >          phi_u       (dofs_per_cell);
  std::vector<SymmetricTensor<2,dim> > grads_phi_u (dofs_per_cell);
  std::vector<double>                  div_phi_u   (dofs_per_cell);
  std::vector<double>                  phi_p       (dofs_per_cell);

  const FEValuesExtractors::Vector velocities (0);
  const FEValuesExtractors::Scalar pressure (dim);

				   // Now start the loop over all cells in
				   // the problem. We are working on two
				   // different DoFHandlers for this
				   // assembly routine, so we must have two
				   // different cell iterators for the two
				   // objects in use. This might seem a bit
				   // peculiar, since both the Stokes system
				   // and the temperature system use the
				   // same grid, but that's the only way to
				   // keep degrees of freedom in sync. The
				   // first commands within the loop are
				   // again all very familiar, doing the
				   // update of the finite element data as
				   // specified by the update flags, zeroing
				   // out the local arrays and getting the
				   // values of the old solution at the
				   // quadrature point. Then we are ready to
				   // loop over the quadrature points on the
				   // cell.
  typename DoFHandler<dim>::active_cell_iterator
    cell = stokes_dof_handler.begin_active(),
    endc = stokes_dof_handler.end();
  typename DoFHandler<dim>::active_cell_iterator
    temperature_cell = temperature_dof_handler.begin_active();
  
  for (; cell!=endc; ++cell, ++temperature_cell)
    {
      stokes_fe_values.reinit (cell);
      temperature_fe_values.reinit (temperature_cell);
      
      local_matrix = 0;
      local_rhs = 0;

      temperature_fe_values.get_function_values (old_temperature_solution, 
						 old_temperature_values);

      for (unsigned int q=0; q<n_q_points; ++q)
	{
	  const double old_temperature = old_temperature_values[q];

					   // Extract the basis relevant terms in
					   // the inner products once in advance as
					   // shown in step-22 in order to
					   // accelerate assembly.
					   // 
					   // Once this is done, we start the loop
					   // over the rows and columns of the local
					   // matrix and feed the matrix with the
					   // relevant products. The right hand side
					   // is filled with the forcing term driven
					   // by temperature in direction of gravity
					   // (which is vertical in our example).
					   // Note that the right hand side term is
					   // always generated, whereas the matrix
					   // contributions are only updated when it
					   // is requested by the
					   // <code>rebuild_matrices</code> flag.
	  for (unsigned int k=0; k<dofs_per_cell; ++k)
	    {
	      phi_u[k] = stokes_fe_values[velocities].value (k,q);
	      if (rebuild_stokes_matrix)
	        {
		  grads_phi_u[k] = stokes_fe_values[velocities].symmetric_gradient(k,q);
		  div_phi_u[k]   = stokes_fe_values[velocities].divergence (k, q);
		  phi_p[k]       = stokes_fe_values[pressure].value (k, q);
		}
	    }

	  if (rebuild_stokes_matrix)
	    for (unsigned int i=0; i<dofs_per_cell; ++i)
	      for (unsigned int j=0; j<dofs_per_cell; ++j)
		local_matrix(i,j) += (EquationData::eta *
				      grads_phi_u[i] * grads_phi_u[j]
				      - div_phi_u[i] * phi_p[j]
				      - phi_p[i] * div_phi_u[j])
				     * stokes_fe_values.JxW(q);

	  const Point<dim> gravity = ( (dim == 2) ? (Point<dim> (0,1)) : 
				       (Point<dim> (0,0,1)) );
	  for (unsigned int i=0; i<dofs_per_cell; ++i)
	    local_rhs(i) += (EquationData::Rayleigh_number *
			     gravity * phi_u[i] * old_temperature)*
			    stokes_fe_values.JxW(q);
	}

				       // The last step in the loop over all
				       // cells is to enter the local
				       // contributions into the global matrix
				       // and vector structures to the positions
				       // specified in
				       // <code>local_dof_indices</code>.
				       // Again, we only add the matrix data
				       // when it is requested. Again, we let
				       // the ConstraintMatrix class do the
				       // insertion of the cell matrix elements
				       // to the global matrix, which already
				       // condenses the hanging node
				       // constraints.
      cell->get_dof_indices (local_dof_indices);

      if (rebuild_stokes_matrix == true)
	stokes_constraints.distribute_local_to_global (local_matrix,
						       local_dof_indices,
						       stokes_matrix);

      stokes_constraints.distribute_local_to_global (local_rhs,
						     local_dof_indices,
						     stokes_rhs);
    }

  rebuild_stokes_matrix = false;

  std::cout << std::endl;
}




				 // @sect4{BoussinesqFlowProblem::assemble_temperature_matrix}
				 // 
				 // This function assembles the matrix in
				 // the temperature equation. The
				 // temperature matrix consists of two
				 // parts, a mass matrix and the time step
				 // size times a stiffness matrix given by
				 // a Laplace term times the amount of
				 // diffusion. Since the matrix depends on
				 // the time step size (which varies from
				 // one step to another), the temperature
				 // matrix needs to be updated every time
				 // step. We could simply regenerate the
				 // matrices in every time step, but this
				 // is not really efficient since mass and
				 // Laplace matrix do only change when we
				 // change the mesh. Hence, we do this
				 // more efficiently by generating two
				 // separate matrices in this function,
				 // one for the mass matrix and one for
				 // the stiffness (diffusion) matrix. We
				 // will then sum up the matrix plus the
				 // stiffness matrix times the time step
				 // size.
				 // 
				 // So the details for this first step are
				 // very simple. In case we need to
				 // rebuild the matrix (i.e., the mesh has
				 // changed), we zero the data structures,
				 // get a quadrature formula and a
				 // FEValues object, and create local
				 // matrices, local dof indices and
				 // evaluation structures for the basis
				 // functions.
template <int dim>
void BoussinesqFlowProblem<dim>::assemble_temperature_matrix ()
{
  if (rebuild_temperature_matrices == false)
    return;
  
  temperature_mass_matrix = 0;
  temperature_stiffness_matrix = 0;
  
  QGauss<dim>   quadrature_formula(temperature_degree+2);
  FEValues<dim> temperature_fe_values (temperature_fe, quadrature_formula,
				       update_values    | update_gradients |
				       update_JxW_values);

  const unsigned int   dofs_per_cell   = temperature_fe.dofs_per_cell;
  const unsigned int   n_q_points      = quadrature_formula.size();

  FullMatrix<double>   local_mass_matrix (dofs_per_cell, dofs_per_cell);
  FullMatrix<double>   local_stiffness_matrix (dofs_per_cell, dofs_per_cell);

  std::vector<unsigned int> local_dof_indices (dofs_per_cell);

  std::vector<double> gamma_values (n_q_points);

  std::vector<double>         phi_T       (dofs_per_cell);
  std::vector<Tensor<1,dim> > grad_phi_T  (dofs_per_cell);

				   // Now, let's start the loop over all
				   // cells in the triangulation. We need to
				   // zero out the local matrices, update
				   // the finite element evaluations, and
				   // then loop over the rows and columns of
				   // the matrices on each quadrature point,
				   // where we then create the mass matrix
				   // and the stiffness matrix (Laplace
				   // terms times the diffusion
				   // <tt>EquationData::kappa</tt>. Finally,
				   // we let the hanging node constraints
				   // insert these values into the global
				   // matrix, and directly condense the
				   // constraints into the matrix.
  typename DoFHandler<dim>::active_cell_iterator
    cell = temperature_dof_handler.begin_active(),
    endc = temperature_dof_handler.end();
  for (; cell!=endc; ++cell)
    {
      local_mass_matrix = 0;
      local_stiffness_matrix = 0;

      temperature_fe_values.reinit (cell);
      
      for (unsigned int q=0; q<n_q_points; ++q)
	{
	  for (unsigned int k=0; k<dofs_per_cell; ++k)
	    {
	      grad_phi_T[k] = temperature_fe_values.shape_grad (k,q);
	      phi_T[k]      = temperature_fe_values.shape_value (k, q);
	    }
	  
	  for (unsigned int i=0; i<dofs_per_cell; ++i)
	    for (unsigned int j=0; j<dofs_per_cell; ++j)
	      {
		local_mass_matrix(i,j)
		  += (phi_T[i] * phi_T[j]
		      *
		      temperature_fe_values.JxW(q));
		local_stiffness_matrix(i,j)
		  += (EquationData::kappa * grad_phi_T[i] * grad_phi_T[j]
		      *
		      temperature_fe_values.JxW(q));
	      }
	}
      
      cell->get_dof_indices (local_dof_indices);

      temperature_constraints.distribute_local_to_global (local_mass_matrix,
							  local_dof_indices,
							  temperature_mass_matrix);
      temperature_constraints.distribute_local_to_global (local_stiffness_matrix,
							  local_dof_indices,
							  temperature_stiffness_matrix);
    }
  
  rebuild_temperature_matrices = false;
}



				 // @sect4{BoussinesqFlowProblem::assemble_temperature_system}
				 //
				 // This function does the second part of
				 // the assembly work on the temperature
				 // matrix, the actual addition of
				 // pressure mass and stiffness matrix
				 // (where the time step size comes into
				 // play), as well as the creation of the
				 // velocity-dependent right hand
				 // side. The declarations for the right
				 // hand side assembly in this function
				 // are pretty much the same as the ones
				 // used in the other assembly routines,
				 // except that we restrict ourselves to
				 // vectors this time. We are going to
				 // calculate residuals on the temperature
				 // system, which means that we have to
				 // evaluate second derivatives, specified
				 // by the update flag
				 // <tt>update_hessians</tt>. The
				 // temperature equation is coupled to the
				 // Stokes system by means of the fluid
				 // velocity, and these two parts of the
				 // solution are associated with different
				 // dof handlers. So we need to create a
				 // second FEValues object for the
				 // evaluation of the velocity at the
				 // quadrature points.
template <int dim>
void BoussinesqFlowProblem<dim>::assemble_temperature_system ()
{
  const bool use_bdf2_scheme = (timestep_number != 0);

  if (use_bdf2_scheme == true)
    {
      temperature_matrix.copy_from (temperature_mass_matrix);
      temperature_matrix *= (2*time_step + old_time_step) /
			    (time_step + old_time_step);
      temperature_matrix.add (time_step, temperature_stiffness_matrix);
    }
  else
    {
      temperature_matrix.copy_from (temperature_mass_matrix);
      temperature_matrix.add (time_step, temperature_stiffness_matrix);
    }
  
  temperature_rhs = 0;
  
  QGauss<dim>   quadrature_formula(temperature_degree+2);
  FEValues<dim> temperature_fe_values (temperature_fe, quadrature_formula,
				       update_values    | update_gradients |
				       update_hessians |
				       update_quadrature_points  | update_JxW_values);
  FEValues<dim> stokes_fe_values (stokes_fe, quadrature_formula,
				  update_values);

  const unsigned int   dofs_per_cell   = temperature_fe.dofs_per_cell;
  const unsigned int   n_q_points      = quadrature_formula.size();

  Vector<double>       local_rhs (dofs_per_cell);
  FullMatrix<double>   local_matrix (dofs_per_cell, dofs_per_cell);

  std::vector<unsigned int> local_dof_indices (dofs_per_cell);

				   // Here comes the declaration of vectors
				   // to hold the old and present solution
				   // values and gradients for both the cell
				   // as well as faces to the cell, that
				   // will be generated from the global
				   // solution vectors. Next comes the
				   // declaration of an object to hold the
				   // temperature right hande side values,
				   // and we again use shortcuts for the
				   // temperature basis
				   // functions. Eventually, we need to find
				   // the maximum of velocity, temperature
				   // and the diameter of the computational
				   // domain which will be used for the
				   // definition of the stabilization
				   // parameter.
  std::vector<Vector<double> > present_stokes_values (n_q_points, 
						      Vector<double>(dim+1));
  std::vector<double>         old_temperature_values (n_q_points);
  std::vector<double>         old_old_temperature_values(n_q_points);
  std::vector<Tensor<1,dim> > old_temperature_grads(n_q_points);
  std::vector<Tensor<1,dim> > old_old_temperature_grads(n_q_points);
  std::vector<Tensor<2,dim> > old_temperature_hessians(n_q_points);
  std::vector<Tensor<2,dim> > old_old_temperature_hessians(n_q_points);

  EquationData::TemperatureRightHandSide<dim>  temperature_right_hand_side;
  std::vector<double> gamma_values (n_q_points);

  std::vector<double>         phi_T      (dofs_per_cell);
  std::vector<Tensor<1,dim> > grad_phi_T (dofs_per_cell);
  
  const double global_u_infty = get_maximal_velocity();
  const std::pair<double,double>
    global_T_range = get_extrapolated_temperature_range();
  const double global_Omega_diameter = GridTools::diameter (triangulation);

				   // Now, let's start the loop over all
				   // cells in the triangulation. First set
				   // the local rhs to zero, and then get
				   // the values of the old solution
				   // functions (and the current velocity)
				   // at the quadrature points, since they
				   // are going to be needed for the
				   // definition of the stabilization
				   // parameters and as coefficients in the
				   // equation, respectively.
  typename DoFHandler<dim>::active_cell_iterator
    cell = temperature_dof_handler.begin_active(),
    endc = temperature_dof_handler.end();
  typename DoFHandler<dim>::active_cell_iterator
    stokes_cell = stokes_dof_handler.begin_active();

  for (; cell!=endc; ++cell, ++stokes_cell)
    {
      local_rhs = 0;

      temperature_fe_values.reinit (cell);
      stokes_fe_values.reinit (stokes_cell);

      temperature_fe_values.get_function_values (old_temperature_solution,
						 old_temperature_values);
      temperature_fe_values.get_function_values (old_old_temperature_solution,
						 old_old_temperature_values);

      temperature_fe_values.get_function_gradients (old_temperature_solution,
						    old_temperature_grads);
      temperature_fe_values.get_function_gradients (old_old_temperature_solution,
						    old_old_temperature_grads);
      
      temperature_fe_values.get_function_hessians (old_temperature_solution,
						   old_temperature_hessians);
      temperature_fe_values.get_function_hessians (old_old_temperature_solution,
						   old_old_temperature_hessians);
      
      temperature_right_hand_side.value_list (temperature_fe_values.get_quadrature_points(),
					      gamma_values);

      stokes_fe_values.get_function_values (stokes_solution,
					    present_stokes_values);

				       // Next, we calculate the
				       // artificial viscosity for
				       // stabilization according to the
				       // discussion in the introduction
				       // using the dedicated
				       // function. With that at hand, we
				       // can define get into the loop
				       // over quadrature points and local
				       // rhs vector components. The terms
				       // here are quite lenghty, but
				       // their definition follows the
				       // time-discrete system developed
				       // in the introduction of this
				       // program. The BDF-2 scheme needs
				       // one more term from the old time
				       // step (and involves more
				       // complicated factors) than the
				       // backward Euler scheme that is
				       // used for the first time
				       // step. When all this is done, we
				       // distribute the local vector into
				       // the global one (including
				       // hanging node constraints).
      const double nu
	= compute_viscosity (old_temperature_values,
			     old_old_temperature_values,
			     old_temperature_grads,
			     old_old_temperature_grads,
			     old_temperature_hessians,
			     old_old_temperature_hessians,
			     present_stokes_values,
			     gamma_values,
			     global_u_infty,
			     global_T_range.second - global_T_range.first,
			     global_Omega_diameter, cell->diameter(),
			     old_time_step);
      
      for (unsigned int q=0; q<n_q_points; ++q)
	{
	  for (unsigned int k=0; k<dofs_per_cell; ++k)
	    {
	      grad_phi_T[k] = temperature_fe_values.shape_grad (k,q);
	      phi_T[k]      = temperature_fe_values.shape_value (k, q);
	    }

	  const double        old_T      = old_temperature_values[q];
	  const double        old_old_T  = old_old_temperature_values[q];

	  const Tensor<1,dim> old_grad_T     = old_temperature_grads[q];
	  const Tensor<1,dim> old_old_grad_T = old_old_temperature_grads[q];

	  
	  Tensor<1,dim> present_u;
	  for (unsigned int d=0; d<dim; ++d)
	    present_u[d] = present_stokes_values[q](d);

	  if (use_bdf2_scheme == true)
	    {
	      for (unsigned int i=0; i<dofs_per_cell; ++i)
		local_rhs(i) += ((time_step + old_time_step) / old_time_step *
				 old_T * phi_T[i]
				 -
				 (time_step * time_step) /
				 (old_time_step * (time_step + old_time_step)) *
				 old_old_T * phi_T[i]
				 -
				 time_step *
				 present_u *
				 ((1+time_step/old_time_step) * old_grad_T
				  -
				  time_step / old_time_step * old_old_grad_T) *
				 phi_T[i]
				 -
				 time_step *
				 nu *
				 ((1+time_step/old_time_step) * old_grad_T
				  -
				  time_step / old_time_step * old_old_grad_T) *
				 grad_phi_T[i]
				 +
				 time_step *
				 gamma_values[q] * phi_T[i])
				*
				temperature_fe_values.JxW(q);
	    }
	  else
	    {
	      for (unsigned int i=0; i<dofs_per_cell; ++i)
		local_rhs(i) += (old_T * phi_T[i]
				 -
				 time_step *
				 present_u * old_grad_T * phi_T[i]
				 -
				 time_step *
				 nu *
				 old_grad_T * grad_phi_T[i]
				 +
				 time_step *
				 gamma_values[q] * phi_T[i])
				*
				temperature_fe_values.JxW(q);
	    }
	}
      
      cell->get_dof_indices (local_dof_indices);
      temperature_constraints.distribute_local_to_global (local_rhs,
							  local_dof_indices,
							  temperature_rhs);
    }
}




				 // @sect4{BoussinesqFlowProblem::solve}
				 //
				 // This function solves the linear
				 // equation systems. According to
				 // the introduction, we start with
				 // the Stokes system, where we need
				 // to generate our block Schur
				 // preconditioner. Since all the
				 // relevant actions are implemented
				 // in the class
				 // <tt>BlockSchurPreconditioner</tt>,
				 // all we have to do is to
				 // initialize the class
				 // appropriately. What we need to
				 // pass down is an
				 // <tt>InverseMatrix</tt> object
				 // for the pressure mass matrix,
				 // which we set up using the
				 // respective class together with
				 // the IC preconditioner we already
				 // generated, and the AMG
				 // preconditioner for the
				 // velocity-velocity matrix. Note
				 // that both
				 // <tt>Mp_preconditioner</tt> and
				 // <tt>Amg_preconditioner</tt> are
				 // only pointers, so we use
				 // <tt>*</tt> to pass down the
				 // actual preconditioner objects.
				 // 
				 // Once the preconditioner is
				 // ready, we create a GMRES solver
				 // for the block system. Since we
				 // are working with Trilinos data
				 // structures, we have to set the
				 // respective template argument in
				 // the solver. GMRES needs to
				 // internally store temporary
				 // vectors for each iteration (see
				 // even the discussion in the
				 // results section of step-22)
				 // &ndash; the more vectors it can
				 // use, the better it will
				 // generally perform. To let memory
				 // demands not increase to much, we
				 // set the number of vectors to
				 // 100. This means that up to 100
				 // solver iterations, every
				 // temporary vector can be
				 // stored. If the solver needs to
				 // iterate more often to get the
				 // specified tolerance, it will
				 // work on a reduced set of vectors
				 // by restarting at every 100
				 // iterations. Then, we solve the
				 // system and distribute the
				 // constraints in the Stokes
				 // system, i.e. hanging nodes and
				 // no-flux boundary condition, in
				 // order to have the appropriate
				 // solution values even at
				 // constrained dofs. Finally, we
				 // write the number of iterations
				 // to the screen.
template <int dim>
void BoussinesqFlowProblem<dim>::solve ()
{
  std::cout << "   Solving..." << std::endl;

  {
    LinearSolvers::InverseMatrix<TrilinosWrappers::SparseMatrix,
      TrilinosWrappers::PreconditionIC>
      mp_inverse (stokes_preconditioner_matrix.block(1,1), *Mp_preconditioner);

    LinearSolvers::BlockSchurPreconditioner<TrilinosWrappers::PreconditionAMG,
      TrilinosWrappers::PreconditionIC>
      preconditioner (stokes_matrix, mp_inverse, *Amg_preconditioner);

    SolverControl solver_control (stokes_matrix.m(),
				  1e-6*stokes_rhs.l2_norm());

    SolverGMRES<TrilinosWrappers::BlockVector> gmres(solver_control,
						     SolverGMRES<TrilinosWrappers::BlockVector >::AdditionalData(100));

    gmres.solve(stokes_matrix, stokes_solution, stokes_rhs, preconditioner);

    stokes_constraints.distribute (stokes_solution);

    std::cout << "   "
              << solver_control.last_step()
              << " GMRES iterations for Stokes subsystem."
              << std::endl;
  }

				   // Once we know the Stokes
				   // solution, we can determine the
				   // new time step from the maximal
				   // velocity. We have to do this to
				   // satisfy the CFL condition since
				   // convection terms are treated
				   // explicitly in the temperature
				   // equation, as discussed in the
				   // introduction. Next we set up the
				   // temperature system and the right
				   // hand side using the function
				   // <tt>assemble_temperature_system()</tt>. Knowing
				   // the matrix and right hand side
				   // of the temperature equation, we
				   // set up a preconditioner and a
				   // solver. The temperature matrix
				   // is a mass matrix plus a Laplace
				   // matrix times a small number, the
				   // time step. Hence, the mass
				   // matrix dominates and we get a
				   // reasonable good preconditioner
				   // by simple means, namely SSOR. We
				   // set the relaxation parameter to
				   // 1.2. As a solver, we choose the
				   // conjugate gradient method CG. As
				   // before, we tell the solver to
				   // use Trilinos vectors via the
				   // template argument
				   // <tt>TrilinosWrappers::Vector</tt>
				   // at construction. Finally, we
				   // solve, distribute the hanging
				   // node constraints and write out
				   // the number of iterations.
  old_time_step = time_step;    
  time_step = 1./(1.6*dim*std::sqrt(1.*dim)) /
	      temperature_degree *
	      GridTools::minimal_cell_diameter(triangulation) /
              std::max (get_maximal_velocity(), .01);
  
  temperature_solution = old_temperature_solution;


  assemble_temperature_system ();
  {

    SolverControl solver_control (temperature_matrix.m(),
				  1e-8*temperature_rhs.l2_norm());
    SolverCG<TrilinosWrappers::Vector> cg (solver_control);

    TrilinosWrappers::PreconditionSSOR preconditioner;
    preconditioner.initialize (temperature_matrix, 1.2);

    cg.solve (temperature_matrix, temperature_solution,
	      temperature_rhs, preconditioner);

    temperature_constraints.distribute (temperature_solution);

    std::cout << "   "
              << solver_control.last_step()
              << " CG iterations for temperature."
              << std::endl;

				     // In the end of this function, we
				     // step through the vector and read
				     // out the maximum and minimum
				     // temperature value, which we also
				     // want to output.
    double min_temperature = temperature_solution(0),
	   max_temperature = temperature_solution(0);
    for (unsigned int i=0; i<temperature_solution.size(); ++i)
      {
	min_temperature = std::min<double> (min_temperature,
					    temperature_solution(i));
	max_temperature = std::max<double> (max_temperature,
					    temperature_solution(i));
      }
    
    std::cout << "   Temperature range: "
	      << min_temperature << ' ' << max_temperature
	      << std::endl;
  }
}



				 // @sect4{BoussinesqFlowProblem::output_results}
				 // 
				 // This function writes the
				 // solution to a vtk output file
				 // for visualization, which is done
				 // every tenth time step. This is
				 // usually a quite simple task,
				 // since the deal.II library
				 // provides functions that do
				 // almost all the job for us. In
				 // this case, the situation is a
				 // bit more complicated, since we
				 // want to visualize both the
				 // Stokes solution and the
				 // temperature as one data set, but
				 // we have done all the
				 // calculations based on two
				 // different. The way we're going
				 // to achieve this recombination is
				 // to create a joint DoFHandler
				 // that collects both components,
				 // the Stokes solution and the
				 // temperature solution. This can
				 // be nicely done by combining the
				 // finite elements from the two
				 // systems to form one FESystem,
				 // and let this collective system
				 // define a new DoFHandler
				 // object. To be sure that
				 // everything was done correctly,
				 // we perform a sanity check that
				 // ensures that we got all the dofs
				 // from both Stokes and temperature
				 // even in the combined system.
				 // 
				 // Next, we create a vector that
				 // collects the actual solution
				 // values (up to now, we've just
				 // provided the tools for it
				 // without reading any data. Since
				 // this vector is only going to be
				 // used for output, we create it as
				 // a deal.II vector that nicely
				 // cooperate with the data output
				 // classes. Remember that we used
				 // Trilinos vectors for assembly
				 // and solving.
template <int dim>
void BoussinesqFlowProblem<dim>::output_results ()  const
{
  if (timestep_number % 10 != 0)
    return;

  const FESystem<dim> joint_fe (stokes_fe, 1,
				temperature_fe, 1);
  DoFHandler<dim> joint_dof_handler (triangulation);
  joint_dof_handler.distribute_dofs (joint_fe);
  Assert (joint_dof_handler.n_dofs() ==
	  stokes_dof_handler.n_dofs() + temperature_dof_handler.n_dofs(),
	  ExcInternalError());
  
  Vector<double> joint_solution (joint_dof_handler.n_dofs());

				   // Unfortunately, there is no
				   // straight-forward relation that
				   // tells us how to sort Stokes and
				   // temperature vector into the
				   // joint vector. The way we can get
				   // around this trouble is to rely
				   // on the information collected in
				   // the FESystem. For each dof in a
				   // cell, the joint finite element
				   // knows to which equation
				   // component (velocity component,
				   // pressure, or temperature) it
				   // belongs &ndash; that's the
				   // information we need! So we step
				   // through all cells (as a
				   // complication, we need to create
				   // iterations for the cells in the
				   // Stokes system and the
				   // temperature system, too, even
				   // though they are the same in all
				   // the three cases), and for each
				   // joint cell dof, we read out that
				   // component using the function
				   // <tt>joint_fe.system_to_base_index(i).second</tt>. We
				   // also need to keep track whether
				   // we're on a Stokes dof or a
				   // temperature dof, which is
				   // contained in
				   // <tt>joint_fe.system_to_base_index(i).first.first</tt>. Eventually,
				   // the dof_indices data structures
				   // on either of the three systems
				   // tell us how the relation between
				   // global vector and local dofs
				   // looks like on the present cell,
				   // which concludes this tedious
				   // work.
  {
    std::vector<unsigned int> local_joint_dof_indices (joint_fe.dofs_per_cell);
    std::vector<unsigned int> local_stokes_dof_indices (stokes_fe.dofs_per_cell);
    std::vector<unsigned int> local_temperature_dof_indices (temperature_fe.dofs_per_cell);
    
    typename DoFHandler<dim>::active_cell_iterator
      joint_cell       = joint_dof_handler.begin_active(),
      joint_endc       = joint_dof_handler.end(),
      stokes_cell      = stokes_dof_handler.begin_active(),
      temperature_cell = temperature_dof_handler.begin_active();
    for (; joint_cell!=joint_endc; ++joint_cell, ++stokes_cell, ++temperature_cell)
      {
	joint_cell->get_dof_indices (local_joint_dof_indices);
	stokes_cell->get_dof_indices (local_stokes_dof_indices);
	temperature_cell->get_dof_indices (local_temperature_dof_indices);

	for (unsigned int i=0; i<joint_fe.dofs_per_cell; ++i)
	  if (joint_fe.system_to_base_index(i).first.first == 0)
	    {
	      Assert (joint_fe.system_to_base_index(i).second
		      <
		      local_stokes_dof_indices.size(),
		      ExcInternalError());
	      joint_solution(local_joint_dof_indices[i])
		= stokes_solution(local_stokes_dof_indices[joint_fe.system_to_base_index(i).second]);
	    }
	  else
	    {
	      Assert (joint_fe.system_to_base_index(i).first.first == 1,
		      ExcInternalError());
	      Assert (joint_fe.system_to_base_index(i).second
		      <
		      local_stokes_dof_indices.size(),
		      ExcInternalError());
	      joint_solution(local_joint_dof_indices[i])
		= temperature_solution(local_temperature_dof_indices[joint_fe.system_to_base_index(i).second]);
	    }
      }
  }
  
				   // Next, we proceed as we've done
				   // in step-22. We create solution
				   // names (that are going to appear
				   // in the visualization program for
				   // the individual components), and
				   // attach the joint dof handler to
				   // a DataOut object. The first
				   // <tt>dim</tt> components are the
				   // vector velocity, and then we
				   // have pressure and
				   // temperature. This information is
				   // read out using the
				   // DataComponentInterpretation
				   // helper class. Next, we attach
				   // the solution values together
				   // with the names of its components
				   // to the output object, and build
				   // patches according to the degree
				   // of freedom, which are (sub-)
				   // elements that describe the data
				   // for visualization
				   // programs. Finally, we set a file
				   // name (that includes the time
				   // step number) and write the vtk
				   // file.
  std::vector<std::string> joint_solution_names (dim, "velocity");
  joint_solution_names.push_back ("p");
  joint_solution_names.push_back ("T");

  DataOut<dim> data_out;

  data_out.attach_dof_handler (joint_dof_handler);

  std::vector<DataComponentInterpretation::DataComponentInterpretation>
    data_component_interpretation
    (dim+2, DataComponentInterpretation::component_is_scalar);
  for (unsigned int i=0; i<dim; ++i)
    data_component_interpretation[i]
      = DataComponentInterpretation::component_is_part_of_vector;

  data_out.add_data_vector (joint_solution, joint_solution_names,
			    DataOut<dim>::type_dof_data,
			    data_component_interpretation);
  data_out.build_patches (std::min(stokes_degree, temperature_degree));

  std::ostringstream filename;
  filename << "solution-" << Utilities::int_to_string(timestep_number, 4) << ".vtk";

  std::ofstream output (filename.str().c_str());
  data_out.write_vtk (output);
}



				 // @sect4{BoussinesqFlowProblem::refine_mesh}
				 // 
				 // This function takes care of the
				 // adaptive mesh refinement. The
				 // three tasks this function
				 // performs is to first find out
				 // which cells to refine/coarsen,
				 // then to actually do the
				 // refinement and eventually
				 // transfer the solution vectors
				 // between the two different
				 // grids. The first task is simply
				 // achieved by using the
				 // well-established Kelly error
				 // estimator on the temperature (it
				 // is the temperature we're mainly
				 // interested in for this program,
				 // and we need to be accurate in
				 // regions of high temperature
				 // gradients, also to not have too
				 // much numerical diffusion). The
				 // second task is to actually do
				 // the remeshing. That involves
				 // only basic functions as well,
				 // such as the
				 // <tt>refine_and_coarsen_fixed_fraction</tt>
				 // that refines the 80 precent of
				 // the cells which have the largest
				 // estimated error and coarsens the
				 // 10 precent with the smallest
				 // error. For reasons of limited
				 // computer ressources, we have to
				 // set a limit on the maximum
				 // refinement level. We do this
				 // after the refinement indicator
				 // has been applied to the cells,
				 // and simply unselect cells with
				 // too high grid level.
template <int dim>
void BoussinesqFlowProblem<dim>::refine_mesh (const unsigned int max_grid_level)
{
  Vector<float> estimated_error_per_cell (triangulation.n_active_cells());

  KellyErrorEstimator<dim>::estimate (temperature_dof_handler,
				      QGauss<dim-1>(temperature_degree+1),
				      typename FunctionMap<dim>::type(),
				      temperature_solution,
				      estimated_error_per_cell);

  GridRefinement::refine_and_coarsen_fixed_fraction (triangulation,
						     estimated_error_per_cell,
						     0.8, 0.1);
  if (triangulation.n_levels() > max_grid_level) 
    for (typename Triangulation<dim>::active_cell_iterator
	   cell = triangulation.begin_active(max_grid_level);
	 cell != triangulation.end(); ++cell)
      cell->clear_refine_flag ();

				   // Before we can apply the mesh
				   // refinement, we have to prepare
				   // the solution vectors that should
				   // be transfered to the new grid
				   // (we will lose the old grid once
				   // we have done the
				   // refinement). What we definetely
				   // need are the current and the old
				   // temperature (BDF-2 time stepping
				   // requires two old
				   // solutions). Since the
				   // SolutionTransfer objects only
				   // support to transfer one object
				   // per dof handler, we need to
				   // collect the two temperature
				   // solutions in one data
				   // structure. Moreover, we choose
				   // to transfer the Stokes solution,
				   // too. The reason for doing so is
				   // that the Stokes solution will
				   // not change dramatically from
				   // step to step, so we get a good
				   // initial guess for the linear
				   // solver when we reuse old data,
				   // which reduces the number of
				   // needed solver iterations. Next,
				   // we initialize the
				   // SolutionTransfer objects, by
				   // attaching them to the old dof
				   // handler. With this at place, we
				   // can prepare the triangulation
				   // and the data vectors for
				   // refinement (in this order).
  std::vector<TrilinosWrappers::Vector> x_temperature (2);
  x_temperature[0].reinit (temperature_solution);
  x_temperature[0] = temperature_solution;
  x_temperature[1].reinit (temperature_solution);
  x_temperature[1] = old_temperature_solution;
  TrilinosWrappers::BlockVector x_stokes(2);
  x_stokes = stokes_solution;

  SolutionTransfer<dim,TrilinosWrappers::Vector> temperature_trans(temperature_dof_handler);
  SolutionTransfer<dim,TrilinosWrappers::BlockVector> stokes_trans(stokes_dof_handler);

  triangulation.prepare_coarsening_and_refinement();
  temperature_trans.prepare_for_coarsening_and_refinement(x_temperature);
  stokes_trans.prepare_for_coarsening_and_refinement(x_stokes);

				   // Now everything is ready, so do
				   // the refinement and recreate the
				   // dof structure on the new grid,
				   // and initialize the matrix
				   // structures and the new vectors
				   // in the <tt>setup_dofs</tt>
				   // function. Next, we actually
				   // perform the interpolation of the
				   // solutions between the grids. We
				   // create another copy of temporary
				   // vectors for temperature (now
				   // according to the new grid), and
				   // let the interpolate function do
				   // the job. Then, the new vector is
				   // written into the respective
				   // vector. For the Stokes vector,
				   // everything is just the same
				   // &ndash; except that we do not
				   // need another temporary vector
				   // since we just interpolate a
				   // single vector. In the end, we
				   // have to tell the program that
				   // the matrices and preconditioners
				   // need to be regenerated, since
				   // the mesh has changed.
  triangulation.execute_coarsening_and_refinement ();
  setup_dofs ();

  std::vector<TrilinosWrappers::Vector> tmp (2);
  tmp[0].reinit (temperature_solution);
  tmp[1].reinit (temperature_solution);
  temperature_trans.interpolate(x_temperature, tmp);

  temperature_solution = tmp[0];
  old_temperature_solution = tmp[1];
  
  stokes_trans.interpolate (x_stokes, stokes_solution);

  rebuild_stokes_matrix         = true;
  rebuild_temperature_matrices  = true;
  rebuild_stokes_preconditioner = true;
}



				 // @sect4{BoussinesqFlowProblem::run}
				 // 
				 // This function performs all the
				 // essential steps in the
				 // Boussinesq program. It starts by
				 // setting up a grid (depending on
				 // the spatial dimension, we choose
				 // some different level of initial
				 // refinement and additional
				 // adative refinement steps, and
				 // then create a cube in
				 // <tt>dim</tt> dimensions and set
				 // up the dofs for the first
				 // time. Since we want to start the
				 // time stepping already with an
				 // adaptively refined grid, we
				 // perform some pre-refinement
				 // steps, consisting of all
				 // assembly, solution and
				 // refinement, but without actually
				 // advancing in time.
				 // 
				 // Before we start, we project the
				 // initial values to the grid and
				 // obtain the first data for the
				 // <tt>old_temperature_solution</tt>
				 // vector. Then, we initialize time
				 // step number and time step and
				 // start the time loop.
template <int dim>
void BoussinesqFlowProblem<dim>::run ()
{
  const unsigned int initial_refinement = (dim == 2 ? 4 : 2);
  const unsigned int n_pre_refinement_steps = (dim == 2 ? 4 : 3);


  GridGenerator::hyper_cube (triangulation);
  triangulation.refine_global (initial_refinement);

  setup_dofs();

  unsigned int pre_refinement_step = 0;
  
  start_time_iteration:

  VectorTools::project (temperature_dof_handler,
			temperature_constraints,
			QGauss<dim>(temperature_degree+2),
			EquationData::TemperatureInitialValues<dim>(),
			old_temperature_solution);
  
  timestep_number           = 0;
  time_step = old_time_step = 0;
  
  double time = 0;

  do
    {
      std::cout << "Timestep " << timestep_number
		<< ":  t=" << time
		<< ", dt=" << time_step
                << std::endl;

				       // The first steps in the time loop
				       // are all obvious &ndash; we
				       // assemble the Stokes system, the
				       // preconditioner, the temperature
				       // matrix (matrices and
				       // preconditioner do actually only
				       // change in case we've remeshed
				       // before), and then do the
				       // solve. The solution is then
				       // written to screen. Before going
				       // on with the next time step, we
				       // have to check whether we should
				       // first finish the pre-refinement
				       // steps or if we should remesh
				       // (every fifth time step),
				       // refining up to a level that is
				       // consistent with initial
				       // refinement and pre-refinement
				       // steps. Last in the loop is to
				       // advance the solutions, i.e. to
				       // copy the temperature solution to
				       // the next "older" time level.
      assemble_stokes_system ();
      build_stokes_preconditioner ();
      assemble_temperature_matrix ();

      solve ();

      output_results ();

      std::cout << std::endl;
      
      if ((timestep_number == 0) &&
	  (pre_refinement_step < n_pre_refinement_steps))
	{
	  refine_mesh (initial_refinement + n_pre_refinement_steps);
	  ++pre_refinement_step;
	  goto start_time_iteration;
	}
      else
	if ((timestep_number > 0) && (timestep_number % 5 == 0))
	  refine_mesh (initial_refinement + n_pre_refinement_steps);

      time += time_step;
      ++timestep_number;

      old_old_temperature_solution = old_temperature_solution;
      old_temperature_solution     = temperature_solution;      
    }
				   // Do all the above until we arrive
				   // at time 100.
  while (time <= 100);
}



				 // @sect3{The <code>main</code> function}
				 // 
				 // The main function looks almost
				 // the same as in all other
				 // programs. The only difference is
				 // that Trilinos wants to get the
				 // arguments from calling the
				 // function (argc and argv) in
				 // order to correctly set up the
				 // MPI system in case we use those
				 // compilers (even though this
				 // program is only meant to be run
				 // in serial).
int main (int argc, char *argv[])
{
  try
    {
      deallog.depth_console (0);

      Utilities::TrilinosTools trilinos (&argc, &argv);

      BoussinesqFlowProblem<2> flow_problem;
      flow_problem.run ();
    }
  catch (std::exception &exc)
    {
      std::cerr << std::endl << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Exception on processing: " << std::endl
                << exc.what() << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;

      return 1;
    }
  catch (...)
    {
      std::cerr << std::endl << std::endl
                << "----------------------------------------------------"
                << std::endl;
      std::cerr << "Unknown exception!" << std::endl
                << "Aborting!" << std::endl
                << "----------------------------------------------------"
                << std::endl;
      return 1;
    }

  return 0;
}
