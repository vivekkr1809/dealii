/* $Id$ */

#include <fe/fe.h>
#include <fe/fe_values.h>
#include <fe/quadrature.h>
#include <grid/tria_iterator.h>
#include <grid/tria_accessor.h>
#include <grid/tria_boundary.h>



/*------------------------------- FEValues -------------------------------*/


template <int dim>
FEValues<dim>::FEValues (const FiniteElement<dim> &fe,
			 const Quadrature<dim>    &quadrature,
			 const UpdateFlags         update_flags) :
		n_quadrature_points(quadrature.n_quadrature_points),
		total_dofs(fe.total_dofs),
		shape_values(fe.total_dofs, quadrature.n_quadrature_points),
		shape_gradients(fe.total_dofs,
				vector<Point<dim> >(quadrature.n_quadrature_points)),
		unit_shape_gradients(fe.total_dofs,
				     vector<Point<dim> >(quadrature.n_quadrature_points)),
		weights(quadrature.n_quadrature_points, 0),
		JxW_values(quadrature.n_quadrature_points, 0),
		quadrature_points(quadrature.n_quadrature_points, Point<dim>()),
		unit_quadrature_points(quadrature.n_quadrature_points, Point<dim>()),
		ansatz_points (fe.total_dofs, Point<dim>()),
		jacobi_matrices (quadrature.n_quadrature_points,
				 dFMatrix(dim,dim)),
		update_flags (update_flags)
{
  for (unsigned int i=0; i<fe.total_dofs; ++i)
    for (unsigned int j=0; j<n_quadrature_points; ++j) 
      {
	shape_values(i,j) = fe.shape_value(i, quadrature.quad_point(j));
	unit_shape_gradients[i][j]
	  = fe.shape_grad(i, quadrature.quad_point(j));
      };

  for (unsigned int i=0; i<n_quadrature_points; ++i) 
    {
      weights[i] = quadrature.weight(i);
      unit_quadrature_points[i] = quadrature.quad_point(i);
    };
};



template <int dim>
double FEValues<dim>::shape_value (const unsigned int i,
				   const unsigned int j) const {
  Assert (i<shape_values.m(), ExcInvalidIndex (i, shape_values.m()));
  Assert (j<shape_values.n(), ExcInvalidIndex (j, shape_values.n()));

  return shape_values(i,j);
};



template <int dim>
const Point<dim> &
FEValues<dim>::shape_grad (const unsigned int i,
			   const unsigned int j) const {
  Assert (i<shape_values.m(), ExcInvalidIndex (i, shape_values.m()));
  Assert (j<shape_values.n(), ExcInvalidIndex (j, shape_values.n()));
  Assert (update_flags & update_gradients, ExcAccessToUninitializedField());

  return shape_gradients[i][j];
};



template <int dim>
const Point<dim> & FEValues<dim>::quadrature_point (const unsigned int i) const {
  Assert (i<n_quadrature_points, ExcInvalidIndex(i, n_quadrature_points));
  Assert (update_flags & update_q_points, ExcAccessToUninitializedField());
  
  return quadrature_points[i];
};



template <int dim>
const Point<dim> & FEValues<dim>::ansatz_point (const unsigned int i) const {
  Assert (i<ansatz_points.size(), ExcInvalidIndex(i, ansatz_points.size()));
  Assert (update_flags & update_ansatz_points, ExcAccessToUninitializedField());
  
  return ansatz_points[i];
};



template <int dim>
double FEValues<dim>::JxW (const unsigned int i) const {
  Assert (i<n_quadrature_points, ExcInvalidIndex(i, n_quadrature_points));
  Assert (update_flags & update_JxW_values, ExcAccessToUninitializedField());
  
  return JxW_values[i];
};



