/*
This file is part of the Ristra portage project.
Please see the license file at the root of this repository, or at:
    https://github.com/laristra/portage/blob/master/LICENSE
*/


#ifndef PORTAGE_INTERSECT_INTERSECT_R2D_H_
#define PORTAGE_INTERSECT_INTERSECT_R2D_H_

#include <array>
#include <stdexcept>
#include <vector>
#include <algorithm>
#ifdef PORTAGE_DEBUG
#include <sstream>
#endif

#include "wonton/support/wonton.h"
#include "wonton/support/Point.h"
#include "wonton/support/Polytope.h"

// portage includes
extern "C" {
#include "r2d.h"
}
#include "portage/support/portage.h"
#include "portage/intersect/dummy_interface_reconstructor.h"
#include "portage/intersect/intersect_polys_r2d.h"

#ifdef PORTAGE_HAS_TANGRAM
#include "tangram/driver/CellMatPoly.h"
#include "tangram/driver/driver.h"
#include "tangram/support/MatPoly.h"
#endif 



namespace Portage {

using Wonton::SIDE;
using Wonton::WEDGE;
using Wonton::ALL;

// Check if polygon is truly convex (not just star convex)
// Move to Wonton

static inline
bool poly2_is_convex(std::vector<Wonton::Point<2>> const& pverts,
                     NumericTolerances_t const& num_tols) {

  int npverts = pverts.size();
  for (int i = 0; i < npverts; i++) {
    
    // Compute distance from pverts[i+1] to segment (pverts[i], pverts[i+2])
    int ifv = i, imv = (i+1)%npverts, isv = (i+2)%npverts;
    Wonton::Vector<2> normal( pverts[isv][1] - pverts[ifv][1], 
                             -pverts[isv][0] + pverts[ifv][0]);
    normal.normalize();
    Wonton::Vector<2> fv2mv = pverts[imv] - pverts[ifv];
    double dst = Wonton::dot(fv2mv, normal);
    
    if (dst <= -num_tols.min_absolute_distance)
      return false;
  }

  return true;
}

#ifdef PORTAGE_DEBUG
static inline
void throw_validity_error_2d(Wonton::Entity_kind ekind, int entity_id,
                             bool in_source_mesh,
                             double tri_area, double entity_area) {
  std::stringstream sstr;
  std::string ekind_str;
  std::string mesh_str = in_source_mesh ? "source" : "target";
  if (ekind == Wonton::CELL)
    ekind_str = "cell";
  else if (ekind == Wonton::NODE)
    ekind_str = "dual cell";

  sstr << "In intersect_r2d.h: " <<
      "Triangle in decomposition of " << mesh_str << " " << ekind_str <<
      " " << entity_id << " has a negative area of " << tri_area << "\n";
  if (entity_area <= 0.0)
    sstr << "The " << ekind_str << " (area = " << entity_area << ") is inside out or degenerate";
  else
    sstr << "The " << ekind_str << " (area = " << entity_area << ") may be highly non-convex";
  throw std::runtime_error(sstr.str());
}  // throw_validity_error_2d
#endif



///
/// \class IntersectR2D  2-D intersection algorithm
///
/// This file contains a class for intersection of 2D cells (or dual
/// cells) against candidate cells (or dual cells). Before we do the
/// intersection, we check if the cells (or dual cells) are
/// valid. Eventually, these checks will be moved into the mesh class
/// but that requires an API change so for the moment we will leave
/// them here, active only for Debug executables.
///


// There are few criteria that could be used to evaluate whether the
// polygons/polyhedra are suitable for use in the intersection using
// R2D/R3D's clipping routines:
//
// 1. "VALIDITY" OF SOURCE CELL: The source polyhedron (from a cell, a
// dual cell or a material polyhedron) can be non-convex since R2D/R3D
// can clip non-convex cells. We may, however, want to verify that the
// source polyhedron is "valid" before computing with it (although the
// way R2D/R3D are written and the interpolate routines are written,
// there may be nothing to preclude us from computing with negative
// intersection volumes). We can test the validity of a source
// polyhedron by (a) checking that its volume computed using
// divergence theorem (by integrating over its boundary) is positive
// (b) verifying that it is star-convex i.e. allows a decomposition
// into positive volume simplices.
//
// 2. STAR-CONVEXITY OF TARGET CELL: The target cell can be non-convex
// but it MUST BE STAR-CONVEX, i.e. allow a decomposition into
// positive volume simplices. Since each simplex is convex, one can
// clip the source cell with the planes of each simplex to get the
// intersection volume between the simplex and the source cell. The
// intersection volumes from all the simplices of the decomposition
// can be summed up to get the total intersection volume.
//
//
// Summary: For the general code flow we will use the divergence
// theorem check in poly2_is_convex to determine if we should swap
// source and target polygons. In DEBUG mode, however, we will use the
// star-convexity check for both the source and the target polyhedra
// as it's the more stringent check. For cells or dual cells, this
// decomposition is already available in the form of wedges. Material
// polygons/polyhedra from interface reconstruction, on the other
// hand, are tested using the divergence theorem since they already
// undergo strict checks in the interface reconstruction
// procedure. Also, in 3D they are guaranteed to be convex since a
// non-convex cell is decomposed into simplices and then sliced by the
// interface plane.



template <Entity_kind on_what, class SourceMeshType,
          class SourceStateType, class TargetMeshType,
          template<class, int, class, class> class InterfaceReconstructorType =
          DummyInterfaceReconstructor,
          class Matpoly_Splitter = void,
          class Matpoly_Clipper = void>
class IntersectR2D {

#ifdef PORTAGE_HAS_TANGRAM
  using InterfaceReconstructor2D =
      Tangram::Driver<InterfaceReconstructorType, 2, SourceMeshType,
                      Matpoly_Splitter, Matpoly_Clipper>;
#endif

