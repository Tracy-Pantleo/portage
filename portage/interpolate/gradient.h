/*
  This file is part of the Ristra portage project.
  Please see the license file at the root of this repository, or at:
  https://github.com/laristra/portage/blob/master/LICENSE
*/

#ifndef PORTAGE_INTERPOLATE_GRADIENT_H_
#define PORTAGE_INTERPOLATE_GRADIENT_H_

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>

#include "wonton/support/wonton.h"
#include "wonton/support/lsfits.h"
#include "wonton/support/Point.h"
#include "wonton/support/Vector.h"

#include "portage-config.h"
#include "portage/support/portage.h"
#include "portage/intersect/dummy_interface_reconstructor.h"
#include "portage/driver/fix_mismatch.h"
#include "portage/driver/parts.h"

#ifdef PORTAGE_HAS_TANGRAM
  #include "tangram/driver/driver.h"
  #include "tangram/driver/CellMatPoly.h"
  #include "tangram/support/MatPoly.h"
#endif

namespace Portage {

  using Wonton::Point;
  using Wonton::Vector;

/*! @class Limited_Gradient gradient.h
    @brief Compute limited gradient of a field or components of a field
    @tparam Mesh A mesh class that one can query for mesh info
    @tparam State A state manager class that one can query for field info
    @tparam on_what An enum type which indicates different entity types
*/

  template<
    int D, Entity_kind on_what,
    typename Mesh, typename State,
    template<class, int, class, class>
      class InterfaceReconstructorType = DummyInterfaceReconstructor,
    class Matpoly_Splitter = void,
    class Matpoly_Clipper = void,
    class CoordSys = Wonton::DefaultCoordSys
  >
  class Limited_Gradient {

    // useful aliases
#ifdef PORTAGE_HAS_TANGRAM
    using InterfaceReconstructor =
      Tangram::Driver<
        InterfaceReconstructorType, D, Mesh,
        Matpoly_Splitter, Matpoly_Clipper
      >;
#endif

  public:
    /*! @brief Constructor
        @param[in] mesh  Mesh class than one can query for mesh info
        @param[in] state A state manager class that one can query for field info
        @param[in] on_what An enum that indicates what type of entity the field is
        on
        @param[in] var_name Name of field for which the gradient is to be computed
        @param[in] limiter_type An enum indicating if the limiter type (none,
        Barth-Jespersen, Superbee etc)
        @param[in] Boundary_Limiter_type An enum indicating the limiter type on the boundary

        @todo must remove assumption that field is scalar
     */
    Limited_Gradient(Mesh const& mesh, State const& state) : mesh_(mesh), state_(state) {}

    // Assignment operator (disabled)
    Limited_Gradient& operator = (const Limited_Gradient&) = delete;

    // Destructor
    ~Limited_Gradient() = default;

    // Functor - not implemented for all types - see specialization for
    // cells, nodes
    Vector<D> operator()(int entity_id) {
      throw std::runtime_error("Limited gradient not implemented for this entity kind");
    }

  private:
    Mesh const& mesh_;
    State const& state_;
    double const* values_ = nullptr;
    std::string variable_name_ = "";
    Limiter_type limiter_type_ = DEFAULT_LIMITER;
    Boundary_Limiter_type boundary_limiter_type_ = DEFAULT_BND_LIMITER;
    Field_type field_type_ = Field_type::UNKNOWN_TYPE_FIELD;
    int material_id_ = 0;
  };

  //////////////////////////////////////////////////////////////////////////////

  /*! @class Limited_Gradient<MeshType,StateType,CELL> gradient.h
    @brief Specialization of limited gradient class for @c cell-centered field
    @tparam Mesh A mesh class that one can query for mesh info
    @tparam State A state manager class that one can query for field info
  */


