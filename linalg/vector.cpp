// Copyright (c) 2010, Lawrence Livermore National Security, LLC. Produced at
// the Lawrence Livermore National Laboratory. LLNL-CODE-443211. All Rights
// reserved. See file COPYRIGHT for details.
//
// This file is part of the MFEM library. For more information and source code
// availability see http://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the GNU Lesser General Public License (as published by the Free
// Software Foundation) version 2.1 dated February 1999.

// Implementation of data type vector

#include "../general/okina.hpp"
#include "vector.hpp"
#include "kernels/vector.hpp"

#if defined(MFEM_USE_SUNDIALS) && defined(MFEM_USE_MPI)
#include <nvector/nvector_parallel.h>
#include <nvector/nvector_parhyp.h>
#endif

#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <limits>

namespace mfem
{

void Vector::Push() const
{
   mm::push(data, size*sizeof(double));
}

void Vector::Pull() const
{
   mm::pull(data, size*sizeof(double));
}

Vector::Vector(const Vector &v)
{
   int s = v.Size();

   if (s > 0)
   {
      MFEM_ASSERT(v.data, "invalid source vector");
      allocsize = size = s;
      data = mm::malloc<double>(s);
      mm::memcpy(data, v.data, sizeof(double)*s);
   }
   else
   {
      allocsize = size = 0;
      data = NULL;
   }
}

Vector::Vector(double *_data, int _size)
{
   data = _data;
   size = _size;
   allocsize = -size;
}

void Vector::SetData(double *d)
{
   data = d;
}

void Vector::SetDataAndSize(double *d, int s)
{
   data = d;
   size = s;
   allocsize = -s;
}

void Vector::Load(std::istream **in, int np, int *dim)
{
   MFEM_GPU_CANNOT_PASS;
   int i, j, s;

   s = 0;
   for (i = 0; i < np; i++)
   {
      s += dim[i];
   }

   SetSize(s);

   int p = 0;
   for (i = 0; i < np; i++)
      for (j = 0; j < dim[i]; j++)
      {
         *in[i] >> data[p++];
      }
}

void Vector::Load(std::istream &in, int Size)
{
   MFEM_GPU_CANNOT_PASS;
   SetSize(Size);

   for (int i = 0; i < size; i++)
   {
      in >> data[i];
   }
}

double &Vector::Elem(int i)
{
   return operator()(i);
}

const double &Vector::Elem(int i) const
{
   return operator()(i);
}

double Vector::operator*(const double *v) const
{
   const int N = size;
   double prod = kernels::vector::Dot(N, data, v);
   return prod;
}

double Vector::operator*(const Vector &v) const
{
#ifdef MFEM_DEBUG
   if (v.size != size)
   {
      mfem_error("Vector::operator*(const Vector &) const");
   }
#endif

   return operator*(v.data);
}

Vector &Vector::operator=(const double *v)
{
   if (data != v)
   {
      MFEM_ASSERT(data + size <= v || v + size <= data, "Vectors overlap!");
      kernels::vector::Assign(size, v, data);
   }
   return *this;
}

Vector &Vector::operator=(const Vector &v)
{
   SetSize(v.Size());
   return operator=(v.data);
}

Vector &Vector::operator=(double value)
{
   kernels::vector::Set(size, value, data);
   return *this;
}

Vector &Vector::operator*=(double c)
{
   kernels::vector::OpMultEQ(size, c, data);
   return *this;
}

Vector &Vector::operator/=(double c)
{
   MFEM_GPU_CANNOT_PASS;
   double m = 1.0/c;
   for (int i = 0; i < size; i++)
   {
      data[i] *= m;
   }
   return *this;
}

Vector &Vector::operator-=(double c)
{
   MFEM_GPU_CANNOT_PASS;
   for (int i = 0; i < size; i++)
   {
      data[i] -= c;
   }
   return *this;
}

Vector &Vector::operator-=(const Vector &v)
{
#ifdef MFEM_DEBUG
   if (size != v.size)
   {
      mfem_error("Vector::operator-=(const Vector &)");
   }
#endif
   kernels::vector::OpSubtractEQ(size,v,data);
   return *this;
}

Vector &Vector::operator+=(const Vector &v)
{
#ifdef MFEM_DEBUG
   if (size != v.size)
   {
      mfem_error("Vector::operator+=(const Vector &)");
   }
#endif
   kernels::vector::OpPlusEQ(size,v.GetData(),data);
   return *this;
}

Vector &Vector::Add(const double a, const Vector &Va)
{
#ifdef MFEM_DEBUG
   if (size != Va.size)
   {
      mfem_error("Vector::Add(const double, const Vector &)");
   }
#endif
   if (a != 0.0) { kernels::vector::OpAddEQ(size, a, Va, data); }
   return *this;
}

Vector &Vector::Set(const double a, const Vector &Va)
{
   MFEM_GPU_CANNOT_PASS;
#ifdef MFEM_DEBUG
   if (size != Va.size)
   {
      mfem_error("Vector::Set(const double, const Vector &)");
   }
#endif
   for (int i = 0; i < size; i++)
   {
      data[i] = a * Va(i);
   }
   return *this;
}

void Vector::SetVector(const Vector &v, int offset)
{
   MFEM_GPU_CANNOT_PASS;
   int vs = v.Size();
   double *vp = v.data, *p = data + offset;

#ifdef MFEM_DEBUG
   if (offset+vs > size)
   {
      mfem_error("Vector::SetVector(const Vector &, int)");
   }
#endif

   for (int i = 0; i < vs; i++)
   {
      p[i] = vp[i];
   }
}

void Vector::Neg()
{
   MFEM_GPU_CANNOT_PASS;
   for (int i = 0; i < size; i++)
   {
      data[i] = -data[i];
   }
}

void add(const Vector &v1, const Vector &v2, Vector &v)
{
#ifdef MFEM_DEBUG
   if (v.size != v1.size || v.size != v2.size)
   {
      mfem_error("add(Vector &v1, Vector &v2, Vector &v)");
   }
#endif

#ifdef MFEM_USE_OPENMP
   MFEM_GPU_CANNOT_PASS;
   #pragma omp parallel for
   for (int i = 0; i < v.size; i++)
   {
      v.data[i] = v1.data[i] + v2.data[i];
   }
#else
   const int N = v.size;
   GET_CONST_PTR(v1);
   GET_CONST_PTR(v2);
   GET_PTR(v);
   MFEM_FORALL(i, N, d_v[i] = d_v1[i] + d_v2[i];);
#endif
}

void add(const Vector &v1, double alpha, const Vector &v2, Vector &v)
{
#ifdef MFEM_DEBUG
   if (v.size != v1.size || v.size != v2.size)
   {
      mfem_error ("add(Vector &v1, double alpha, Vector &v2, Vector &v)");
   }
#endif
   if (alpha == 0.0)
   {
      v = v1;
   }
   else if (alpha == 1.0)
   {
      add(v1, v2, v);
   }
   else
   {
      const double *v1p = v1.data, *v2p = v2.data;
      double *vp = v.data;
      int s = v.size;
#ifdef MFEM_USE_OPENMP
#warning MFEM_USE_OPENMP with KERNELS
      //#pragma omp parallel for
#endif
      kernels::vector::AlphaAdd(vp,v1p,alpha,v2p,s);
   }
}

void add(const double a, const Vector &x, const Vector &y, Vector &z)
{
   MFEM_GPU_CANNOT_PASS;
#ifdef MFEM_DEBUG
   if (x.size != y.size || x.size != z.size)
      mfem_error ("add(const double a, const Vector &x, const Vector &y,"
                  " Vector &z)");
#endif
   if (a == 0.0)
   {
      z = 0.0;
   }
   else if (a == 1.0)
   {
      add(x, y, z);
   }
   else
   {
      const double *xp = x.data;
      const double *yp = y.data;
      double       *zp = z.data;
      int            s = x.size;

#ifdef MFEM_USE_OPENMP
      #pragma omp parallel for
#endif
      for (int i = 0; i < s; i++)
      {
         zp[i] = a * (xp[i] + yp[i]);
      }
   }
}

void add(const double a, const Vector &x,
         const double b, const Vector &y, Vector &z)
{
   MFEM_GPU_CANNOT_PASS;
#ifdef MFEM_DEBUG
   if (x.size != y.size || x.size != z.size)
      mfem_error("add(const double a, const Vector &x,\n"
                 "    const double b, const Vector &y, Vector &z)");
#endif
   if (a == 0.0)
   {
      z.Set(b, y);
   }
   else if (b == 0.0)
   {
      z.Set(a, x);
   }
   else if (a == 1.0)
   {
      add(x, b, y, z);
   }
   else if (b == 1.0)
   {
      add(y, a, x, z);
   }
   else if (a == b)
   {
      add(a, x, y, z);
   }
   else
   {
      const double *xp = x.data;
      const double *yp = y.data;
      double       *zp = z.data;
      int            s = x.size;

#ifdef MFEM_USE_OPENMP
      #pragma omp parallel for
#endif
      for (int i = 0; i < s; i++)
      {
         zp[i] = a * xp[i] + b * yp[i];
      }
   }
}

void subtract(const Vector &x, const Vector &y, Vector &z)
{
#ifdef MFEM_DEBUG
   if (x.size != y.size || x.size != z.size)
   {
      mfem_error ("subtract(const Vector &, const Vector &, Vector &)");
   }
#endif
   const double *xp = x.data;
   const double *yp = y.data;
   double       *zp = z.data;
   int            s = x.size;

#ifdef MFEM_USE_OPENMP
   //#pragma omp parallel for
#endif
   kernels::vector::Subtract(zp,xp,yp,s);
}

void subtract(const double a, const Vector &x, const Vector &y, Vector &z)
{
   MFEM_GPU_CANNOT_PASS;
#ifdef MFEM_DEBUG
   if (x.size != y.size || x.size != z.size)
      mfem_error("subtract(const double a, const Vector &x,"
                 " const Vector &y, Vector &z)");
#endif

   if (a == 0.)
   {
      z = 0.;
   }
   else if (a == 1.)
   {
      subtract(x, y, z);
   }
   else
   {
      const double *xp = x.data;
      const double *yp = y.data;
      double       *zp = z.data;
      int            s = x.size;

#ifdef MFEM_USE_OPENMP
      #pragma omp parallel for
#endif
      for (int i = 0; i < s; i++)
      {
         zp[i] = a * (xp[i] - yp[i]);
      }
   }
}

void Vector::median(const Vector &lo, const Vector &hi)
{
   MFEM_GPU_CANNOT_PASS;
   double *v = data;

   for (int i = 0; i < size; i++)
   {
      if (v[i] < lo[i])
      {
         v[i] = lo[i];
      }
      else if (v[i] > hi[i])
      {
         v[i] = hi[i];
      }
   }
}

void Vector::GetSubVector(const Array<int> &dofs, Vector &elemvect) const
{
   const int n = dofs.Size();
   elemvect.SetSize(n);
   kernels::vector::GetSubvector(n, elemvect, data, dofs);
}

void Vector::GetSubVector(const Array<int> &dofs, double *elem_data) const
{
   kernels::vector::GetSubvector(dofs.Size(), elem_data, data,dofs);
}

void Vector::SetSubVector(const Array<int> &dofs, const double value)
{
   kernels::vector::SetSubvector(dofs.Size(), data, value, dofs);
}

void Vector::SetSubVector(const Array<int> &dofs, const Vector &elemvect)
{
   kernels::vector::SetSubvector(dofs.Size(), data, elemvect, dofs);
}

void Vector::SetSubVector(const Array<int> &dofs, double *elem_data)
{
   kernels::vector::SetSubvector(dofs.Size(), data, elem_data, dofs);
}

void Vector::AddElementVector(const Array<int> &dofs, const Vector &elemvect)
{
   MFEM_ASSERT(dofs.Size() == elemvect.Size(), "Size mismatch: "
               "length of dofs is " << dofs.Size() <<
               ", length of elemvect is " << elemvect.Size());
   kernels::vector::AddElement(dofs.Size(), dofs, elemvect.GetData(), data);
}

void Vector::AddElementVector(const Array<int> &dofs, double *elem_data)
{
   const int n = dofs.Size();
   kernels::vector::AddElement(n,dofs,elem_data,data);
}

void Vector::AddElementVector(const Array<int> &dofs, const double a,
                              const Vector &elemvect)
{
   kernels::vector::AddElementAlpha(dofs.Size(), dofs, elemvect, data, a);
}

void Vector::SetSubVectorComplement(const Array<int> &dofs, const double val)
{
   Vector dofs_vals;
   GetSubVector(dofs, dofs_vals);
   operator=(val);
   SetSubVector(dofs, dofs_vals);
}

void Vector::Print(std::ostream &out, int width) const
{
   if (!size) { return; }
   this->Pull();
   for (int i = 0; 1; )
   {
      out << data[i];
      i++;
      if (i == size)
      {
         break;
      }
      if ( i % width == 0 )
      {
         out << '\n';
      }
      else
      {
         out << ' ';
      }
   }
   out << '\n';
}

void Vector::Print_HYPRE(std::ostream &out) const
{
   int i;
   std::ios::fmtflags old_fmt = out.flags();
   out.setf(std::ios::scientific);
   std::streamsize old_prec = out.precision(14);

   out << size << '\n';  // number of rows

   for (i = 0; i < size; i++)
   {
      out << data[i] << '\n';
   }

   out.precision(old_prec);
   out.flags(old_fmt);
}

void Vector::Randomize(int seed)
{
   MFEM_GPU_CANNOT_PASS;
   // static unsigned int seed = time(0);
   const double max = (double)(RAND_MAX) + 1.;

   if (seed == 0)
   {
      seed = (int)time(0);
   }

   // srand(seed++);
   srand((unsigned)seed);

   for (int i = 0; i < size; i++)
   {
      data[i] = std::abs(rand()/max);
   }
}

double Vector::Norml2() const
{
   MFEM_GPU_CANNOT_PASS;
   // Scale entries of Vector on the fly, using algorithms from
   // std::hypot() and LAPACK's drm2. This scaling ensures that the
   // argument of each call to std::pow is <= 1 to avoid overflow.
   if (0 == size)
   {
      return 0.0;
   } // end if 0 == size

   if (1 == size)
   {
      return std::abs(data[0]);
   } // end if 1 == size

   double scale = 0.0;
   double sum = 0.0;

   for (int i = 0; i < size; i++)
   {
      if (data[i] != 0.0)
      {
         const double absdata = std::abs(data[i]);
         if (scale <= absdata)
         {
            const double sqr_arg = scale / absdata;
            sum = 1.0 + sum * (sqr_arg * sqr_arg);
            scale = absdata;
            continue;
         } // end if scale <= absdata
         const double sqr_arg = absdata / scale;
         sum += (sqr_arg * sqr_arg); // else scale > absdata
      } // end if data[i] != 0
   }
   return scale * std::sqrt(sum);
}

double Vector::Normlinf() const
{
   MFEM_GPU_CANNOT_PASS;
   double max = 0.0;
   for (int i = 0; i < size; i++)
   {
      max = std::max(std::abs(data[i]), max);
   }
   return max;
}

double Vector::Norml1() const
{
   MFEM_GPU_CANNOT_PASS;
   double sum = 0.0;
   for (int i = 0; i < size; i++)
   {
      sum += std::abs(data[i]);
   }
   return sum;
}

double Vector::Normlp(double p) const
{
   MFEM_GPU_CANNOT_PASS;
   MFEM_ASSERT(p > 0.0, "Vector::Normlp");
   if (p == 1.0)
   {
      return Norml1();
   }
   if (p == 2.0)
   {
      return Norml2();
   }
   if (p < infinity())
   {
      // Scale entries of Vector on the fly, using algorithms from
      // std::hypot() and LAPACK's drm2. This scaling ensures that the
      // argument of each call to std::pow is <= 1 to avoid overflow.
      if (0 == size)
      {
         return 0.0;
      } // end if 0 == size

      if (1 == size)
      {
         return std::abs(data[0]);
      } // end if 1 == size

      double scale = 0.0;
      double sum = 0.0;

      for (int i = 0; i < size; i++)
      {
         if (data[i] != 0.0)
         {
            const double absdata = std::abs(data[i]);
            if (scale <= absdata)
            {
               sum = 1.0 + sum * std::pow(scale / absdata, p);
               scale = absdata;
               continue;
            } // end if scale <= absdata
            sum += std::pow(absdata / scale, p); // else scale > absdata
         } // end if data[i] != 0
      }
      return scale * std::pow(sum, 1.0/p);
   } // end if p < infinity()

   return Normlinf(); // else p >= infinity()
}

double Vector::Max() const
{
   MFEM_GPU_CANNOT_PASS;
   double max = data[0];

   for (int i = 1; i < size; i++)
      if (data[i] > max)
      {
         max = data[i];
      }

   return max;
}

double Vector::Min() const
{
   MFEM_GPU_CANNOT_PASS;
   double min = data[0];

   for (int i = 1; i < size; i++)
      if (data[i] < min)
      {
         min = data[i];
      }

   return min;
}

double Vector::Sum() const
{
   MFEM_GPU_CANNOT_PASS;
   double sum = 0.0;

   for (int i = 0; i < size; i++)
   {
      sum += data[i];
   }

   return sum;
}

#ifdef MFEM_USE_SUNDIALS

#ifndef SUNTRUE
#define SUNTRUE TRUE
#endif
#ifndef SUNFALSE
#define SUNFALSE FALSE
#endif

Vector::Vector(N_Vector nv)
{
   N_Vector_ID nvid = N_VGetVectorID(nv);
   switch (nvid)
   {
      case SUNDIALS_NVEC_SERIAL:
         SetDataAndSize(NV_DATA_S(nv), NV_LENGTH_S(nv));
         break;
#ifdef MFEM_USE_MPI
      case SUNDIALS_NVEC_PARALLEL:
         SetDataAndSize(NV_DATA_P(nv), NV_LOCLENGTH_P(nv));
         break;
      case SUNDIALS_NVEC_PARHYP:
      {
         hypre_Vector *hpv_local = N_VGetVector_ParHyp(nv)->local_vector;
         SetDataAndSize(hpv_local->data, hpv_local->size);
         break;
      }
#endif
      default:
         MFEM_ABORT("N_Vector type " << nvid << " is not supported");
   }
}

void Vector::ToNVector(N_Vector &nv)
{
   MFEM_ASSERT(nv, "N_Vector handle is NULL");
   N_Vector_ID nvid = N_VGetVectorID(nv);
   switch (nvid)
   {
      case SUNDIALS_NVEC_SERIAL:
         MFEM_ASSERT(NV_OWN_DATA_S(nv) == SUNFALSE, "invalid serial N_Vector");
         NV_DATA_S(nv) = data;
         NV_LENGTH_S(nv) = size;
         break;
#ifdef MFEM_USE_MPI
      case SUNDIALS_NVEC_PARALLEL:
         MFEM_ASSERT(NV_OWN_DATA_P(nv) == SUNFALSE, "invalid parallel N_Vector");
         NV_DATA_P(nv) = data;
         NV_LOCLENGTH_P(nv) = size;
         break;
      case SUNDIALS_NVEC_PARHYP:
      {
         hypre_Vector *hpv_local = N_VGetVector_ParHyp(nv)->local_vector;
         MFEM_ASSERT(hpv_local->owns_data == false, "invalid hypre N_Vector");
         hpv_local->data = data;
         hpv_local->size = size;
         break;
      }
#endif
      default:
         MFEM_ABORT("N_Vector type " << nvid << " is not supported");
   }
}

#endif // MFEM_USE_SUNDIALS

}