 public:

#ifdef PORTAGE_HAS_TANGRAM
  /// Constructor with interface reconstructor

  IntersectR2D(SourceMeshType const & source_mesh,
               SourceStateType const & source_state,
               TargetMeshType const & target_mesh,
               NumericTolerances_t num_tols,
               std::shared_ptr<InterfaceReconstructor2D> ir)
      : sourceMeshWrapper(source_mesh), sourceStateWrapper(source_state),
        targetMeshWrapper(target_mesh), interface_reconstructor(ir),
        num_tols_(num_tols) {}
#endif

  /// Constructor WITHOUT interface reconstructor

  IntersectR2D(SourceMeshType const & source_mesh,
               SourceStateType const & source_state,
               TargetMeshType const & target_mesh,
               NumericTolerances_t num_tols)
      : sourceMeshWrapper(source_mesh), sourceStateWrapper(source_state),
        targetMeshWrapper(target_mesh), num_tols_(num_tols) {}

  /// \brief Set the source mesh material that we have to intersect against

  void set_material(int m) {
    matid_ = m;
  }

  /// \brief Intersect control volume of a target entity with control volumes of a set of source entities
  /// \param[in] tgt_entity  Entity of target mesh to intersect
  /// \param[in] src_entities Entities of source mesh to intersect against
  /// \return vector of Weights_t structure containing moments of intersection

  std::vector<Weights_t>
  operator() (const int tgt_entity, std::vector<int> const& src_entities) const {
    throw std::runtime_error("IntersectR3D not implemented for this entity type");
  }

  IntersectR2D() = delete;

  /// Assignment operator (disabled)
  IntersectR2D & operator = (const IntersectR2D &) = delete;