  template<
    int D,
    typename Mesh,
    typename State,
    template<class, int, class, class>
      class InterfaceReconstructorType,
    class Matpoly_Splitter, class Matpoly_Clipper, class CoordSys
  >
  class Limited_Gradient<
    D, Entity_kind::CELL,
    Mesh, State,
    InterfaceReconstructorType,
    Matpoly_Splitter, Matpoly_Clipper, CoordSys
  > {

    // useful aliases
#ifdef PORTAGE_HAS_TANGRAM
    using InterfaceReconstructor =
      Tangram::Driver<
        InterfaceReconstructorType, D, Mesh,
        Matpoly_Splitter, Matpoly_Clipper
      >;
#endif

  public:
    //Constructor for single material remap
    Limited_Gradient(Mesh const& mesh, State const& state,
                     const Part<Mesh, State>* part = nullptr)
      : mesh_(mesh), state_(state), part_(part)
    {
      int nb_cells = mesh_.num_entities(Wonton::CELL, Wonton::PARALLEL_OWNED);
      int nb_mats = state_.num_materials() + 1;
      stencils_.resize(nb_mats);
      valid_neigh_.resize(nb_mats);
      reference_.resize(nb_mats);
      neighbors_.resize(nb_cells);

#ifdef PORTAGE_HAS_TANGRAM
      for (int m = 0; m < nb_mats; ++m) {
        if (m > 0) {
          std::vector<int> mat_cells;
          state_.mat_get_cells(m - 1, &mat_cells);
          int const count = mat_cells.size();
          stencils_[m].resize(count);
          valid_neigh_[m].resize(count);
          reference_[m].resize(count);
        } else {
          stencils_[m].resize(nb_cells);
          valid_neigh_[m].resize(nb_cells);
          reference_[m].resize(nb_cells);
        }
      }
#else
      assert(nb_mats == 1);
      stencils_[0].resize(nb_cells);
      valid_neigh_[0].resize(nb_cells);
      reference_[0].resize(nb_cells);
#endif

      if (part == nullptr) {
        // Collect and keep the list of neighbors for each OWNED CELL as
        // it is common to gradient computation of any field variable.
        // We don't collect neighbors of GHOST CELLs because the outer
        // ghost layer (even if it is a single layer) will not have
        // neighbors on the outer side and therefore, will not yield the
        // right gradient anyway
        Wonton::for_each(mesh_.begin(Wonton::CELL, Wonton::PARALLEL_OWNED),
                         mesh_.end(Wonton::CELL, Wonton::PARALLEL_OWNED),
                         [this](int c) {
                           auto* data = neighbors_.data() + c;
                           mesh_.cell_get_node_adj_cells(c, Wonton::ALL, data);
                           neighbors_[c].emplace(neighbors_[c].begin(), c);
                         });
      } else {
        Wonton::for_each(part->cells().begin(),
                         part->cells().end(),
                         [this](int c) {
                           neighbors_[c] = part_->get_neighbors(c);
                           neighbors_[c].emplace(neighbors_[c].begin(), c);
                         });
      }
      // cache stencil matrices for mesh field.
      // unfortunately it cannot be done yet for
      // multimaterial fields since it requires the
      // interface reconstructor and its options to
      // be already set (which is only done during
      // the material intersection step in the driver).
      cache_matrices(Field_type::MESH_FIELD);
    }

    /**
     * @brief Retrieve the relative index of a given cell.
     *
     * For a mesh field, it is equivalent to its absolute index.
     * For a material field, it is the index in the material cell
     * list.
     *
     * @param c: cell absolute index.
     * @param m: mesh/material index.
     * @return its relative index.
     */
    int get_relative_index(int c, int m) const {
#ifdef PORTAGE_HAS_TANGRAM
      if (m < 0)
        throw std::runtime_error("invalid mesh/material index");
      else
        return (m == 0 ? c : state_.cell_index_in_material(c, m - 1));
#else
      return c;
#endif
    }

    /**
     * @brief Cache matrices involved in least square approximation.
     *
     * @param field_type: the field type.
     */
    void cache_matrices(Field_type field_type) {

      auto kernel = [this](int c, int m = 0) {
        auto const& p = retrieve_stencil_points(c, m);
        int const cell = get_relative_index(c, m);
        stencils_[m][cell] = Wonton::build_gradient_stencil_matrices<D>(p, true);
      };

      if (part_ == nullptr) {
        if (field_type == Field_type::MESH_FIELD) {
          Wonton::for_each(mesh_.begin(Wonton::CELL, Wonton::PARALLEL_OWNED),
                           mesh_.end(Wonton::CELL, Wonton::PARALLEL_OWNED),
                           [&](int c) { kernel(c); });

        } else {
#ifdef PORTAGE_HAS_TANGRAM
          if (interface_reconstructor_) {
            int const nb_mats = state_.num_materials();
            for (int m = 0; m < nb_mats; ++m) {
              // filter material cells
              std::vector<int> buffer, cells;
              state_.mat_get_cells(m, &buffer);
              for (int const& c : buffer) {
                if (mesh_.cell_get_type(c) == Wonton::PARALLEL_OWNED) {
                  cells.emplace_back(c);
                }
              }
              // retrieve matrices
              std::for_each(cells.begin(), cells.end(), [&](int c) { kernel(c, m + 1); });
            }
          } else
            throw std::runtime_error("interface reconstructor not set");
#endif
        }
      } else {
        assert(field_type == Field_type::MESH_FIELD);
        Wonton::for_each(part_->cells().begin(),
                         part_->cells().end(),
                         [&](int c) { kernel(c); });
      }
    }

#ifdef PORTAGE_HAS_TANGRAM
    //This method should be called by the user if the field type is MULTIMATERIAL_FIELD.
    //As the constructor with interface reconstructor only sets the variable name
    //for such fields, this method is needed to properly set the multimaterial data local
    //to the routine.
    void set_material(int matid) {
      material_id_ = matid;
      // Extract the field data from the state manager
      if (field_type_ != Field_type::MESH_FIELD) {
        state_.mat_get_celldata(variable_name_, material_id_, &values_);
      }
    }

    void set_interface_reconstructor(std::shared_ptr<InterfaceReconstructor> ir) {
      interface_reconstructor_ = ir;
    }
#endif

    /**
     * @brief Set interpolation variable and options.
     *
     * If the field type is a MESH_FIELD, then the corresponding data will
     * be stored. If the field_type is a MULTIMATERIAL_FIELD, then it
     * only stores the variable name. The user code must make a call to
     * set_material to store the material-wise data.
     *
     * @param variable_name: the name of the variable to remap
     * @param limiter_type: the limiter to use for internal points.
     * @param boundary_limiter_type: the limiter to use at boundary points.
     */
    void set_interpolation_variable(std::string variable_name,
                                    Limiter_type limiter_type = NOLIMITER,
                                    Boundary_Limiter_type boundary_limiter_type = BND_NOLIMITER) {

      variable_name_ = variable_name;
      limiter_type_ = limiter_type;
      boundary_limiter_type_ = boundary_limiter_type;
      material_id_ = -1;

      // Extract the field data from the state manager
      field_type_ = state_.field_type(Entity_kind::CELL, variable_name_);
      if (field_type_ == Field_type::MESH_FIELD) {
        state_.mesh_get_data(Entity_kind::CELL, variable_name_, &values_);
      }
    }

    /**
     * @brief Compute stencil points.
     *
     * This is meant to be called only once for all fields,
     * and is required for the construction of the least square
     * matrices used to approximate the gradient.
     *
     * @param cell: the cell ID.
     * @return stencil points coordinates.
     */
    std::vector<Point<D>> retrieve_stencil_points(int c, int m = 0) {

      int const size = neighbors_[c].size();

      std::vector<Point<D>> list_coords;
      list_coords.reserve(size);
      int const cell = get_relative_index(c, m);
      valid_neigh_[m][cell].clear();
      valid_neigh_[m][cell].reserve(size);

#ifdef PORTAGE_HAS_TANGRAM
    std::vector<std::shared_ptr<Tangram::CellMatPoly<D>>> cmp_ptrs;
    if (interface_reconstructor_ != nullptr)
      cmp_ptrs = interface_reconstructor_->cell_matpoly_ptrs();
#endif

      // Loop over cell where grad is needed and its neighboring cells
      for (auto&& neigh_global : neighbors_[c]) {
#ifdef PORTAGE_HAS_TANGRAM
        // Field values for each cells in each material are stored according to
        // the material's cell list. So, get the local index of each neighbor cell
        // in the material cell list to access the correct field value
        int const neigh_local = get_relative_index(neigh_global, m);

        // In case of material-data, we need to check that neighbors contain
        // material of interest (i.e. have valid local id);
        // in the case of mesh data, this is always true, since local cellid
        // (neigh_local) is equal to global cellid (neigh_global).
        // nota bene: cell_index_in_material can return -1.
        if (neigh_local >= 0) {
          std::vector<int> cell_mats;
          state_.cell_get_mats(neigh_global, &cell_mats);
          int nb_mats = cell_mats.size();

          bool non_materialistic_cells = !nb_mats || (m - 1 == -1);
            // nb_mats == 0 -- no materials
            // m - 1 == -1 -- intersect with mesh not a particular material

          bool pure_cell = non_materialistic_cells || (nb_mats == 1);

          if (!pure_cell) {
            assert(interface_reconstructor_ != nullptr);
            assert(cmp_ptrs[neigh_global] != nullptr);
            nb_mats = cmp_ptrs[neigh_global]->num_materials();
            cell_mats = cmp_ptrs[neigh_global]->cell_matids();
            pure_cell = (nb_mats == 1);
          }

          bool stencil_cell_has_mat = pure_cell ? non_materialistic_cells || (cell_mats[0] == m - 1) :
                                                  cmp_ptrs[neigh_global]->is_cell_material(m - 1); 

          if (stencil_cell_has_mat) {
            if (!pure_cell) /* multi-material cell */ {
              // Collect all the matpolys in this cell for the material of interest
              auto matpolys = cmp_ptrs[neigh_global]->get_matpolys(m - 1);

              // If there are multiple matpolys in this cell for the material of interest,
              // aggregate moments to compute new centroid
              double mvol = 0.0;
              Point<D> centroid;
              for (auto&& poly : matpolys) {
                auto moments = poly.moments();
                mvol += moments[0];
                for (int k = 0; k < D; k++)
                  centroid[k] += moments[k + 1];
              }

              // There are cases where r3d returns a single zero volume poly which
              // ultimately produces a nan, so here we are explicitly filtering this
              // case.
              if (mvol==0.) continue;

              for (int k = 0; k < D; k++)
                centroid[k] /= mvol;

              list_coords.emplace_back(centroid);

              // Populate least squares vectors with centroid for material
              // of interest and field value in the current cell for that material
              // mark as valid
              valid_neigh_[m][cell].emplace_back(neigh_local);
              // save cell centroid as a reference point of the stencil
              if (neigh_global == c) { reference_[m][cell] = centroid; }
            } else { /* single-material cell */
              // Get the cell-centered value for this material
              Point<D> centroid;
              mesh_.cell_centroid(neigh_global, &centroid);
              list_coords.emplace_back(centroid);
              valid_neigh_[m][cell].emplace_back(neigh_local);
              // save cell centroid as a reference point of the stencil
              if (neigh_global == c) { reference_[m][cell] = centroid; }
            }
          }
        }
#else
        // If we get here, we must have mesh data which is cell-centered
        // and not dependent on material, so just get the centroid and value
        if (m == 0) {
          Point<D> centroid;
          mesh_.cell_centroid(neigh_global, &centroid);
          list_coords.emplace_back(centroid);
          valid_neigh_[m][cell].emplace_back(neigh_global);
          // save cell centroid as a reference point of the stencil
          if (neigh_global == c) { reference_[m][cell] = centroid; }
        }
#endif
      }
      return list_coords;
    }

    /**
     * @brief Retrieve values of each stencil point.
     *
     * This is meant to be called for each field variable.
     *
     * @param cell: the cell ID.
     * @return values of each stencil point.
     */
    std::vector<double> retrieve_stencil_values(int c, int m) const {
      std::vector<double> list_values;
      for (auto&& neigh : valid_neigh_[m][c]) {
        list_values.emplace_back(values_[neigh]);
      }
      return list_values;
    }

    /**
     * @brief Compute the limited gradient for the given cell.
     *
     * @param cellid: the cell ID.
     * @return the field gradient at this cell.
     */
    Vector<D> operator()(int cellid) {

      assert(values_);
      assert(mesh_.cell_get_type(cellid) == Entity_type::PARALLEL_OWNED);

      double phi = 1.0;
      Vector<D> grad;

      // check that cell is within the part if part-by-part requested
      if (part_ != nullptr && !part_->contains(cellid)) {
        grad.zero();
        return grad;
      }

      // useful predicates
      bool is_boundary_cell = mesh_.on_exterior_boundary(Entity_kind::CELL, cellid);
      bool apply_limiter = limiter_type_ == BARTH_JESPERSEN &&
                           (!is_boundary_cell || boundary_limiter_type_ == BND_BARTH_JESPERSEN);

      // Limit the boundary gradient to enforce monotonicity preservation
      if (is_boundary_cell && boundary_limiter_type_ == BND_ZERO_GRADIENT) {
        grad.zero();
        return grad;
      }

      int const m = material_id_ + 1;
      int const c = get_relative_index(cellid, m);

#if defined(PORTAGE_DEBUG)
      auto print = [](Wonton::Matrix const& M, std::string const& desc) {
        std::cout << desc << ": [";
        for (int i = 0; i < M.rows(); ++i) {
          for (int j = 0; j < M.columns(); ++j) {
            std::cout << M[i][j] <<", ";
          }
        }
        std::cout << "]" << std::endl;
      };

      print(stencils_[m][c][0], "(A^T.A)^-1");
      print(stencils_[m][c][1], "A^T");
      std::cout << " ------------ " << std::endl;
#endif

      // retrieve values of each stencil point
      auto list_values = retrieve_stencil_values(c, m);

      // compute the gradient using the stored stencil matrices
      grad = Wonton::ls_gradient<D, CoordSys>(stencils_[m][c][0],
                                              stencils_[m][c][1],
                                              list_values);

      // Limit the gradient to enforce monotonicity preservation
      if (apply_limiter) {

        phi = 1.0;

        // Min and max vals of function (cell centered vals) among neighbors
        // and the cell itself
        /// @todo: must remove assumption the field is scalar
        double minval = list_values[0];
        double maxval = list_values[0];
        double cellcenval = list_values[0];

        // Find min and max values among all neighbors (exlude the first element
        // in nbrids because it corresponds to the cell itself, not a neighbor)
        int const nb_values = list_values.size();
        for (int i = 1; i < nb_values; ++i) {
          minval = std::min(list_values[i], minval);
          maxval = std::max(list_values[i], maxval);
        }

        std::vector<Point<D>> coords;
#ifdef PORTAGE_HAS_TANGRAM
        /* Per page 278 of [Kucharik, M. and Shaskov, M, "Conservative
           Multi-material Remap for Staggered Multi-material Arbitrary
           Lagrangian-Eulerian Methods," Journal of Computational Physics,
           v 258, pp. 268-304, 2014], if a cell is a multimaterial cell and the
           field is a material field, then the min value (for density, at
           least) should be set to 0 and the max value to infinity (or a large
           number), so that we don't end up limiting the gradient to 0 and drop
           down to 1st order. But we don't know if a variable is density (don't
           want to do silly things like string comparison) or pressure or
           something else? What if we limit pressure to 0 and it should
           actually be allowed to go -ve? In the end, along with the limiter
           type, the application should be able to tell Portage the global
           bounds that a variable has to satisfy. Then we can impose the global
           limits at multi-material cells and boundary cells without limiting
           the gradient to 0. */

        std::vector<std::shared_ptr<Tangram::CellMatPoly<D>>> cmp_ptrs;
        if (interface_reconstructor_ != nullptr)
          cmp_ptrs = interface_reconstructor_->cell_matpoly_ptrs();

        std::vector<int> cell_mats;
        state_.cell_get_mats(cellid, &cell_mats);
        int nb_mats = cell_mats.size();

        bool non_materialistic_cells = !nb_mats || (m - 1 == -1);
          // nb_mats == 0 -- no materials
          // m - 1 == -1 -- intersect with mesh not a particular material

        bool pure_cell = non_materialistic_cells || (nb_mats == 1);

        if (!pure_cell) {
          assert(interface_reconstructor_ != nullptr);
          assert(cmp_ptrs[cellid] != nullptr);
          nb_mats = cmp_ptrs[cellid]->num_materials();
          cell_mats = cmp_ptrs[cellid]->cell_matids();
          pure_cell = (nb_mats == 1);
        }

        bool stencil_cell_has_mat = pure_cell ? non_materialistic_cells || (cell_mats[0] == m - 1) :
                                                cmp_ptrs[cellid]->is_cell_material(m - 1);

        if (stencil_cell_has_mat) {
          if (!pure_cell) /* multi-material cell */ {
            // Collect all the matpolys in this cell for the material of interest
            auto matpolys = cmp_ptrs[cellid]->get_matpolys(m - 1);

            // Get vertices of all material polygons for given material
            for (auto&& poly : matpolys) {
              auto points = poly.points();
              coords.insert(coords.end(), points.begin(), points.end());
            }
          } else /* single-material cell */ {
            mesh_.cell_get_coordinates(cellid, &coords);
          }
        }
#else
        mesh_.cell_get_coordinates(cellid, &coords);
#endif
        // Find the min and max of the reconstructed function in the cell
        // Since the reconstruction is linear, this will occur at one of
        // the nodes of the cell. So find the values of the reconstructed
        // function at the nodes of the cell. For multi-material problems,
        // the reconstruction is defined over material polytopes instead of
        // cells, so the reconstruction is evaluated in vertices of these
        // polytopes.

        for (auto&& coord : coords) {
          auto vec = coord - reference_[m][c];
          double diff = dot(grad, vec);
          double extremeval = (diff > 0.) ? maxval : minval;
          double phi_new = (diff == 0. ? 1. : (extremeval - cellcenval) / diff);
          phi = std::min(phi_new, phi);
        }
      }

      // Limited gradient is phi*grad
      return phi * grad;
    }

  private:
    Mesh const& mesh_;
    State const& state_;
    double const* values_ = nullptr;
    std::string variable_name_ = "";
    Limiter_type limiter_type_ = DEFAULT_LIMITER;
    Boundary_Limiter_type boundary_limiter_type_ = DEFAULT_BND_LIMITER;
    Field_type field_type_ = Field_type::UNKNOWN_TYPE_FIELD;

    int material_id_ = -1;
    std::vector<int> cell_ids_;
    std::vector<std::vector<int>> neighbors_ {};
    /** cached stencil matrices per cell and per material */
    std::vector<std::vector<std::vector<Wonton::Matrix>>> stencils_ {};
    /** filtered neighbors per cell and per material */
    std::vector<std::vector<std::vector<int>>> valid_neigh_ {};
    /** cached reference point per stencil and per material */
    std::vector<std::vector<Wonton::Point<D>>> reference_ {};

#ifdef PORTAGE_HAS_TANGRAM
    std::shared_ptr<InterfaceReconstructor> interface_reconstructor_;
#endif
    const Part<Mesh, State>* part_ = nullptr;
  };

