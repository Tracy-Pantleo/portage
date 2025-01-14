/*
This file is part of the Ristra portage project.
Please see the license file at the root of this repository, or at:
    https://github.com/laristra/portage/blob/master/LICENSE
*/


#include <iostream>

#include "gtest/gtest.h"

// wonton includes
#include "wonton/support/wonton.h"
#include "wonton/support/Point.h"
#include "wonton/support/Vector.h"
#include "wonton/mesh/simple/simple_mesh.h"
#include "wonton/mesh/simple/simple_mesh_wrapper.h"
#include "wonton/state/simple/simple_state.h"
#include "wonton/state/simple/simple_state_wrapper.h"

// portage includes
#include "portage/interpolate/gradient.h"
#include "portage/support/portage.h"

/// Test gradient computation for cell centered fields

TEST(Gradient, Fields_Cell_Ctr) {
  // Create a 4 x 4 cell mesh
  std::shared_ptr<Wonton::Simple_Mesh> mesh1 =
      std::make_shared<Wonton::Simple_Mesh>(0.0, 0.0, 1.0, 1.0, 4, 4);
  ASSERT_TRUE(mesh1 != nullptr);

  // create the wrapper
  Wonton::Simple_Mesh_Wrapper meshwrapper(*mesh1);

  // Create a state object
  Wonton::Simple_State mystate(mesh1);

  // Create a state Wrapper
  Wonton::Simple_State_Wrapper statewrapper(mystate);

  const int nc1 = meshwrapper.num_owned_cells();

  // Define two state vectors, one with constant value and the other
  // with a linear function that is x+2y

  std::vector<double> data1(nc1, 1.25);

  // add the data vector to the state
  mystate.add("cellvars1", Portage::Entity_kind::CELL, &(data1[0]));

  // create the second vector
  std::vector<double> data2(nc1);

  // set the data (x+2*y)
  for (int c = 0; c < nc1; c++) {
    Wonton::Point<2> ccen;
    meshwrapper.cell_centroid(c, &ccen);
    data2[c] = ccen[0] + 2 * ccen[1];
  }

  // add the second data vector to the state
  mystate.add("cellvars2", Portage::Entity_kind::CELL, &(data2[0]));

  // Create Gradient objects
  using Gradient = Portage::Limited_Gradient<2, Wonton::CELL,
                                             Wonton::Simple_Mesh_Wrapper,
                                             Wonton::Simple_State_Wrapper>;

  Gradient gradcalc1(meshwrapper, statewrapper);
  Gradient gradcalc2(meshwrapper, statewrapper);
  Gradient gradcalc3(meshwrapper, statewrapper);
  Gradient gradcalc4(meshwrapper, statewrapper);

  gradcalc1.set_interpolation_variable("cellvars1", Portage::NOLIMITER, Portage::BND_NOLIMITER);
  gradcalc2.set_interpolation_variable("cellvars2", Portage::NOLIMITER, Portage::BND_NOLIMITER);
  gradcalc3.set_interpolation_variable("cellvars1", Portage::BARTH_JESPERSEN, Portage::BND_NOLIMITER);
  gradcalc4.set_interpolation_variable("cellvars2", Portage::BARTH_JESPERSEN, Portage::BND_NOLIMITER);

  // Compute the gradient for each of these fields

  Wonton::Vector<2> grad;

  // Verify the gradient values
  // For field 1 (constant), it is is 0,0
  // For field 2 (x+2y), it is (1,2)

  for (int c = 0; c < nc1; ++c) {
    // unlimited gradient of constant function

    grad = gradcalc1(c);
    ASSERT_NEAR(0.0, grad[0], 1.0e-10);
    ASSERT_NEAR(0.0, grad[1], 1.0e-10);

    // unlimited gradient of linear function

    grad = gradcalc2(c);
    ASSERT_NEAR(1.0, grad[0], 1.0e-10);
    ASSERT_NEAR(2.0, grad[1], 1.0e-10);

    // limited gradient of constant function

    grad = gradcalc3(c);
    ASSERT_NEAR(0.0, grad[0], 1.0e-10);
    ASSERT_NEAR(0.0, grad[1], 1.0e-10);

    // limited gradient of linear function
    //
    // For now, the limiter does not know anything about boundary
    // conditions and therefore, it is a little unpredictable what the
    // gradient will be limited to on boundary cells (which are not
    // completely surrounded by other cells). So check only interior
    // cells.

    grad = gradcalc4(c);

    bool boundary_cell = false;
    std::vector<int> cfaces, cellfaceDirs;
    meshwrapper.cell_get_faces_and_dirs(c, &cfaces, &cellfaceDirs);
    for (auto f : cfaces) {
      std::vector<int> fcells;
      meshwrapper.face_get_cells(f, Portage::Entity_type::ALL, &fcells);
      if (fcells.size() == 1) {
        boundary_cell = true;
        break;
      }
    }

    if (!boundary_cell) {
      ASSERT_NEAR(1.0, grad[0], 1.0e-10);
      ASSERT_NEAR(2.0, grad[1], 1.0e-10);
    }
  }
}