 private:
  SourceMeshType const & sourceMeshWrapper;
  SourceStateType const & sourceStateWrapper;
  TargetMeshType const & targetMeshWrapper;
  int matid_ = -1;
#ifdef PORTAGE_HAS_TANGRAM
  std::shared_ptr<InterfaceReconstructor2D> interface_reconstructor;
#endif
  NumericTolerances_t num_tols_;
};  // class IntersectR2D



//////////////////////////////////////////////////////////////////////////////
// Specialization of Intersect2D class for cells

template <class SourceMeshType, class SourceStateType,
          class TargetMeshType,
          template <class, int, class, class> class InterfaceReconstructorType,
          class Matpoly_Splitter, class Matpoly_Clipper>
class IntersectR2D<Entity_kind::CELL, SourceMeshType, SourceStateType, TargetMeshType,
                   InterfaceReconstructorType, Matpoly_Splitter, Matpoly_Clipper> {

#ifdef PORTAGE_HAS_TANGRAM
  using InterfaceReconstructor2D =
      Tangram::Driver<InterfaceReconstructorType, 2, SourceMeshType,
                      Matpoly_Splitter, Matpoly_Clipper>;
#endif

 public:

#ifdef PORTAGE_HAS_TANGRAM
  /// Constructor with interface reconstructor

  IntersectR2D(SourceMeshType const & source_mesh,
               SourceStateType const & source_state,
               TargetMeshType const & target_mesh,
               NumericTolerances_t num_tols,
               std::shared_ptr<InterfaceReconstructor2D> ir)
      : sourceMeshWrapper(source_mesh), sourceStateWrapper(source_state),
        targetMeshWrapper(target_mesh), interface_reconstructor(ir),
        num_tols_(num_tols) { verify_coord_sys_(); }
#endif

  /// Constructor WITHOUT interface reconstructor

  IntersectR2D(SourceMeshType const & source_mesh,
               SourceStateType const & source_state,
               TargetMeshType const & target_mesh,
               NumericTolerances_t num_tols)
      : sourceMeshWrapper(source_mesh), sourceStateWrapper(source_state),
        targetMeshWrapper(target_mesh), num_tols_(num_tols) { verify_coord_sys_(); }

  /// \brief Set the source mesh material that we have to intersect against

  void set_material(int m) {
    matid_ = m;
  }

  /// \brief Intersect target cell with a set of source cells
  /// \param[in] tgt_entity  Cell of target mesh to intersect
  /// \param[in] src_entities List of source cells to intersect against
  /// \return vector of Weights_t structure containing moments of intersection

  std::vector<Weights_t> operator() (int tgt_cell, std::vector<int> const& src_cells) const {
    std::vector<Wonton::Point<2>> target_poly;
    targetMeshWrapper.cell_get_coordinates(tgt_cell, &target_poly);

    bool trg_convex = poly2_is_convex(target_poly, num_tols_);

#ifdef PORTAGE_DEBUG
    if (targetMeshWrapper.num_entities(SIDE, ALL) == 0) {
      std::stringstream sstr;
      sstr << "In intersect_r2d:" <<
          " Decomposition of cells into sides not available." <<
          " Cannot check validity of input.\n" <<
          " Request sides in mesh wrapper creation or make sure your " <<
          " mesh framework supports cell_get_sides and side_volume methods";
      throw std::runtime_error(sstr.str());
    }
    
    std::vector<int> sides;
    targetMeshWrapper.cell_get_sides(tgt_cell, &sides);

    for (auto const& sd : sides) {
      double svol = targetMeshWrapper.side_volume(sd);
      if (svol < 0.0) {
        double cvol = targetMeshWrapper.cell_volume(tgt_cell);
        throw_validity_error_2d(Wonton::CELL, tgt_cell, false, svol, cvol);
      }
    }
#endif
    
    int nsrc = src_cells.size();
    std::vector<Weights_t> sources_and_weights(nsrc);
    int ninserted = 0;

#ifdef PORTAGE_HAS_TANGRAM
    std::vector<std::shared_ptr<Tangram::CellMatPoly<2>>> cmp_ptrs;
    if (interface_reconstructor != nullptr)
      cmp_ptrs = interface_reconstructor->cell_matpoly_ptrs();
#endif

    for (int i = 0; i < nsrc; i++) {
      int s = src_cells[i];

      Weights_t & this_wt = sources_and_weights[ninserted];
      this_wt.entityID = s;

#ifdef PORTAGE_HAS_TANGRAM
      int nmats = sourceStateWrapper.cell_get_num_mats(s);
      std::vector<int> cellmats;
      sourceStateWrapper.cell_get_mats(s, &cellmats);

      bool non_materialistic_cells = !nmats || (matid_ == -1);
        // nmats == 0 -- no materials
        // matid_ == -1 -- intersect with mesh not a particular material

      bool pure_cell = non_materialistic_cells || (nmats == 1);

      if (!pure_cell) {
        assert(interface_reconstructor != nullptr);
        assert(cmp_ptrs[s] != nullptr);
        nmats = cmp_ptrs[s]->num_materials();
        cellmats = cmp_ptrs[s]->cell_matids();
        pure_cell = (nmats == 1);
      }

      bool source_cell_has_mat = pure_cell ? non_materialistic_cells || (cellmats[0] == matid_) :
                                             cmp_ptrs[s]->is_cell_material(matid_);
      if (source_cell_has_mat) {
        if (pure_cell) {
          // ---------- Intersection with pure cell ---------------
          std::vector<Wonton::Point<2>> source_poly;
          sourceMeshWrapper.cell_get_coordinates(s, &source_poly);

#ifdef PORTAGE_DEBUG
        if (sourceMeshWrapper.num_entities(SIDE, ALL) == 0) {
          std::stringstream sstr;
          sstr << "In intersect_r2d:" <<
              " Side decomposition of cells not requested." <<
              " Cannot check validity of input.\n" <<
              " Request sides in mesh wrapper creation or make sure your " <<
              " mesh framework supports cell_get_sides and side_volume methods";
          throw std::runtime_error(sstr.str());
        }
    
        std::vector<int> sides;
        sourceMeshWrapper.cell_get_sides(s, &sides);
    
        for (auto const& sd : sides) {
          double svol = sourceMeshWrapper.side_volume(sd);
          if (svol < 0.0) {
            double cvol = sourceMeshWrapper.cell_volume(s);
            throw_validity_error_2d(Wonton::CELL, s, true, svol, cvol);
          }
#endif

          // If target polygon is convex we don't care if the source
          // polygon is non-convex because R2D can deal with it.
          auto sys = sourceMeshWrapper.mesh_get_coordinate_system();
          if (trg_convex) {
            this_wt.weights = intersect_polys_r2d(source_poly, target_poly,
                                                  num_tols_, true, sys);
          } else {
            bool src_convex = poly2_is_convex(source_poly, num_tols_);

            // flip the order of the polygons and indicate whether the second
            // polygon (source masquerading as the target) is non-convex or not
            // If it is non-convex it will be triangulated and the triangles
            // intersected with the first polygon
            
            this_wt.weights = intersect_polys_r2d(target_poly, source_poly,
                                                  num_tols_, src_convex, sys);
          }
          
        } else {
          // mixed cell containing this material - intersect with
          // polygon approximation of this material in the cell
          // (obtained from interface reconstruction)

          std::vector<Tangram::MatPoly<2>> matpolys =
              cmp_ptrs[s]->get_matpolys(matid_);

          this_wt.weights.resize(3, 0.0);
          for (auto& matpoly : matpolys) {

#ifdef PORTAGE_DEBUG
          // Lets check the volume of the source material polygon
          std::vector<double> smom = matpoly.moments();
          if (smom[0] < 0.0) {
            std::stringstream sstr;
            sstr << "In intersect_polys_r3d.h: " <<
                "Material polygon for material " << matid_ << " in cell " <<
                s << " has negative volume " << smom[0] << "\n";
            throw std::runtime_error(sstr.str());
          }
#endif
            
            std::vector<Wonton::Point<2>> source_poly = matpoly.points();

            auto sys = sourceMeshWrapper.mesh_get_coordinate_system();
            std::vector<double> momvec = intersect_polys_r2d(source_poly,
                                                             target_poly,
                                                             num_tols_,
                                                             trg_convex,
                                                             sys);

            for (int k = 0; k < 3; k++)
              this_wt.weights[k] += momvec[k];
          }
        }
      }
#else  // No Tangram

#ifdef PORTAGE_DEBUG
      if (sourceMeshWrapper.num_entities(SIDE, ALL) == 0) {
        std::stringstream sstr;
        sstr << "In intersect_r2d:" <<
            " Decomposition of cells into sides not requested." <<
            " Cannot check validity of input.\n" <<
            " Request wedges in mesh wrapper creation or make sure your " <<
            " mesh framework supports cell_get_sides and side_volume methods";
        throw std::runtime_error(sstr.str());
      }
      
      std::vector<int> sides;
      sourceMeshWrapper.cell_get_sides(s, &sides);
      
      for (auto const& sd : sides) {
        double svol = sourceMeshWrapper.side_volume(sd);
        if (svol < 0.0) {
          double cvol = targetMeshWrapper.cell_volume(tgt_cell);
          throw_validity_error_2d(Wonton::CELL, tgt_cell, false, svol, cvol);
        }
      }
#endif

      std::vector<Wonton::Point<2>> source_poly;
      sourceMeshWrapper.cell_get_coordinates(s, &source_poly);

      auto sys = sourceMeshWrapper.mesh_get_coordinate_system();
      if (trg_convex) {
        this_wt.weights = intersect_polys_r2d(source_poly, target_poly,
                                              num_tols_, true, sys);
      } else {
        bool src_convex = poly2_is_convex(source_poly, num_tols_);

        // flip the order of the polygons and indicate whether the second
        // polygon (source masquerading as the target) is non-convex or not
        // If it is non-convex it will be triangulated and the triangles
        // intersected with the first polygon
        
        this_wt.weights = intersect_polys_r2d(target_poly, source_poly,
                                              num_tols_, src_convex, sys);
      }        
#endif

      // Increment if vol of intersection > 0; otherwise, allow overwrite
      if (!this_wt.weights.empty() && this_wt.weights[0] > 0.0)
        ninserted++;
    }

    sources_and_weights.resize(ninserted);
    return sources_and_weights;
  }

  IntersectR2D() = delete;

  /// Assignment operator (disabled)
  IntersectR2D & operator = (const IntersectR2D &) = delete;

 private:
  void verify_coord_sys_() { 
    if (sourceMeshWrapper.mesh_get_coordinate_system() != 
        targetMeshWrapper.mesh_get_coordinate_system()) {
      throw std::runtime_error("Source and target meshes use different coordinate systems.\n");
    }
  }

 private:
  SourceMeshType const & sourceMeshWrapper;
  SourceStateType const & sourceStateWrapper;
  TargetMeshType const & targetMeshWrapper;
  int matid_ = -1;
#ifdef PORTAGE_HAS_TANGRAM
  std::shared_ptr<InterfaceReconstructor2D> interface_reconstructor;
#endif
  NumericTolerances_t num_tols_;
};  // class IntersectR2D




//////////////////////////////////////////////////////////////////////////////
// Specialization of Intersect2D class for nodes

template <class SourceMeshType, class SourceStateType,
          class TargetMeshType,
          template <class, int, class, class> class InterfaceReconstructorType,
          class Matpoly_Splitter, class Matpoly_Clipper>
class IntersectR2D<Entity_kind::NODE, SourceMeshType, SourceStateType, TargetMeshType,
                   InterfaceReconstructorType, Matpoly_Splitter, Matpoly_Clipper> {

#ifdef PORTAGE_HAS_TANGRAM
  using InterfaceReconstructor2D =
      Tangram::Driver<InterfaceReconstructorType, 2, SourceMeshType,
                      Matpoly_Splitter, Matpoly_Clipper>;
#endif

 public:

#ifdef PORTAGE_HAS_TANGRAM
  /// Constructor with interface reconstructor

  IntersectR2D(SourceMeshType const & source_mesh,
               SourceStateType const & source_state,
               TargetMeshType const & target_mesh,
               NumericTolerances_t num_tols,
               std::shared_ptr<InterfaceReconstructor2D> ir)
      : sourceMeshWrapper(source_mesh), sourceStateWrapper(source_state),
        targetMeshWrapper(target_mesh), interface_reconstructor(ir),
        num_tols_(num_tols) {}
#endif


  /// Constructor WITHOUT interface reconstructor

  IntersectR2D(SourceMeshType const & source_mesh,
               SourceStateType const & source_state,
               TargetMeshType const & target_mesh,
               NumericTolerances_t num_tols)
      : sourceMeshWrapper(source_mesh), sourceStateWrapper(source_state),
        targetMeshWrapper(target_mesh), num_tols_(num_tols) {}

  /// \brief Set the source mesh material that we have to intersect against

  void set_material(int m) {
    matid_ = m;
  }

  /// \brief Intersect control volume of a target node with control volumes of
  /// a set of source nodes
  /// \param[in] tgt_node  Target mesh node whose control volume we consider
  /// \param[in] src_nodes List of source nodes whose control volumes we will intersect against
  /// \return vector of Weights_t structure containing moments of intersection

  std::vector<Weights_t> operator() (int tgt_node, std::vector<int> const& src_nodes) const {
    std::vector<Wonton::Point<2>> target_poly;
    targetMeshWrapper.dual_cell_get_coordinates(tgt_node, &target_poly);
    
    bool trg_convex = poly2_is_convex(target_poly, num_tols_);

#ifdef PORTAGE_DEBUG
    if (targetMeshWrapper.num_entities(WEDGE, ALL) == 0) {
      std::stringstream sstr;
      sstr << "In intersect_r2d:" <<
          " Wedge decomposition of cells not requested." <<
          " Cannot check validity of input.\n" <<
          " Request wedges in mesh wrapper creation or make sure your " <<
          " mesh framework supports node_get_wedges and wedge_volume methods";
      throw std::runtime_error(sstr.str());
    }
    
    std::vector<int> wedges;
    targetMeshWrapper.node_get_wedges(tgt_node, ALL, &wedges);

    for (auto const& w : wedges) {
      double wvol = targetMeshWrapper.wedge_volume(w);
      if (wvol < 0.0) {
        double dvol = targetMeshWrapper.dual_cell_volume(tgt_node);
        throw_validity_error_2d(Wonton::CELL, tgt_node, false, wvol, dvol);
      }
    }
#endif
    
    
    int nsrc = src_nodes.size();
    std::vector<Weights_t> sources_and_weights(nsrc);
    int ninserted = 0;
    for (int i = 0; i < nsrc; i++) {
      int s = src_nodes[i];
      std::vector<Wonton::Point<2>> source_poly;
      sourceMeshWrapper.dual_cell_get_coordinates(s, &source_poly);

#ifdef PORTAGE_DEBUG
      if (sourceMeshWrapper.num_entities(WEDGE, ALL) == 0) {
        std::stringstream sstr;
        sstr << "In intersect_r2d:" <<
            " Wedge decomposition of cells not requested." <<
            " Cannot check validity of input.\n" <<
            " Request wedges in mesh wrapper creation or make sure your" <<
            " mesh framework supports node_get_wedges and wedge_volume methods";
        throw std::runtime_error(sstr.str());
      }
      
      std::vector<int> wedges;
      sourceMeshWrapper.node_get_wedges(s, ALL, &wedges);

      for (auto const& w : wedges) {
        double wvol = sourceMeshWrapper.wedge_volume(w);
        if (wvol < 0.0) {
          double dvol = sourceMeshWrapper.dual_cell_volume(s);
          throw_validity_error_2d(Wonton::NODE, s, true, wvol, dvol);
        }
      }
#endif
    
      Weights_t & this_wt = sources_and_weights[ninserted];
      this_wt.entityID = s;
      if (trg_convex)
        this_wt.weights = intersect_polys_r2d(source_poly, target_poly,
                                              num_tols_);
      else {
        bool src_convex = poly2_is_convex(source_poly, num_tols_);

        // flip the order of the polygons and indicate whether the second
        // polygon (source masquerading as the target) is non-convex or not
        // If it is non-convex it will be triangulated and the triangles
        // intersected with the first polygon
        
        this_wt.weights = intersect_polys_r2d(target_poly, source_poly,
                                              num_tols_, src_convex);
      }        

      // Increment if vol of intersection > 0; otherwise, allow overwrite
      if (!this_wt.weights.empty() && this_wt.weights[0] > 0.0)
        ninserted++;
    }

    sources_and_weights.resize(ninserted);
    return sources_and_weights;
  }

  IntersectR2D() = delete;

  /// Assignment operator (disabled)

  IntersectR2D & operator = (const IntersectR2D &) = delete;

 private:
  SourceMeshType const & sourceMeshWrapper;
  SourceStateType const & sourceStateWrapper;
  TargetMeshType const & targetMeshWrapper;
  int matid_ = -1;
#ifdef PORTAGE_HAS_TANGRAM
  std::shared_ptr<InterfaceReconstructor2D> interface_reconstructor;
#endif
  NumericTolerances_t num_tols_;
};  // class IntersectR2D

} // namespace Portage

#endif // PORTAGE_INTERSECT_INTERSECT_R2D_H_
