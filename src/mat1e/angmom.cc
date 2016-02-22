//
// BAGEL - Brilliantly Advanced General Electronic Structure Library
// Filename: angmom_london.cc
// Copyright (C) 2015 Toru Shiozaki
//
// Author: Ryan D. Reynolds <RyanDReynolds@u.northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//


#include <src/mat1e/angmom.h>
#include <src/integral/os/angmombatch.h>

using namespace std;
using namespace bagel;

AngMom::AngMom(shared_ptr<const Geometry> g, array<double,3> mc) : geom_(g), mcoord_(mc) {
  assert(!geom_->magnetism());
}


array<shared_ptr<Matrix>, 3> AngMom::compute() const {

  const int nbasis = geom_->nbasis();
  auto mat0 = make_shared<Matrix>(nbasis, nbasis);
  auto mat1 = make_shared<Matrix>(nbasis, nbasis);
  auto mat2 = make_shared<Matrix>(nbasis, nbasis);

  // TODO perhaps we could reduce operation by a factor of 2
  auto o0 = geom_->offsets().begin();
  for (auto a0 = geom_->atoms().begin(); a0 != geom_->atoms().end(); ++a0, ++o0) {
    auto o1 = geom_->offsets().begin();
    for (auto a1 = geom_->atoms().begin(); a1 != geom_->atoms().end(); ++a1, ++o1) {

      auto offset0 = o0->begin();
      for (auto b0 = (*a0)->shells().begin(); b0 != (*a0)->shells().end(); ++b0, ++offset0) {
        auto offset1 = o1->begin();
        for (auto b1 = (*a1)->shells().begin(); b1 != (*a1)->shells().end(); ++b1, ++offset1) {

          array<shared_ptr<const Shell>,2> input = {{*b1, *b0}};
          AngMomBatch mom(input, mcoord_);
          mom.compute();

          const double* dat0 = mom.data();
          const double* dat1 = mom.data() + mom.size_block();
          const double* dat2 = mom.data() + mom.size_block()*2;
          for (int i = *offset0; i != *offset0 + (*b0)->nbasis(); ++i) {
            for (int j = *offset1; j != *offset1 + (*b1)->nbasis(); ++j, ++dat0, ++dat1, ++dat2) {
              mat0->element(j,i) = *dat0;
              mat1->element(j,i) = *dat1;
              mat2->element(j,i) = *dat2;
            }
          }

        }
      }
    }
  }

  return array<shared_ptr<Matrix>,3>{{mat0, mat1, mat2}};
}
