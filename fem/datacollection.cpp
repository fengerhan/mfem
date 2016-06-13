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

#include "fem.hpp"
#include "picojson.h"

#include <fstream>
#include <cerrno>      // errno
#ifndef _WIN32
#include <sys/stat.h>  // mkdir
#else
#include <direct.h>    // _mkdir
#define mkdir(dir, mode) _mkdir(dir)
#endif

#ifdef MFEM_USE_SIDRE
#include "sidre/sidre.hpp"
#endif // MFEM_USE_SIDRE

namespace mfem
{

using namespace std;

// Helper string functions. Will go away in C++11

string to_string(int i)
{
   stringstream ss;
   ss << i;

   // trim leading spaces
   string out_str = ss.str();
   out_str = out_str.substr(out_str.find_first_not_of(" \t"));
   return out_str;
}

string to_padded_string(int i, int digits)
{
   ostringstream oss;
   oss << setw(digits) << setfill('0') << i;
   return oss.str();
}

int to_int(string str)
{
   int i;
   stringstream(str) >> i;
   return i;
}

// class DataCollection implementation

DataCollection::DataCollection(const char *collection_name)
{
   name = collection_name;
   // leave prefix_path empty
   mesh = NULL;
   myid = 0;
   num_procs = 1;
   serial = true;
   own_data = false;
   cycle = -1;
   time = 0.0;
   precision = precision_default;
   pad_digits = pad_digits_default;
   error = NO_ERROR;
}

DataCollection::DataCollection(const char *collection_name, Mesh *_mesh)
{
   name = collection_name;
   // leave prefix_path empty
   mesh = _mesh;
   myid = 0;
   num_procs = 1;
   serial = true;
#ifdef MFEM_USE_MPI
   ParMesh *par_mesh = dynamic_cast<ParMesh*>(mesh);
   if (par_mesh)
   {
      myid = par_mesh->GetMyRank();
      num_procs = par_mesh->GetNRanks();
      serial = false;
   }
#endif
   own_data = false;
   cycle = -1;
   time = 0.0;
   precision = precision_default;
   pad_digits = pad_digits_default;
   error = NO_ERROR;
}

void DataCollection::SetMesh(Mesh *new_mesh)
{
   if (own_data) { delete mesh; }
   mesh = new_mesh;
   myid = 0;
   num_procs = 1;
   serial = true;
#ifdef MFEM_USE_MPI
   ParMesh *par_mesh = dynamic_cast<ParMesh*>(mesh);
   if (par_mesh)
   {
      myid = par_mesh->GetMyRank();
      num_procs = par_mesh->GetNRanks();
      serial = false;
   }
#endif
}

void DataCollection::RegisterField(const char* name, GridFunction *gf)
{
   if (own_data && HasField(name))
   {
      delete field_map[name];
   }
   field_map[name] = gf;
}

GridFunction *DataCollection::GetField(const char *field_name)
{
   if (HasField(field_name))
   {
      return field_map[field_name];
   }
   else
   {
      return NULL;
   }
}

void DataCollection::SetPrefixPath(const char *prefix)
{
   if (prefix)
   {
      prefix_path = prefix;
      if (!prefix_path.empty() && prefix_path[prefix_path.size()-1] != '/')
      {
         prefix_path += '/';
      }
   }
   else
   {
      prefix_path.clear();
   }
}

void DataCollection::Save()
{
   SaveMesh();

   if (error) { return; }

   for (map<string,GridFunction*>::iterator it = field_map.begin();
        it != field_map.end(); ++it)
   {
      SaveOneField(it);
   }
}

static int create_directory(const string &dir_name, const Mesh *mesh, int myid)
{
   int err;
#ifndef MFEM_USE_MPI
   err = mkdir(dir_name.c_str(), 0777);
   err = (err && (errno != EEXIST)) ? 1 : 0;
#else
   const ParMesh *pmesh = dynamic_cast<const ParMesh*>(mesh);
   if (myid == 0 || pmesh == NULL)
   {
      err = mkdir(dir_name.c_str(), 0777);
      err = (err && (errno != EEXIST)) ? 1 : 0;
      if (pmesh)
      {
         MPI_Bcast(&err, 1, MPI_INT, 0, pmesh->GetComm());
      }
   }
   else
   {
      // Wait for rank 0 to create the directory
      MPI_Bcast(&err, 1, MPI_INT, 0, pmesh->GetComm());
   }
#endif
   return err;
}

void DataCollection::SaveMesh()
{
   int err;
   if (!prefix_path.empty())
   {
      err = create_directory(prefix_path, mesh, myid);
      if (err)
      {
         error = WRITE_ERROR;
         MFEM_WARNING("Error creating directory: " << prefix_path);
         return; // do not even try to write the mesh
      }
   }

   string dir_name = prefix_path;
   if (cycle == -1)
   {
      dir_name += name;
   }
   else
   {
      dir_name += name + "_" + to_padded_string(cycle, pad_digits);
   }
   err = create_directory(dir_name, mesh, myid);
   if (err)
   {
      error = WRITE_ERROR;
      MFEM_WARNING("Error creating directory: " << dir_name);
      return; // do not even try to write the mesh
   }

   string mesh_name;
   if (serial)
   {
      mesh_name = dir_name + "/mesh";
   }
   else
   {
      mesh_name = dir_name + "/mesh." + to_padded_string(myid, pad_digits);
   }
   ofstream mesh_file(mesh_name.c_str());
   mesh_file.precision(precision);
   mesh->Print(mesh_file);
   if (!mesh_file)
   {
      error = WRITE_ERROR;
      MFEM_WARNING("Error writing mesh to file: " << mesh_name);
   }
}

void DataCollection::SaveOneField(
   const std::map<std::string,GridFunction*>::iterator &it)
{
   string dir_name = prefix_path;
   if (cycle == -1)
   {
      dir_name += name;
   }
   else
   {
      dir_name += name + "_" + to_padded_string(cycle, pad_digits);
   }

   string file_name;
   if (serial)
   {
      file_name = dir_name + "/" + it->first;
   }
   else
   {
      file_name = dir_name + "/" + it->first + "." +
                  to_padded_string(myid, pad_digits);
   }
   ofstream field_file(file_name.c_str());
   field_file.precision(precision);
   (it->second)->Save(field_file);
   if (!field_file)
   {
      error = WRITE_ERROR;
      MFEM_WARNING("Error writting field to file: " << it->first);
   }
}

void DataCollection::SaveField(const char *field_name)
{
   const map<string,GridFunction*>::iterator it = field_map.find(field_name);
   if (it != field_map.end())
   {
      SaveOneField(it);
   }
}

void DataCollection::DeleteData()
{
   if (own_data)
   {
      delete mesh;
   }
   mesh = NULL;
   for (map<string,GridFunction*>::iterator it = field_map.begin();
        it != field_map.end(); ++it)
   {
      if (own_data)
      {
         delete it->second;
      }
      it->second = NULL;
   }
   own_data = false;
}

void DataCollection::DeleteAll()
{
   DeleteData();
   field_map.clear();
}

DataCollection::~DataCollection()
{
   if (own_data)
   {
      delete mesh;
      for (map<string,GridFunction*>::iterator it = field_map.begin();
           it != field_map.end(); ++it)
      {
         delete it->second;
      }
   }
}


// class VisItDataCollection implementation

VisItDataCollection::VisItDataCollection(const char *collection_name)
   : DataCollection(collection_name)
{
   serial = false; // always include rank in file names
   cycle  = 0;     // always include cycle in directory names

   spatial_dim = 0;
   topo_dim = 0;
   visit_max_levels_of_detail = 32;
}

VisItDataCollection::VisItDataCollection(const char *collection_name,
                                         Mesh *mesh)
   : DataCollection(collection_name, mesh)
{
   serial = false; // always include rank in file names
   cycle  = 0;     // always include cycle in directory names

   spatial_dim = mesh->SpaceDimension();
   topo_dim = mesh->Dimension();
   visit_max_levels_of_detail = 32;
}

void VisItDataCollection::SetMesh(Mesh *new_mesh)
{
   DataCollection::SetMesh(new_mesh);
   serial = false; // always include rank in file names

   spatial_dim = mesh->SpaceDimension();
   topo_dim = mesh->Dimension();
}

void VisItDataCollection::RegisterField(const char* name, GridFunction *gf)
{
   DataCollection::RegisterField(name, gf);
   field_info_map[name] = VisItFieldInfo("nodes", gf->VectorDim());
}

void VisItDataCollection::SetMaxLevelsOfDetail(int max_levels_of_detail)
{
   visit_max_levels_of_detail = max_levels_of_detail;
}

void VisItDataCollection::DeleteAll()
{
   field_info_map.clear();
   DataCollection::DeleteAll();
}

void VisItDataCollection::Save()
{
   DataCollection::Save();
   SaveRootFile();
}

void VisItDataCollection::SaveRootFile()
{
   if (myid == 0)
   {
      string root_name = prefix_path + name + "_" +
                         to_padded_string(cycle, pad_digits) + ".mfem_root";
      ofstream root_file(root_name.c_str());
      root_file << GetVisItRootString();
      if (!root_file)
      {
         error = WRITE_ERROR;
         MFEM_WARNING("Error writting VisIt Root file: " << root_name);
      }
   }
}

void VisItDataCollection::Load(int _cycle)
{
   DeleteAll();
   cycle = _cycle;
   string root_name = prefix_path + name + "_" +
                      to_padded_string(cycle, pad_digits) + ".mfem_root";
   LoadVisItRootFile(root_name);
   if (!error)
   {
      LoadMesh();
   }
   if (!error)
   {
      LoadFields();
   }
   if (!error)
   {
      own_data = true;
   }
   else
   {
      DeleteAll();
   }
}

void VisItDataCollection::LoadVisItRootFile(string root_name)
{
   ifstream root_file(root_name.c_str());
   stringstream buffer;
   buffer << root_file.rdbuf();
   if (!buffer)
   {
      error = READ_ERROR;
      MFEM_WARNING("Error reading the VisIt Root file: " << root_name);
   }
   else
   {
      ParseVisItRootString(buffer.str());
   }
}

void VisItDataCollection::LoadMesh()
{
   string mesh_fname = prefix_path + name + "_" +
                       to_padded_string(cycle, pad_digits) +
                       "/mesh." + to_padded_string(myid, pad_digits);
   ifstream file(mesh_fname.c_str());
   if (!file)
   {
      error = READ_ERROR;
      MFEM_WARNING("Unable to open mesh file: " << mesh_fname);
      return;
   }
   // TODO: 1) load parallel mesh on one processor
   //       2) load parallel mesh on the same number of processors
   mesh = new Mesh(file, 1, 1);
   spatial_dim = mesh->SpaceDimension();
   topo_dim = mesh->Dimension();
}

void VisItDataCollection::LoadFields()
{
   string path_left = prefix_path + name + "_" +
                      to_padded_string(cycle, pad_digits) + "/";
   string path_right = "." + to_padded_string(myid, pad_digits);

   field_map.clear();
   for (map<string,VisItFieldInfo>::iterator it = field_info_map.begin();
        it != field_info_map.end(); ++it)
   {
      string fname = path_left + it->first + path_right;
      ifstream file(fname.c_str());
      if (!file)
      {
         error = READ_ERROR;
         MFEM_WARNING("Unable to open field file: " << fname);
         return;
      }
      // TODO: 1) load parallel GridFunction on one processor
      //       2) load parallel GridFunction on the same number of processors
      field_map[it->first] = new GridFunction(mesh, file);
   }
}

string VisItDataCollection::GetVisItRootString()
{
   // Get the path string (relative to where the root file is, i.e. no prefix).
   string path_str = name + "_" + to_padded_string(cycle, pad_digits) + "/";

   // We have to build the json tree inside out to get all the values in there
   picojson::object top, dsets, main, mesh, fields, field, mtags, ftags;

   // Build the mesh data
   string file_ext_format = ".%0" + to_string(pad_digits) + "d";
   mtags["spatial_dim"] = picojson::value(to_string(spatial_dim));
   mtags["topo_dim"] = picojson::value(to_string(topo_dim));
   mtags["max_lods"] = picojson::value(to_string(visit_max_levels_of_detail));
   mesh["path"] = picojson::value(path_str + "mesh" + file_ext_format);
   mesh["tags"] = picojson::value(mtags);

   // Build the fields data entries
   for (map<string,VisItFieldInfo>::iterator it = field_info_map.begin();
        it != field_info_map.end(); ++it)
   {
      ftags["assoc"] = picojson::value((it->second).association);
      ftags["comps"] = picojson::value(to_string((it->second).num_components));
      field["path"] = picojson::value(path_str + it->first + file_ext_format);
      field["tags"] = picojson::value(ftags);
      fields[it->first] = picojson::value(field);
   }

   main["cycle"] = picojson::value(double(cycle));
   main["time"] = picojson::value(time);
   main["domains"] = picojson::value(double(num_procs));
   main["mesh"] = picojson::value(mesh);
   if (!field_info_map.empty())
   {
      main["fields"] = picojson::value(fields);
   }

   dsets["main"] = picojson::value(main);
   top["dsets"] = picojson::value(dsets);

   return picojson::value(top).serialize(true);
}

void VisItDataCollection::ParseVisItRootString(string json)
{
   picojson::value top, dsets, main, mesh, fields;
   string parse_err = picojson::parse(top, json);
   if (!parse_err.empty())
   {
      error = READ_ERROR;
      MFEM_WARNING("Unable to parse visit root data.");
      return;
   }

   // Process "main"
   dsets = top.get("dsets");
   main = dsets.get("main");
   cycle = int(main.get("cycle").get<double>());
   time = main.get("time").get<double>();
   num_procs = int(main.get("domains").get<double>());
   mesh = main.get("mesh");
   fields = main.get("fields");

   // ... Process "mesh"

   // Set the DataCollection::name using the mesh path
   string path = mesh.get("path").get<string>();
   size_t right_sep = path.find('_');
   if (right_sep == string::npos)
   {
      error = READ_ERROR;
      MFEM_WARNING("Unable to parse visit root data.");
      return;
   }
   name = path.substr(0, right_sep);

   spatial_dim = to_int(mesh.get("tags").get("spatial_dim").get<string>());
   topo_dim = to_int(mesh.get("tags").get("topo_dim").get<string>());
   visit_max_levels_of_detail =
      to_int(mesh.get("tags").get("max_lods").get<string>());

   // ... Process "fields"
   field_info_map.clear();
   if (fields.is<picojson::object>())
   {
      picojson::object fields_obj = fields.get<picojson::object>();
      for (picojson::object::iterator it = fields_obj.begin();
           it != fields_obj.end(); ++it)
      {
         picojson::value tags = it->second.get("tags");
         field_info_map[it->first] =
            VisItFieldInfo(tags.get("assoc").get<string>(),
                           to_int(tags.get("comps").get<string>()));
      }
   }
}

#ifdef MFEM_USE_SIDRE

// class SidreDataCollection implementation
// This version is a prototype of adding needed MFEM data to Sidre as mostly 'external' data.
// There are some exceptions - individual scalars are copied into Sidre, as long as we know the
// data does not change during a run.  There are some drawbacks to trying to do most data as 'external'
// to Sidre. (For future discussion)
SidreDataCollection::SidreDataCollection(const char *collection_name, Mesh * new_mesh, asctoolkit::sidre::DataGroup* dg)
  : DataCollection(collection_name, new_mesh)
{
  namespace sidre = asctoolkit::sidre;

  sidre_dc_group = dg->createGroup( std::string(collection_name) );

  // Create group for mesh
  sidre::DataGroup * mesh_grp = sidre_dc_group->createGroup("topology");
  addMesh(mesh_grp);

  if ( mesh->elements.Size() > 0)
  {
     // Create group for mesh elements, add material, shape, and vertex indices.
     sidre::DataGroup * mesh_elements_grp = mesh_grp->createGroup("mesh_elements");
     // Get the pointer to the internal elements array in Mesh.
     addElements(mesh_elements_grp, mesh->elements);
  }

  if ( mesh->boundary.Size() > 0)
  {
     // Create group for boundary elements, add material, shape, and vertex indices.
     sidre::DataGroup * boundary_elements_grp = mesh_grp->createGroup("boundary_elements");
     // Get the pointer to the internal boundary elements array in Mesh.
     addElements(boundary_elements_grp, mesh->boundary);
  }

  // Add mesh vertices
  sidre::DataGroup * grp = mesh_grp->createGroup("coords");
  if (mesh->vertices.Size() > 0)
  {
     addVertices(grp);
  }

  // If a grid function is present defining higher order nodes, add that too.
  if (mesh->GetNodes() != NULL)
  {
    sidre::DataGroup * grp = mesh_grp->createGroup("nodes");
    addField(grp, mesh->GetNodes() );
  }

}

void SidreDataCollection::RegisterField(const char* name, GridFunction *gf)
{
  DataCollection::RegisterField(name, gf);
  addField(sidre_dc_group->createGroup(name), gf);
}

// Private helper functions
void SidreDataCollection::addElements( asctoolkit::sidre::DataGroup * group, Array<Element *>& elements )
{
  // NOTE:
  // This is just a first pass at adding element data.
  // It is not optimal, as we are making one entry per element.
  // If MFEM can guarantee that all the elements are contiguous in memory, we could rework this
  // to be one view with a stride.
  // TODO: Look at code that creates these vertices classes and talk to MFEM team.
  // TODO: The striding strategy would only work if all elements are of the same type ( same # of indices ).
  // Veselin mentioned that tets do have an optimization where they are all allocated from a large
  // block.

  namespace sidre = asctoolkit::sidre;
  using sidre::detail::SidreTT;

  group->createView("number")->setScalar( elements.Size() );
  // TODO - Need a helper function to map element shape id to a string name.
  // For now put in the int value.
  // Assume all elements are same type ( for this prototype only ).
  // Add the hex's only.

  group->createView("shape")->setString("hexs");
  sidre::DataView * conn_view = group->createView("connectivity");

  // This can easily be converted to use a data buffer prepared by the datastore instead of a stl vector in mfem.
  // TODO - Discuss refactoring material attributes as a field with MFEM team.
  conn_view->setExternalDataPtr( &Quadrilateral::all_indices[0] );
  conn_view-> apply( SidreTT<mfem_int_t>::id, Quadrilateral::all_indices.size() );

  // Have to build array of material_attributes, unless we want # elems entries in datastore.
  // Unless, the data structure is changed in mfem.
  sidre::DataView * attributes_view = group->createView("material_attributes", SidreTT<mfem_int_t>::id,
    elements.Size() ) -> allocate();
  mfem_int_t * attributes_ptr = attributes_view->getArray();

  for (int i = 0; i < elements.Size(); i++)
  {
    attributes_ptr[i] = elements[i]->GetAttribute();
  }
}

void SidreDataCollection::addField(asctoolkit::sidre::DataGroup * grp, GridFunction* gf)
{
  if (gf->Size() == 0)
  {
    return;
  }

  namespace sidre = asctoolkit::sidre;

  grp->createView("type")->setString("FiniteElementSpace");
  grp->createView("name")->setString( gf->FESpace()->FEColl()->Name() );
// the grid function save goes a bit different route on the vector dim.  It grabs it from the finite element space.  Ask Rob or Tzanio about this.
// calling the frunction on the grid function looks correct.
  grp->createView("dimension")->setScalar( gf->VectorDim() );

  sidre::DataView * ordering_view = grp->createView("ordering");

  // Add ordering
  // Ordering::byNODES - first nodes, then vector dimension,
  // Ordering::byVDIM  - first vector dimension, then nodes  */

  if ( gf->FESpace()->GetOrdering() == Ordering::byNODES )
  {
    ordering_view->setString("byNode");
  }
  else
  {
    ordering_view->setString("byVDim");
  }
  // TODO - ask if MFEM team cares about typedefs.  Could hard code SIDRE_DOUBLE_ID here instead.
  grp->createView("data")->setExternalDataPtr( const_cast<mfem_double_t*>(
      gf->GetData()) )->apply( sidre::detail::SidreTT<mfem_double_t>::id ,
      gf->Size());
}

void SidreDataCollection::addMesh(asctoolkit::sidre::DataGroup * grp)
{
// should consider adding version members for mesh  in mfem
//  grp->createView("version")->setScalar( ncmesh ? 1.1 : 1.0 );
  grp->createView("type")->setString("unstructured");
  grp->createView("dimension")->setScalar(mesh->Dim);
}

void SidreDataCollection::addVertices(asctoolkit::sidre::DataGroup * grp)
{
  // NOTE:
  // This is just a first pass at adding the data.
  // It is not optimal, as we are making one entry per vertex.
  // TODO: Rework this to be one view with a stride, if we can verify the vertices are contiguous in memory.
  namespace sidre = asctoolkit::sidre;

  MFEM_VERIFY( mesh->Dim == 2 || mesh->Dim == 3, "Expected two or three dimensions." );

  grp->createView("type")->setString( "explicit" );

  // Create single view for all vertices.  An array of length # of vertices
  //   * size of vertex class ( 3 doubles ) is sufficient.
  // This will break if the number of vertices EVER changes in mfem - so check with Tzanio...
  // will need to rethink this for AMR
  size_t total_length = mesh->vertices.Size() * 3;

  // apply args are 'type, num_elements, offset, stride'
  // Note - the 'z' value might be empty if this is 2D.
  grp->createView("xyz", mesh->vertices[0]())->apply(sidre::DOUBLE_ID, total_length );

  // For restarts we really don't need these separate x, y, z arrays... but the mesh blueprint
  // would like them...
  // I think this is the first example of the blueprint wanting one thing, and restart wants
  // another.
  //grp->createView("x", mesh->vertices[0]())->apply(sidre::DOUBLE_ID, total_length, 0, 3);
  //grp->createView("y", mesh->vertices[0]())->apply(sidre::DOUBLE_ID, total_length, 1, 3);

  //if ( mesh->Dim == 3 )
  //{
  //  grp->createView("z", mesh->vertices[0]())->apply(sidre::DOUBLE_ID, total_length, 2, 3);
  //}
}

#endif // MFEM_USE_SIDRE

}  // end namespace MFEM