// Test gradient computation with node centered fields

TEST(Gradient, Fields_Node_Ctr) {
  // Make a 3x3 mesh
  std::shared_ptr<Wonton::Simple_Mesh> mesh1 =
      std::make_shared<Wonton::Simple_Mesh>(0.0, 0.0, 1.0, 1.0, 3, 3);
  ASSERT_TRUE(mesh1 != nullptr);

  // create the wrapper
  Wonton::Simple_Mesh_Wrapper meshwrapper(*mesh1);

  // Create a state object
  Wonton::Simple_State mystate(mesh1);

  // Create a state Wrapper
  Wonton::Simple_State_Wrapper statewrapper(mystate);

  // get the number of nodes
  const int nn1 = meshwrapper.num_owned_nodes();

  std::vector<double> data1(nn1, 1.5);

  // add the data vector to the state
  mystate.add("nodevars1", Portage::Entity_kind::NODE, &(data1[0]));

  std::vector<double> data2(nn1);

  for (int n = 0; n < nn1; ++n) {
    Wonton::Point<2> nodexy;
    meshwrapper.node_get_coordinates(n, &nodexy);
    data2[n] = 3 * nodexy[0] + nodexy[1];
  }

  // add the data vector to the state
  mystate.add("nodevars2", Portage::Entity_kind::NODE, &(data2[0]));

  // Create Gradient calculater objects
  using Gradient = Portage::Limited_Gradient<2, Wonton::NODE,
                                             Wonton::Simple_Mesh_Wrapper,
                                             Wonton::Simple_State_Wrapper>;

  Gradient gradcalc1(meshwrapper, statewrapper);
  Gradient gradcalc2(meshwrapper, statewrapper);
  Gradient gradcalc3(meshwrapper, statewrapper);
  Gradient gradcalc4(meshwrapper, statewrapper);

  gradcalc1.set_interpolation_variable("nodevars1", Portage::NOLIMITER, Portage::BND_NOLIMITER);
  gradcalc2.set_interpolation_variable("nodevars2", Portage::NOLIMITER, Portage::BND_NOLIMITER);
  gradcalc3.set_interpolation_variable("nodevars1", Portage::BARTH_JESPERSEN, Portage::BND_NOLIMITER);
  gradcalc4.set_interpolation_variable("nodevars2", Portage::BARTH_JESPERSEN, Portage::BND_NOLIMITER);

  // Make sure we retrieved the correct gradient value for each node
  // For field 1, it is a constant
  // For field 2, it is a linear function

  Wonton::Vector<2> grad;

  for (int n = 0; n < nn1; ++n) {
    // unlimited gradient of constant function

    grad = gradcalc1(n);
    ASSERT_NEAR(0.0, grad[0], 1.0e-10);
    ASSERT_NEAR(0.0, grad[1], 1.0e-10);

    // unlimited gradient of linear function

    grad = gradcalc2(n);
    ASSERT_NEAR(3.0, grad[0], 1.0e-10);
    ASSERT_NEAR(1.0, grad[1], 1.0e-10);

    // limited gradient of constant function

    grad = gradcalc3(n);
    ASSERT_NEAR(0.0, grad[0], 1.0e-10);
    ASSERT_NEAR(0.0, grad[1], 1.0e-10);

    // unlimited gradient of linear function
    //
    // For now, the limiter does not know anything about boundary
    // conditions and therefore, it is a little unpredictable what the
    // limiting will be in boundary nodes/dual-cells (which are not
    // completely surrounded by other dual cells). So check only
    // interior nodes.

    grad = gradcalc4(n);

    // use the primary mesh to check if the node (dual cell) is a
    // boundary node and don't check answer if its a boundary node

    bool boundary_node = false;
    std::vector<int> nodecells;
    meshwrapper.node_get_cells(n, Portage::Entity_type::ALL, &nodecells);
    for (auto nc : nodecells) {
      std::vector<int> cfaces, cellfaceDirs;
      meshwrapper.cell_get_faces_and_dirs(nc, &cfaces, &cellfaceDirs);
      for (auto f : cfaces) {
        std::vector<int> fcells;
        meshwrapper.face_get_cells(f, Portage::Entity_type::ALL, &fcells);
        if (fcells.size() == 1) {
          boundary_node = true;
          break;
        }
      }
      if (boundary_node) break;
    }

    if (!boundary_node) {
      ASSERT_NEAR(3.0, grad[0], 1.0e-10);
      ASSERT_NEAR(1.0, grad[1], 1.0e-10);
    }
  }
}
