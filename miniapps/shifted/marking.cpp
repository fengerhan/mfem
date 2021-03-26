// Copyright (c) 2010-2021, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "marking.hpp"

namespace mfem
{

void ShiftedFaceMarker::MarkElements(Array<int> &elem_marker) const
{
   elem_marker.SetSize(pmesh.GetNE() + pmesh.GetNSharedFaces());
   elem_marker = SBElementType::INSIDE;

   IntegrationRules IntRulesLo(0, Quadrature1D::GaussLobatto);

   Vector vals;
   // Check elements on the current MPI rank
   for (int i = 0; i < pmesh.GetNE(); i++)
   {
      ElementTransformation *Tr = pmesh.GetElementTransformation(i);
      const IntegrationRule &ir =
         IntRulesLo.Get(pmesh.GetElementBaseGeometry(i), 4*Tr->OrderJ());
      ls_func.GetValues(i, ir, vals);

      int count = 0;
      for (int j = 0; j < ir.GetNPoints(); j++)
      {
         if (vals(j) <= 0.) { count++; }
      }

      if (count == ir.GetNPoints()) // completely outside
      {
         elem_marker[i] = SBElementType::OUTSIDE;
      }
      else if (count > 0) // partially outside
      {
         elem_marker[i] = SBElementType::CUT;
      }
   }

   // Check neighbors on the adjacent MPI rank
   for (int i = pmesh.GetNE(); i < pmesh.GetNE()+pmesh.GetNSharedFaces(); i++)
   {
      int shared_fnum = i-pmesh.GetNE();
      FaceElementTransformations *tr =
         pmesh.GetSharedFaceTransformations(shared_fnum);
      int Elem2NbrNo = tr->Elem2No - pmesh.GetNE();

      ElementTransformation *eltr =
         pmesh.GetFaceNbrElementTransformation(Elem2NbrNo);
      const IntegrationRule &ir =
         IntRulesLo.Get(pmesh.GetElementBaseGeometry(0),
                        4*eltr->OrderJ());

      const int nip = ir.GetNPoints();
      vals.SetSize(nip);
      int count = 0;
      for (int j = 0; j < nip; j++)
      {
         const IntegrationPoint &ip = ir.IntPoint(j);
         vals[j] = ls_func.GetValue(tr->Elem2No, ip);
         if (vals[j] <= 0.) { count++; }
      }

      if (count == ir.GetNPoints()) // completely outside
      {
         elem_marker[i] = SBElementType::OUTSIDE;
      }
      else if (count > 0) // partially outside
      {
         elem_marker[i] = SBElementType::CUT;
      }
   }
}

void ShiftedFaceMarker::ListShiftedFaceDofs(const Array<int> &elem_marker,
                                            Array<int> &sface_dof_list)
{
   sface_dof_list.DeleteAll();
   Array<int> dofs; // work array

   // First we check interior faces of the mesh (excluding interior faces that
   // are on the processor boundaries)
   for (int f = 0; f < pmesh.GetNumFaces(); f++)
   {
      FaceElementTransformations *tr = pmesh.GetInteriorFaceTransformations(f);
      if (tr != NULL)
      {
         int te1 = elem_marker[tr->Elem1No], te2 = elem_marker[tr->Elem2No];
         if (!include_cut_cell &&
             te1 == ShiftedFaceMarker::CUT && te2 == ShiftedFaceMarker::INSIDE)
         {
            pfes_sltn.GetFaceDofs(f, dofs);
            sface_dof_list.Append(dofs);
         }
         if (!include_cut_cell &&
             te1 == ShiftedFaceMarker::INSIDE && te2 == ShiftedFaceMarker::CUT)
         {
            pfes_sltn.GetFaceDofs(f, dofs);
            sface_dof_list.Append(dofs);
         }
         if (include_cut_cell &&
             te1 == SBElementType::CUT && te2 == SBElementType::OUTSIDE)
         {
            pfes_sltn.GetFaceDofs(f, dofs);
            sface_dof_list.Append(dofs);
         }
         if (include_cut_cell &&
             te1 == SBElementType::OUTSIDE && te2 == SBElementType::CUT)
         {
            pfes_sltn.GetFaceDofs(f, dofs);
            sface_dof_list.Append(dofs);
         }
      }
   }

   // Here we add boundary faces that we want to model as SBM faces and
   // change the attribute of these faces.
   ess_bdr.SetSize(pmesh.bdr_attributes.Max());
   int pmesh_bdr_attr_max = 0;
   if (ess_bdr.Size())
   {
      pmesh_bdr_attr_max = pmesh.bdr_attributes.Max();
      ess_bdr = 1;
   }
   bool sbm_at_true_boundary = false;
   if (include_cut_cell)
   {
      for (int i = 0; i < pmesh.GetNBE(); i++)
      {
         int attr = pmesh.GetBdrAttribute(i);
         FaceElementTransformations *tr;
         tr = pmesh.GetBdrFaceTransformations (i);
         if (tr != NULL)
         {
            int ne1 = tr->Elem1No;
            int te1 = elem_marker[ne1];
            const int faceno = pmesh.GetBdrFace(i);
            if (te1 == SBElementType::CUT)
            {
               pfes_sltn.GetFaceDofs(faceno, dofs);
               sface_dof_list.Append(dofs);
               pmesh.SetBdrAttribute(i, pmesh_bdr_attr_max+1);
               sbm_at_true_boundary = true;
            }
         }
      }
   }
   if (sbm_at_true_boundary) {
       ess_bdr.Append(0);
       pmesh.SetAttributes();
   }

   // Now we add interior faces that are on processor boundaries.
   for (int i = 0; i < pmesh.GetNSharedFaces(); i++)
   {
      FaceElementTransformations *tr = pmesh.GetSharedFaceTransformations(i);
      if (tr != NULL)
      {
         int ne1 = tr->Elem1No;
         int te1 = elem_marker[ne1];
         int te2 = elem_marker[i+pmesh.GetNE()];
         const int faceno = pmesh.GetSharedFace(i);
         // Add if the element on this proc is completely inside the domain
         // and the the element on other proc is not
         if (!include_cut_cell &&
             te2 == ShiftedFaceMarker::CUT && te1 == ShiftedFaceMarker::INSIDE)
         {
            pfes_sltn.GetFaceDofs(faceno, dofs);
            sface_dof_list.Append(dofs);
         }
         if (include_cut_cell &&
             te2 == SBElementType::OUTSIDE && te1 == SBElementType::CUT)
         {
            pfes_sltn.GetFaceDofs(faceno, dofs);
            sface_dof_list.Append(dofs);
         }
      }
   }
}

// Determine the list of true (i.e. conforming) essential boundary dofs.
// To do this, we first make a list of all dofs that are on the real boundary
// of the mesh, then add all the dofs of the elements that are completely
// outside or intersect shifted boundary. Then we remove the dofs from
// SBM faces
void ShiftedFaceMarker::ListEssentialTDofs(const Array<int> &elem_marker,
                                           const Array<int> &sface_dof_list,
                                           Array<int> &ess_tdof_list,
                                           Array<int> &ess_shift_bdr) const
{
   // Make a list of dofs on all boundaries
   ess_shift_bdr.SetSize(ess_bdr.Size());
   if (pmesh.bdr_attributes.Size())
   {
      for (int i = 0; i < ess_bdr.Size(); i++)
      {
         ess_shift_bdr[i] = 1 - ess_bdr[i];
      }
   }
   Array<int> ess_vdofs_bdr;
   pfes_sltn.GetEssentialVDofs(ess_bdr, ess_vdofs_bdr);

   // Get all dofs associated with elements outside the domain or intersected
   // by the boundary.
   Array<int> ess_vdofs(ess_vdofs_bdr.Size()), dofs;
   ess_vdofs = 0;
   for (int e = 0; e < pmesh.GetNE(); e++)
   {
       if (!include_cut_cell &&
           (elem_marker[e] == SBElementType::OUTSIDE ||
            elem_marker[e] == SBElementType::CUT))
       {
         pfes_sltn.GetElementVDofs(e, dofs);
         for (int i = 0; i < dofs.Size(); i++)
         {
            ess_vdofs[dofs[i]] = -1;
         }
      }
      if (include_cut_cell &&
          elem_marker[e] == SBElementType::OUTSIDE)
      {
         pfes_sltn.GetElementVDofs(e, dofs);
         for (int i = 0; i < dofs.Size(); i++)
         {
            ess_vdofs[dofs[i]] = -1;
         }
      }
   }

   // Combine the lists to mark essential dofs.
   for (int i = 0; i < ess_vdofs.Size(); i++)
   {
      if (ess_vdofs_bdr[i] == -1) { ess_vdofs[i] = -1; }
   }

   // Unmark dofs that are on SBM faces (but not on dirichlet boundaries)
   for (int i = 0; i < sface_dof_list.Size(); i++)
   {
      if (ess_vdofs_bdr[sface_dof_list[i]] != -1)
      {
         ess_vdofs[sface_dof_list[i]] = 0;
      }
   }

   // Synchronize
   for (int i = 0; i < ess_vdofs.Size() ; i++) { ess_vdofs[i] += 1; }
   pfes_sltn.Synchronize(ess_vdofs);
   for (int i = 0; i < ess_vdofs.Size() ; i++) { ess_vdofs[i] -= 1; }

   // Convert to tdofs
   Array<int> ess_tdofs;
   pfes_sltn.GetRestrictionMatrix()->BooleanMult(ess_vdofs, ess_tdofs);
   pfes_sltn.MarkerToList(ess_tdofs, ess_tdof_list);
}

}