template <int dim>
void FEValues<dim>::reinit (const typename DoFHandler<dim>::cell_iterator &cell,
			    const FiniteElement<dim>                      &fe,
			    const Boundary<dim>                           &boundary) {
				   // fill jacobi matrices and real
				   // quadrature points
  if ((update_flags & update_jacobians) ||
      (update_flags & update_q_points)  ||
      (update_flags & update_ansatz_points))
    fe.fill_fe_values (cell,
		       unit_quadrature_points,
		       jacobi_matrices,
		       update_flags & update_jacobians,
		       ansatz_points,
		       update_flags & update_ansatz_points,
		       quadrature_points,
		       update_flags & update_q_points,
		       boundary);

				   // compute gradients on real element if
				   // requested
  if (update_flags & update_gradients) 
    {
      Assert (update_flags & update_jacobians, ExcCannotInitializeField());
      
      for (unsigned int i=0; i<fe.total_dofs; ++i)
	for (unsigned int j=0; j<n_quadrature_points; ++j)
	  for (unsigned int s=0; s<dim; ++s)
	    {
	      shape_gradients[i][j](s) = 0;
	      
					       // (grad psi)_s =
					       // (grad_{\xi\eta})_b J_{bs}
					       // with J_{bs}=(d\xi_b)/(dx_s)
	      for (unsigned int b=0; b<dim; ++b)
		shape_gradients[i][j](s)
		  +=
		  unit_shape_gradients[i][j](b) * jacobi_matrices[j](b,s);
	    };
    };
  
  
				   // compute Jacobi determinants in
				   // quadrature points.
				   // refer to the general doc for
				   // why we take the inverse of the
				   // determinant
  if (update_flags & update_JxW_values) 
    {
      Assert (update_flags & update_jacobians, ExcCannotInitializeField());
      for (unsigned int i=0; i<n_quadrature_points; ++i)
	JxW_values[i] = weights[i] / jacobi_matrices[i].determinant();
    };
};





/*------------------------------- FEFaceValues -------------------------------*/


template <int dim>
FEFaceValues<dim>::FEFaceValues (const FiniteElement<dim> &fe,
				 const Quadrature<dim-1>  &quadrature,
				 const UpdateFlags         update_flags) :
		n_quadrature_points(quadrature.n_quadrature_points),
		total_dofs(fe.total_dofs),
		shape_gradients(fe.total_dofs,
				vector<Point<dim> >(quadrature.n_quadrature_points)),
		weights(quadrature.n_quadrature_points, 0),
		JxW_values(quadrature.n_quadrature_points, 0),
		quadrature_points(quadrature.n_quadrature_points, Point<dim>()),
		unit_quadrature_points(quadrature.n_quadrature_points, Point<dim-1>()),
		ansatz_points (fe.total_dofs, Point<dim>()),
		jacobi_matrices (quadrature.n_quadrature_points,dFMatrix(dim,dim)),
		face_jacobi_determinants (quadrature.n_quadrature_points,0),
		normal_vectors (quadrature.n_quadrature_points,Point<dim>()),
		update_flags (update_flags),
		selected_face(0)
{
  for (unsigned int face=0; face<2*dim; ++face)
    {
      shape_values[face].reinit(fe.total_dofs, quadrature.n_quadrature_points);
      unit_shape_gradients[face].resize (fe.total_dofs,
					 vector<Point<dim> >(quadrature.n_quadrature_points));
      global_unit_quadrature_points[face].resize (quadrature.n_quadrature_points,
						  Point<dim>());
    };

  				   // set up an array of the unit points
				   // on the given face, but in coordinates
				   // of the space with #dim# dimensions.
				   // the points are still on the unit
				   // cell.
  for (unsigned int face=0; face<2*dim; ++face)
    for (unsigned int p=0; p<n_quadrature_points; ++p)
      switch (dim) 
	{
	  case 2:
	  {
	    
	    switch (face)
	      {
		case 0:
		      global_unit_quadrature_points[face][p]
			= Point<dim>(unit_quadrature_points[p](0),0);
		      break;	   
		case 1:
		      global_unit_quadrature_points[face][p]
			= Point<dim>(1,unit_quadrature_points[p](0));
		      break;	   
		case 2:
		      global_unit_quadrature_points[face][p]
			= Point<dim>(unit_quadrature_points[p](0),1);
		      break;	   
		case 3:
		      global_unit_quadrature_points[face][p]
			= Point<dim>(0,unit_quadrature_points[p](0));
		      break;
		default:
		      Assert (false, ExcInternalError());
	      };
	    
	    break;
	  };
	  default:
		Assert (false, ExcNotImplemented());
	};

  for (unsigned int i=0; i<n_quadrature_points; ++i) 
    {
      weights[i] = quadrature.weight(i);
      unit_quadrature_points[i] = quadrature.quad_point(i);
    };

  for (unsigned int face=0; face<2*dim; ++face)
    for (unsigned int i=0; i<fe.total_dofs; ++i)
      for (unsigned int j=0; j<n_quadrature_points; ++j) 
	{
	  shape_values[face](i,j) = fe.shape_value(i, global_unit_quadrature_points[face][j]);
	  unit_shape_gradients[face][i][j]
	    = fe.shape_grad(i, global_unit_quadrature_points[face][j]);
	};
};



