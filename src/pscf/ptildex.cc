//
// BAGEL - Parallel electron correlation program.
// Filename: ptildex.cc
// Copyright (C) 2009 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// The BAGEL package is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public License
// along with the BAGEL package; see COPYING.  If not, write to
// the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
//


#include <iostream>
#include <iomanip>
#include <cassert>
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <src/pscf/ptildex.h>
#include <src/util/f77.h>
#include <src/pscf/pscf_macros.h>

using namespace std;
using namespace bagel;

// olp is real-space quantity.
PTildeX::PTildeX(const shared_ptr<POverlap> olp) : PMatrix1e(olp->geom())  {

  PMatrix1e overlap_m = olp->ft();
  overlap_m.hermite();

  // Use canonical orthogonalization (Szabo pp.144)
  shared_ptr<PData> odata = overlap_m.data();
  const complex<double> cone(1.0, 0.0);
  const complex<double> czero(0.0, 0.0);

  vector<double> max_eig;
  vector<double> min_eig;

  cout << "  Orthogonalizing overlap matrix" << endl;

  vector<double> removed;

  int mcount = 0;
  for (int m = -K(); m != max(K(), 1); ++m, ++mcount) {
    const int boffset = mcount * blocksize_;

    const int lwork = 5 * nbasis_;
    complex<double>* work = new complex<double>[lwork];
    const complex<double>* S = odata->pointer(boffset);
    complex<double>* cdata = data_->pointer(boffset);
    const int unit = 1;
    zcopy_(&blocksize_, S, &unit, cdata, &unit);

    int info = 0;
    double* rwork = new double[5 * nbasis_];
    double* eig = new double[nbasis_];
    zheev_("V", "U", &ndim_, cdata, &ndim_, eig, work, &lwork, rwork, &info);
    if (info != 0) throw runtime_error("zheev in ptildex.cc failed.");
    delete[] rwork;
    delete[] work;

    // counting how many orbital must be deleted owing to the linear dependency
    const double largest = fabs(eig[ndim_ - 1]);
    int cnt = 0;
    for (int i = 0; i != ndim_; ++i) {
      if (fabs(eig[i]) < largest * THRESH_OVERLAP) ++cnt;
      else break;
    }
    if (cnt != 0) {
      cout << "   Caution: ignored " << cnt << " orbital" << (cnt == 1 ? "" : "s")
           << " in orthogonalization (m = " << m << ")" << setprecision(15) << eig[0] << endl;
    }

    for (int i = cnt; i != ndim_; ++i) {
      assert(eig[i] > 0);
      const double scale = 1.0 / sqrt(eig[i]);
      const int offset = i * ndim_;
      for (int j = 0; j != ndim_; ++j) {
        cdata[j + offset] *= scale;
      }
    }
    min_eig.push_back(eig[cnt]);
    max_eig.push_back(eig[ndim_-1]);
    delete[] eig;

    // TODO FATAL: CHECK THAT ALL BLOCKS HAVE SAME NUMBER OF CABS
    // this line needs attention....
    removed.push_back(cnt);
    mdim_ = ndim_ - cnt;

    if (cnt != 0) {
      for (int i = 0; i != mdim_; ++i) {
        zcopy_(&ndim_, &cdata[(i + cnt) * ndim_], &unit, &cdata[i * ndim_], &unit);
      }
    }

  }
  cout << setprecision(15) << endl;
  cout << "  Maximum residual in orthogonalization: " << *max_element(max_eig.begin(), max_eig.end()) << endl;
  cout << "  Minimum residual in orthogonalization: " << *min_element(min_eig.begin(), min_eig.end()) << endl;
  cout << endl;

  assert(*max_element(removed.begin(), removed.end()) == *min_element(removed.begin(), removed.end()));

}


PTildeX::~PTildeX() {
}