  ///////////////////////////////////////////////////////////////////////////////

  /*! @class Limited_Gradient<MeshType,StateType,NODE> gradient.h
    @brief Specialization of limited gradient class for @c node-centered field
    @tparam Mesh A mesh class that one can query for mesh info
    @tparam State A state manager class that one can query for field info
  */

  template<
    int D,
    typename Mesh,
    typename State,
    template<class, int, class, class>
      class InterfaceReconstructorType,
    class Matpoly_Splitter, class Matpoly_Clipper, class CoordSys
  >
  class Limited_Gradient<
    D, Entity_kind::NODE,
    Mesh, State,
    InterfaceReconstructorType,
    Matpoly_Splitter, Matpoly_Clipper, CoordSys
  > {

#ifdef PORTAGE_HAS_TANGRAM
    using InterfaceReconstructor =
    Tangram::Driver<
      InterfaceReconstructorType, D, Mesh,
      Matpoly_Splitter, Matpoly_Clipper
    >;
#endif

  public:
    /*! @brief Constructor
      @param[in] mesh  Mesh class than one can query for mesh info
      @param[in] state A state manager class that one can query for field info
      @param[in] var_name Name of field for which the gradient is to be computed
      @param[in] limiter_type An enum indicating if the limiter type (none, Barth-Jespersen, Superbee etc)
      @param[in] boundary_limiter_type An enum indicating the limiter type on the boundary

      @todo must remove assumption that field is scalar
    */
    Limited_Gradient(Mesh const& mesh, State const& state,
                     const Part<Mesh, State>* part = nullptr)
      : mesh_(mesh), state_(state)
    {
      if (part == nullptr) {
        // Collect and keep the list of neighbors for each OWNED or
        // GHOST as it is common to gradient computation of any field
        // variable.
        // NOTE:*******************************************************
        // The iteration for node (or dual cell) gradients is
        // purposefully different from that of cells. For cells, we
        // collect neighbors of only OWNED source cells because we will
        // compute gradients only over OWNED source cells because our
        // parallel scheme mandates that any target cell overlap only
        // OWNED source cells (even if we have to duplicate those source
        // cells on multiple partitions as we do when we
        // redistribute). With nodes, we have to compute the gradient
        // over ALL (OWNED+GHOST) nodes. This is because, even with the
        // above requirement on overlap of OWNED target and source
        // cells, the dual cells of a target node can overlap the dual
        // cell of GHOST source node on a partition boundary and we want
        // the gradient at that ghost node.
        //
        // Anyway, this is a bit moot, because we cannot guarantee
        // second-order accuracy for remap of node-centered variables in
        // anything other than uniform regular meshes since the linear
        // reconstruction is supposed to be over the centroid while our
        // field variables are at nodes which may not be coincident with
        // the centroid. [PERHAPS THIS WILL GET FIXED IF WE ARE ABLE TO
        // DO A CONSERVATIVE RECONSTRUCTION AROUND ANY POINT NOT JUST
        // THE CENTROID]
        //
        // Also, the only node centered variables in the codes we are
        // dealing with are likely to be velocity and one never remaps
        // velocity directly anyway - we remap momentum in a
        // conservative and consistent way by transferring momentum to
        // cell centers, remapping cell centered momentum and
        // transferring it back to nodes and backing out velocity.
        //
        // Iterating over ALL nodes (or dual control volumes) just keeps
        // us from making a grosser error at partition boundaries

        int const nnodes = mesh_.num_entities(Wonton::NODE, Wonton::ALL);
        neighbors_.resize(nnodes);
        stencils_.resize(nnodes);
        reference_.resize(nnodes);

        Wonton::for_each(mesh_.begin(Wonton::NODE, Wonton::ALL),
                         mesh_.end(Wonton::NODE, Wonton::ALL),
                         [this](int n) {
                           auto* data = neighbors_.data() + n;
                           mesh_.dual_cell_get_node_adj_cells(n, Wonton::ALL, data);
                           neighbors_[n].emplace(neighbors_[n].begin(), n);
                         });
        // cache stencil matrices
        cache_matrices(Field_type::MESH_FIELD);
      } else {
        throw std::runtime_error("part-by-part not supported for nodal remap");
      }
    }

    /**
     * @brief Cache matrices involved in least square approximation.
     *
     * The field type is only given to keep the same interface with the
     * cell-centered field variant.
     *
     * @param field_type: the field type.
     */
    void cache_matrices(Field_type field_type) {
      if (field_type == Field_type::MESH_FIELD) {
        Wonton::for_each(mesh_.begin(Wonton::NODE, Wonton::ALL),
                         mesh_.end(Wonton::NODE, Wonton::ALL),
                         [this](int n) {
                           auto const& points = retrieve_stencil_points(n);
                           stencils_[n] = Wonton::build_gradient_stencil_matrices<D>(points, true);
                         });
      } else
        throw std::runtime_error("incorrect field type");
    }

    void set_material(int material_id) { material_id_ = material_id; }

    void set_interpolation_variable(std::string const& variable_name,
                                    Limiter_type limiter_type = NOLIMITER,
                                    Boundary_Limiter_type boundary_limiter_type = BND_NOLIMITER) {

      variable_name_ = variable_name;
      limiter_type_ = limiter_type;
      boundary_limiter_type_ = boundary_limiter_type;
      state_.mesh_get_data(Entity_kind::NODE, variable_name_, &values_);
    }

#ifdef PORTAGE_HAS_TANGRAM
    // for interface compatibility with cell-centered variant
    void set_interface_reconstructor(std::shared_ptr<InterfaceReconstructor> /* unused */) {}
#endif

    /**
     * @brief Compute stencil points.
     *
     * This is meant to be called only once for all fields,
     * and is required for the construction of the least square
     * matrices used to approximate the gradient.
     *
     * @param node: the node ID.
     * @return stencil points coordinates.
     */
    std::vector<Point<D>> retrieve_stencil_points(int node) {
      auto const& stencil = neighbors_[node];
      int const size = stencil.size();
      std::vector<Point<D>> list_coords(size);
      mesh_.node_get_coordinates(node, &reference_[node]);
      for (int i = 0; i < size; ++i) {
        mesh_.node_get_coordinates(stencil[i], &list_coords[i]);
      }
      return list_coords;
    }

    /**
     * @brief Retrieve values of each stencil point.
     *
     * This is meant to be called for each field variable.
     *
     * @param cell: the cell ID.
     * @return values of each stencil point.
     */
    std::vector<double> retrieve_stencil_values(int node) const {
      auto const& stencil = neighbors_[node];
      std::vector<double> list_values;
      for (auto&& i : stencil) {
        list_values.emplace_back(values_[i]);
      }
      return list_values;
    }

    /**
     * @brief Compute the limited gradient for the given node.
     *
     * @param nodeid: the node ID.
     * @return the field gradient at this node.
     */
    Vector<D> operator()(int nodeid) {

      assert(values_);

      double phi = 1.0;
      Vector<D> grad;

      bool is_boundary_node = mesh_.on_exterior_boundary(Entity_kind::NODE, nodeid);
      bool apply_limiter = limiter_type_ == BARTH_JESPERSEN &&
                           (!is_boundary_node
                            || boundary_limiter_type_ == BND_BARTH_JESPERSEN);

      if (is_boundary_node && boundary_limiter_type_ == BND_ZERO_GRADIENT) {
        grad.zero();
        return grad;
      }

      // retrieve values of each stencil point
      auto node_values = retrieve_stencil_values(nodeid);

      // compute the gradient using the stored stencil matrices
      grad = Wonton::ls_gradient<D, CoordSys>(stencils_[nodeid][0],
                                              stencils_[nodeid][1],
                                              node_values);

      if (apply_limiter) {
        // Min and max vals of function (cell centered vals) among neighbors
        double minval = values_[nodeid];
        double maxval = values_[nodeid];

        for (auto const& val : node_values) {
          minval = std::min(val, minval);
          maxval = std::max(val, maxval);
        }

        // Find the min and max of the reconstructed function in the cell
        // Since the reconstruction is linear, this will occur at one of
        // the nodes of the cell. So find the values of the reconstructed
        // function at the nodes of the cell
        double nodeval = values_[nodeid];

        std::vector<Point<D>> dual_cell_coords;
        mesh_.dual_cell_get_coordinates(nodeid, &dual_cell_coords);

        for (auto&& coord : dual_cell_coords) {
          auto vec = coord - reference_[nodeid];
          double diff = dot(grad, vec);
          double extremeval = (diff > 0.0) ? maxval : minval;
          double phi_new = (diff == 0.0) ? 1 : (extremeval - nodeval) / diff;
          phi = std::min(phi_new, phi);
        }
      }

      // Limited gradient is phi*grad
      return phi * grad;
    }

  private:
    Mesh const& mesh_;
    State const& state_;
    double const* values_ = nullptr;
    std::string variable_name_ = "";
    Limiter_type limiter_type_ = DEFAULT_LIMITER;
    Boundary_Limiter_type boundary_limiter_type_ = DEFAULT_BND_LIMITER;
    Field_type field_type_ = Field_type::UNKNOWN_TYPE_FIELD;

    int material_id_ = -1;
    std::vector<std::vector<int>> neighbors_;
    /** cached stencil matrices per cell and per material */
    std::vector<std::vector<Wonton::Matrix>> stencils_ {};
    /** cached reference point per stencil and per material */
    std::vector<Wonton::Point<D>> reference_ {};

  };
}  // namespace Portage

#endif  // PORTAGE_INTERPOLATE_GRADIENT_H_