template <int dim>
double FEFaceValues<dim>::shape_value (const unsigned int i,
				       const unsigned int j) const {
  Assert (i<shape_values[selected_face].m(),
	  ExcInvalidIndex (i, shape_values[selected_face].m()));
  Assert (j<shape_values[selected_face].n(),
	  ExcInvalidIndex (j, shape_values[selected_face].n()));

  return shape_values[selected_face](i,j);
};



template <int dim>
const Point<dim> &
FEFaceValues<dim>::shape_grad (const unsigned int i,
			       const unsigned int j) const {
  Assert (i<shape_values[selected_face].m(),
	  ExcInvalidIndex (i, shape_values[selected_face].m()));
  Assert (j<shape_values[selected_face].n(),
	  ExcInvalidIndex (j, shape_values[selected_face].n()));
  Assert (update_flags & update_gradients, ExcAccessToUninitializedField());

  return shape_gradients[i][j];
};



template <int dim>
const Point<dim> & FEFaceValues<dim>::quadrature_point (const unsigned int i) const {
  Assert (i<n_quadrature_points, ExcInvalidIndex(i, n_quadrature_points));
  Assert (update_flags & update_q_points, ExcAccessToUninitializedField());
  
  return quadrature_points[i];
};



template <int dim>
const Point<dim> & FEFaceValues<dim>::ansatz_point (const unsigned int i) const {
  Assert (i<ansatz_points.size(), ExcInvalidIndex(i, ansatz_points.size()));
  Assert (update_flags & update_ansatz_points, ExcAccessToUninitializedField());
  
  return ansatz_points[i];
};



template <int dim>
const Point<dim> & FEFaceValues<dim>::normal_vector (const unsigned int i) const {
  Assert (i<normal_vectors.size(), ExcInvalidIndex(i, normal_vectors.size()));
  Assert (update_flags & update_normal_vectors, ExcAccessToUninitializedField());
  
  return normal_vectors[i];
};



template <int dim>
double FEFaceValues<dim>::JxW (const unsigned int i) const {
  Assert (i<n_quadrature_points, ExcInvalidIndex(i, n_quadrature_points));
  Assert (update_flags & update_JxW_values, ExcAccessToUninitializedField());
  
  return JxW_values[i];
};



template <int dim>
void FEFaceValues<dim>::reinit (const typename DoFHandler<dim>::cell_iterator &cell,
				const unsigned int                             face_no,
				const FiniteElement<dim>                      &fe,
				const Boundary<dim>                           &boundary) {
  selected_face = face_no;
				   // fill jacobi matrices and real
				   // quadrature points
  if ((update_flags & update_jacobians) ||
      (update_flags & update_q_points)  ||
      (update_flags & update_ansatz_points) ||
      (update_flags & update_JxW_values))
    fe.fill_fe_face_values (cell,
			    face_no,
			    unit_quadrature_points,
			    global_unit_quadrature_points[face_no],
			    jacobi_matrices,
			    update_flags & update_jacobians,
			    ansatz_points,
			    update_flags & update_ansatz_points,
			    quadrature_points,
			    update_flags & update_q_points,
			    face_jacobi_determinants,
			    update_flags & update_JxW_values,
			    normal_vectors,
			    update_flags & update_normal_vectors,
			    boundary);

				   // compute gradients on real element if
				   // requested
  if (update_flags & update_gradients) 
    {
      Assert (update_flags & update_jacobians, ExcCannotInitializeField());
      
      for (unsigned int i=0; i<fe.total_dofs; ++i)
	for (unsigned int j=0; j<n_quadrature_points; ++j)
	  for (unsigned int s=0; s<dim; ++s)
	    {
	      shape_gradients[i][j](s) = 0;
	      
					       // (grad psi)_s =
					       // (grad_{\xi\eta})_b J_{bs}
					       // with J_{bs}=(d\xi_b)/(dx_s)
	      for (unsigned int b=0; b<dim; ++b)
		shape_gradients[i][j](s)
		  +=
		  unit_shape_gradients[face_no][i][j](b) * jacobi_matrices[j](b,s);
	    };
    };
  
  
				   // compute Jacobi determinants in
				   // quadrature points.
				   // refer to the general doc for
				   // why we take the inverse of the
				   // determinant
  if (update_flags & update_JxW_values) 
    {
      Assert (update_flags & update_jacobians, ExcCannotInitializeField());
      for (unsigned int i=0; i<n_quadrature_points; ++i)
	JxW_values[i] = weights[i] * face_jacobi_determinants[i];
    };
};





/*------------------------------- Explicit Instantiations -------------*/

template class FEValues<1>;
template class FEValues<2>;

template class FEFaceValues<2>;


