//
// BAGEL - Parallel electron correlation program.
// Filename: mmbatch.cc
// Copyright (C) 2012 Toru Shiozaki
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

#include <src/integral/carsphlist.h>
#include <src/integral/hrrlist.h>
#include <src/integral/os/mmbatch.h>

using namespace std;
using namespace bagel;

const static HRRList hrr;
const static CarSphList carsphlist;

void MMBatch::compute() {

  double* const intermediate_p = stack_->get(prim0_*prim1_*asize_*nblocks());
  perform_VRR(intermediate_p);

  double* cdata = data_;
  double* csource = intermediate_p;
  for (int iblock = 0; iblock != nblocks(); ++iblock,
                                    cdata += size_block_, csource += prim0_*prim1_*asize_) {

    double* const intermediate_c = stack_->get(cont0_*cont1_*asize_);
    perform_contraction(asize_, csource, prim0_, prim1_, intermediate_c,
                        basisinfo_[0]->contractions(), basisinfo_[0]->contraction_ranges(), cont0_,
                        basisinfo_[1]->contractions(), basisinfo_[1]->contraction_ranges(), cont1_);

    double* const intermediate_fi = stack_->get(cont0_*cont1_*asize_intermediate_);

    if (basisinfo_[1]->angular_number() != 0) {
      const int hrr_index = basisinfo_[0]->angular_number() * ANG_HRR_END + basisinfo_[1]->angular_number();
      hrr.hrrfunc_call(hrr_index, cont0_*cont1_, intermediate_c, AB_, intermediate_fi);
    } else {
      const unsigned int array_size = cont0_*cont1_*asize_intermediate_;
      copy_n(intermediate_c, array_size, intermediate_fi);
    }

    if (spherical_) {
      double* const intermediate_i = stack_->get(cont0_*cont1_*asize_final_);
      const unsigned int carsph_index = basisinfo_[0]->angular_number() * ANG_HRR_END + basisinfo_[1]->angular_number();
      const int nloops = cont0_ * cont1_;
      carsphlist.carsphfunc_call(carsph_index, nloops, intermediate_fi, intermediate_i);

      const unsigned int sort_index = basisinfo_[1]->angular_number() * ANG_HRR_END + basisinfo_[0]->angular_number();
      sort_.sortfunc_call(sort_index, cdata, intermediate_i, cont1_, cont0_, 1, swap01_);
      stack_->release(cont0_*cont1_*asize_final_, intermediate_i);
    } else {
      const unsigned int sort_index = basisinfo_[1]->angular_number() * ANG_HRR_END + basisinfo_[0]->angular_number();
      sort_.sortfunc_call(sort_index, cdata, intermediate_fi, cont1_, cont0_, 1, swap01_);
    }

    stack_->release(cont0_*cont1_*asize_intermediate_, intermediate_fi);
    stack_->release(cont0_*cont1_*asize_, intermediate_c);
  }

  stack_->release(prim0_*prim1_*asize_*nblocks(), intermediate_p);
}


void MMBatch::perform_VRR(double* intermediate) {

  const int worksize = amax1_;

  double* const pworkx = stack_->get(worksize*(lmax_+1));
  double* const pworky = stack_->get(worksize*(lmax_+1));
  double* const pworkz = stack_->get(worksize*(lmax_+1));
  vector<double*> workx(lmax_+1);
  vector<double*> worky(lmax_+1);
  vector<double*> workz(lmax_+1);
  for (int i = 0; i != lmax_+1; ++i) {
    workx[i] = pworkx + worksize*i;
    worky[i] = pworky + worksize*i;
    workz[i] = pworkz + worksize*i;
  }

  const size_t size_block = prim0_*prim1_*asize_;

  // Perform VRR
  for (int ii = 0; ii != prim0_*prim1_; ++ii) {
    const int offset_ii = ii * asize_;
    vector<double*> current_data(nblocks());
    for (int i = 0; i != nblocks(); ++i)
      current_data[i] = intermediate + offset_ii + size_block*i;

    /// Sx(0 : i + j, 0) etc will be made here
    workx[0][0] = coeffsx_[ii];
    worky[0][0] = coeffsy_[ii];
    workz[0][0] = coeffsz_[ii];
    workx[0][1] = (p_[ii*3  ] - basisinfo_[0]->position(0)) * workx[0][0];
    worky[0][1] = (p_[ii*3+1] - basisinfo_[0]->position(1)) * worky[0][0];
    workz[0][1] = (p_[ii*3+2] - basisinfo_[0]->position(2)) * workz[0][0];
    for (int i = 2; i < amax1_; ++i) {
      workx[0][i] = (p_[ii*3  ] - basisinfo_[0]->position(0)) * workx[0][i - 1] + 0.5*(i-1)/xp_[ii] * workx[0][i - 2];
      worky[0][i] = (p_[ii*3+1] - basisinfo_[0]->position(1)) * worky[0][i - 1] + 0.5*(i-1)/xp_[ii] * worky[0][i - 2];
      workz[0][i] = (p_[ii*3+2] - basisinfo_[0]->position(2)) * workz[0][i - 1] + 0.5*(i-1)/xp_[ii] * workz[0][i - 2];
    }

    // take a linear combination to get dipole..
    const double acx = p_[ii*3]   - center_[0];
    const double acy = p_[ii*3+1] - center_[1];
    const double acz = p_[ii*3+2] - center_[2];

    for (int i = 0; i < amax1_; ++i) {
      workx[1][i] = 0.5*i*workx[0][i-1]/xp_[ii] + acx*workx[0][i];
      worky[1][i] = 0.5*i*worky[0][i-1]/xp_[ii] + acy*worky[0][i];
      workz[1][i] = 0.5*i*workz[0][i-1]/xp_[ii] + acz*workz[0][i];
    }

    // take a linear combination to get multipole integrals..
    for (int j = 1; j <= lmax_; ++j) {
      for (int i = 0; i != amax1_; ++i) {
        workx[j][i] = acx*workx[j-1][i] + 0.5/xp_[ii]*(i*workx[j-1][i-1] + (j==1 ? 0.0 : (j-1)*workx[j-2][i]));
        worky[j][i] = acy*worky[j-1][i] + 0.5/xp_[ii]*(i*worky[j-1][i-1] + (j==1 ? 0.0 : (j-1)*worky[j-2][i]));
        workz[j][i] = acz*workz[j-1][i] + 0.5/xp_[ii]*(i*workz[j-1][i-1] + (j==1 ? 0.0 : (j-1)*workz[j-2][i]));
      }
    }

    /// assembly process
    size_t pos = 0;
    for (int i = amin_; i <= amax_; ++i) {
      for (int iz = 0; iz <= i; ++iz) {
        for (int iy = 0; iy <= i - iz; ++iy, ++pos) {
          const int ix = i - iy - iz;

          int lcnt = 0;
          for (int lc = 1; lc <= lmax_; ++lc)
            for (int lz = 0; lz <= lc; ++lz)
              for (int ly = 0; ly <= lc-lz; ++ly, ++lcnt) {
                const int lx = lc - ly - lz;
                current_data[lcnt][pos] = workx[lx][ix] * worky[ly][iy] * workz[lz][iz];
              }
          assert(lcnt == nblocks());
        }
      }
    }

  } // end of primsize loop

  stack_->release(worksize*(lmax_+1), pworkz);
  stack_->release(worksize*(lmax_+1), pworky);
  stack_->release(worksize*(lmax_+1), pworkx);
}

