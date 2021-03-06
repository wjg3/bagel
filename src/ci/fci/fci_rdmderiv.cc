//
// BAGEL - Brilliantly Advanced General Electronic Structure Library
// Filename: fci_rdmderiv.cc
// Copyright (C) 2011 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
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


#include <src/ci/fci/fci.h>
#include <src/wfn/rdm.h>
#include <src/util/math/algo.h>
#include <src/util/parallel/rmawindow.h>

using namespace std;
using namespace bagel;


shared_ptr<Dvec> FCI::rdm1deriv(const int target) const {

  auto detex = make_shared<Determinants>(norb_, nelea_, neleb_, false, /*mute=*/true);
  cc_->set_det(detex);
  shared_ptr<Civec> cbra = cc_->data(target);

  // 1RDM ci derivative
  // <I|E_ij|0>

  auto dbra = make_shared<Dvec>(cbra->det(), norb_*norb_);
  dbra->zero();
  sigma_2a1(cbra, dbra);
  sigma_2a2(cbra, dbra);

  return dbra;
}


shared_ptr<Dvec> FCI::rdm2deriv(const int target) const {

  auto detex = make_shared<Determinants>(norb_, nelea_, neleb_, false, /*mute=*/true);
  cc_->set_det(detex);
  shared_ptr<Civec> cbra = cc_->data(target);

  // make  <I|E_ij|0>
  auto dbra = make_shared<Dvec>(cbra->det(), norb_*norb_);
  dbra->zero();
  sigma_2a1(cbra, dbra);
  sigma_2a2(cbra, dbra);

  // second make <J|E_kl|I><I|E_ij|0> - delta_li <J|E_kj|0>
  auto ebra = make_shared<Dvec>(cbra->det(), norb_*norb_*norb_*norb_);
  auto tmp = make_shared<Dvec>(cbra->det(), norb_*norb_);
  int ijkl = 0;
  int ij = 0;
  for (auto iter = dbra->dvec().begin(); iter != dbra->dvec().end(); ++iter, ++ij) {
    const int j = ij/norb_;
    const int i = ij-j*norb_;
    tmp->zero();
    sigma_2a1(*iter, tmp);
    sigma_2a2(*iter, tmp);
    int kl = 0;
    for (auto t = tmp->dvec().begin(); t != tmp->dvec().end(); ++t, ++ijkl, ++kl) {
      *ebra->data(ijkl) = **t;
      const int l = kl/norb_;
      const int k = kl-l*norb_;
      if (l == i) *ebra->data(ijkl) -= *dbra->data(k+norb_*j);
    }
  }
  return ebra;
}


shared_ptr<Matrix> FCI::rdm2deriv_offset(const int target, const size_t offset, const size_t size) const {

  auto detex = make_shared<Determinants>(norb_, nelea_, neleb_, false, /*mute=*/true);
  cc_->set_det(detex);
  shared_ptr<Civec> cbra = cc_->data(target);

  // make  <I|E_ij|0>
  auto dbra = make_shared<Dvec>(cbra->det(), norb_*norb_);
  dbra->zero();
  sigma_2a1(cbra, dbra);
  sigma_2a2(cbra, dbra);

  const int norb2 = norb_ * norb_;
  const int lena = cc_->det()->lena();
  const int lenb = cc_->det()->lenb();
  auto emat = make_shared<Matrix>(size, norb2*norb2);

  for (int ij = 0; ij != norb2; ++ij) {
    const int j = ij/norb_;
    const int i = ij-j*norb_;

    for (int kl = ij; kl != norb2; ++kl) {
      if ((kl - ij) % mpi__->size() != mpi__->rank()) continue;
      const int l = kl/norb_;
      const int k = kl-l*norb_;
      const int klij = kl+ij*norb2;

      for (auto& iter : cc_->det()->phia(k,l)) {
        size_t iaJ = iter.source;
        size_t iaI = iter.target;
        double sign = static_cast<double>(iter.sign);
        for (size_t ib = 0; ib != lenb; ++ib) {
          size_t iI = ib + iaI*lenb;
          size_t iJ = ib + iaJ*lenb;
          if ((iJ - offset) < size)
            emat->element(iJ-offset, klij) += sign * dbra->data(ij)->data(iI);
        }
      }

      for (size_t ia = 0; ia != lena; ++ia) {
        for (auto& iter : cc_->det()->phib(k,l)) {
          size_t ibJ = iter.source;
          size_t ibI = iter.target;
          double sign = static_cast<double>(iter.sign);
          size_t iI = ibI + ia*lenb;
          size_t iJ = ibJ + ia*lenb;
          if ((iJ - offset) < size)
            emat->element(iJ-offset, klij) += sign * dbra->data(ij)->data(iI);
        }
      }

      if (i == l) {
        const int kj = k+j*norb_;
        for (size_t iJ = offset; iJ != offset+size; ++iJ) {
          emat->element(iJ-offset, klij) -= dbra->data(kj)->data(iJ);
        }
      }
    }
  }
  emat->allreduce();

  for (size_t ij = 0; ij != norb2; ++ij)
    for (size_t kl = 0; kl != ij; ++kl) {
      size_t klij = kl + ij*norb2;
      size_t ijkl = ij + kl*norb2;
      for (size_t iJ = 0; iJ != size; ++iJ)
        emat->element(iJ,klij) = emat->element(iJ,ijkl);
    }

  return emat;
}


shared_ptr<Matrix> FCI::rdm3deriv(const int target, shared_ptr<const Matrix> fock, const size_t offset, const size_t size) const {
#ifndef HAVE_MPI_H
  throw logic_error("FCI::rdm3deriv should not be called without MPI");
#endif
  auto detex = make_shared<Determinants>(norb_, nelea_, neleb_, false, /*mute=*/true);
  cc_->set_det(detex);
  shared_ptr<Civec> cbra = cc_->data(target);

  const size_t norb2 = norb_*norb_;
  const size_t norb3 = norb2*norb_;
  const size_t norb4 = norb2*norb2;

  // first make <I|i+j|0>
  auto dbra = rdm1deriv(target);

  const size_t ndet = cbra->det()->size();
  const size_t ijmax = 635040001;
  const size_t ijnum = ndet * norb2 * norb2;
  const size_t npass = (ijnum-1) / ijmax + 1;
  const size_t nsize = (ndet-1) / npass + 1;

  // form [J|k+l|0] = <J|m+k+ln|0> f_mn (multipassing)
  auto fock_ebra_mat = make_shared<Matrix>(ndet, norb2);
  for (size_t ipass = 0; ipass != npass; ++ipass) {
    const size_t ioffset = ipass * nsize;
    const size_t isize = (ipass != (npass - 1)) ? nsize : ndet - ioffset;
    shared_ptr<Matrix> ebra;
    ebra = rdm2deriv_offset(target, ioffset, isize);

    for (size_t kl = 0; kl != norb2; ++kl) {
      if (kl % mpi__->size() != mpi__->rank()) continue;
      for (size_t mn = 0; mn != norb2; ++mn) {
        const size_t n = mn/norb_;
        const size_t m = mn-n*norb_;
        const size_t klmn = kl * norb2 + mn;
        for (size_t iI = 0; iI != isize; ++iI)
          fock_ebra_mat->element(iI+ioffset,kl) += fock->element(m,n) * ebra->element(iI,klmn);
      }
    }
  }
  fock_ebra_mat->allreduce();

  // then form [L|k+i+jl|0] <- <L|i+j|K>[K|k+l|0] + ...
  auto fock_fbra = make_shared<Matrix>(size, norb4);
  // set ebra within the pass now
  auto ebra = rdm2deriv_offset(target, offset, size);
  const int lena = cc_->det()->lena();
  const int lenb = cc_->det()->lenb();

  for (int ij = 0; ij != norb2; ++ij) {
    const int j = ij/norb_;
    const int i = ij-j*norb_;

    for (int kl = ij; kl != norb2; ++kl) {
      if ((kl - ij) % mpi__->size() != mpi__->rank()) continue;
      const int l = kl/norb_;
      const int k = kl-l*norb_;
      const int klij = kl+ij*norb2;

      for (auto& iter : cc_->det()->phia(k,l)) {
        size_t iaJ = iter.source;
        size_t iaI = iter.target;
        double sign = static_cast<double>(iter.sign);
        for (size_t ib = 0; ib != lenb; ++ib) {
          size_t iI = ib + iaI*lenb;
          size_t iJ = ib + iaJ*lenb;
          if ((iJ - offset) < size)
            fock_fbra->element(iJ-offset, klij) += sign * fock_ebra_mat->element(iI,ij);
        }
      }

      for (size_t ia = 0; ia != lena; ++ia) {
        for (auto& iter : cc_->det()->phib(k,l)) {
          size_t ibJ = iter.source;
          size_t ibI = iter.target;
          double sign = static_cast<double>(iter.sign);
          size_t iI = ibI + ia*lenb;
          size_t iJ = ibJ + ia*lenb;
          if ((iJ - offset) < size)
            fock_fbra->element(iJ-offset, klij) += sign * fock_ebra_mat->element(iI,ij);
        }
      }

      if (i == l) {
        const int kj = k+j*norb_;
        for (size_t iJ = 0; iJ != size; ++iJ) {
          fock_fbra->element(iJ, klij) -= fock_ebra_mat->element(iJ+offset,kj);
        }
      }

      for (int n = 0; n != norb_; ++n) {
        const size_t ijkn = j*norb3+i*norb2+n*norb_+k;
        for (size_t iJ = 0; iJ != size; ++iJ)
          fock_fbra->element(iJ, klij) -= ebra->element(iJ, ijkn) * fock->element(l,n);
      }
    }
  }
  fock_fbra->allreduce();

  for (size_t ij = 0; ij != norb2; ++ij)
    for (size_t kl = 0; kl != ij; ++kl) {
      size_t klij = kl + ij*norb2;
      size_t ijkl = ij + kl*norb2;
      for (size_t iJ = 0; iJ != size; ++iJ)
        fock_fbra->element(iJ,klij) = fock_fbra->element(iJ,ijkl);
    }

  return fock_fbra;
}


tuple<shared_ptr<Matrix>,shared_ptr<Matrix>> FCI::rdm34deriv(const int target, shared_ptr<const Matrix> fock, const size_t offset, const size_t size) const {
#ifndef HAVE_MPI_H
  throw logic_error("FCI::rdm34deriv should not be called without MPI");
#endif
  assert(fock->ndim() == norb_ && fock->mdim() == norb_);
  auto detex = make_shared<Determinants>(norb_, nelea_, neleb_, false, /*mute=*/true);
  cc_->set_det(detex);
  shared_ptr<Civec> cbra = cc_->data(target);

  const size_t ndet = detex->size();
  const size_t norb2 = norb_*norb_;
  const size_t norb3 = norb2*norb_;
  const size_t norb4 = norb2*norb2;
  const size_t norb5 = norb3*norb2;
  const size_t norb6 = norb4*norb2;

  // first make <I|i+j|0>
  auto dbra = rdm1deriv(target);
  // second make <J|k+i+jl|0> = <J|k+l|I><I|i+j|0> - delta_li <J|k+j|0>
  auto ebra = rdm2deriv(target);

  // third make <K|m+k+i+jln|0>  =  <K|m+n|J><J|k+i+jl|0> - delta_nk<K|m+i+jl|0> - delta_ni<K|k+m+jl|0>
  // K is reduced to (offset, offset+size)
  auto fbra = make_shared<Matrix>(size, norb6);
  // [L|k+i+jl|0]
  auto fock_fbra = make_shared<Dvec>(cbra->det(), norb4);
  {
    RMAWindow_bare<double> fbra_win(size*norb6);
    RMAWindow_bare<double> fock_fbra_win(ndet*norb4);
    auto tmp = make_shared<Dvec>(cbra->det(), norb2);
    auto tmp2 = make_shared<Civec>(cbra->det());
    int ijkl = 0;
    for (auto iter = ebra->dvec().begin(); iter != ebra->dvec().end(); ++iter, ++ijkl) {
      if (ijkl % mpi__->size() != mpi__->rank()) continue;
      const int j = ijkl/norb3;
      const int i = ijkl/norb2-j*norb_;
      const int l = ijkl/norb_-i*norb_-j*norb2;
      const int k = ijkl-l*norb_-i*norb2-j*norb3;
      tmp->zero();
      tmp2->zero();
      sigma_2a1(*iter, tmp);
      sigma_2a2(*iter, tmp);
      vector<shared_ptr<RMATask<double>>> tasks;
      for (int m = 0; m != norb_; ++m)
        for (int n = 0; n != norb_; ++n) {
          if (k == n)
            tmp->data(m+norb_*n)->ax_plus_y(-1.0, ebra->data(m+norb_*(l+norb_*(i+norb_*j))));
          if (i == n)
            tmp->data(m+norb_*n)->ax_plus_y(-1.0, ebra->data(k+norb_*(l+norb_*(m+norb_*j))));
          // put this to the window
          for (int inode = 0; inode != mpi__->size(); ++inode)
            tasks.push_back(fbra_win.rma_rput(tmp->data(m+norb_*n)->data() + offset, inode, size*(ijkl+norb4*m+norb5*n), size));
          // axpy for Fock-weighted 3RDM derivative
          tmp2->ax_plus_y(fock->element(m,n), tmp->data(m+norb_*n));
        }
      for (int inode = 0; inode != mpi__->size(); ++inode)
        tasks.push_back(fock_fbra_win.rma_rput(tmp2->data(), inode, ndet*ijkl, ndet));
      // wait till all the work is done
      for (auto& itask : tasks)
        itask->wait();
    }
    fbra_win.fence();
    copy_n(fbra_win.local_data(), size*norb6, fbra->data());
    fock_fbra_win.fence();
    copy_n(fock_fbra_win.local_data(), ndet*norb4, fock_fbra->data());
  }

  // now make target:  f_mn <L|E_op,mn,kl,ij|0> = [L|E_kl,ij,op|0]  =  <L|o+p|K>[K|E_kl,ij|0] - f_np<L|E_kl,ij,on|0> - delta_pk[L|E_ij,ol|0] - delta_pi[L|E_kl,oj|0]
  auto gbra = fbra->clone();
  {
    RMAWindow_bare<double> gbra_win(size*norb6);
    auto tmp = make_shared<Dvec>(cbra->det(), norb2);
    int ijkl = 0;
    for (auto iter = fock_fbra->dvec().begin(); iter != fock_fbra->dvec().end(); ++iter, ++ijkl) {
      if (ijkl % mpi__->size() != mpi__->rank()) continue;
      const int j = ijkl/norb3;
      const int i = ijkl/norb2-j*norb_;
      const int l = ijkl/norb_-i*norb_-j*norb2;
      const int k = ijkl-l*norb_-i*norb2-j*norb3;
      tmp->zero();
      sigma_2a1(*iter, tmp);
      sigma_2a2(*iter, tmp);
      vector<shared_ptr<RMATask<double>>> tasks;
      for (int o = 0; o != norb_; ++o)
        for (int p = 0; p != norb_; ++p) {
          if (k == p)
            blas::ax_plus_y_n(-1.0, fock_fbra->data(i+norb_*(j+norb_*(o+norb_*l)))->data() + offset, size, tmp->data(o+norb_*p)->data() + offset);
          if (i == p)
            blas::ax_plus_y_n(-1.0, fock_fbra->data(k+norb_*(l+norb_*(o+norb_*j)))->data() + offset, size, tmp->data(o+norb_*p)->data() + offset);
          for (int inode = 0; inode != mpi__->size(); ++inode)
            tasks.push_back(gbra_win.rma_rput(tmp->data(o+norb_*p)->data() + offset, inode, size*(ijkl+norb4*o+norb5*p), size));
        }
      // wait till all the work is done
      for (auto& itask : tasks)
        itask->wait();
    }
    gbra_win.fence();
    blas::ax_plus_y_n(1.0, gbra_win.local_data(), size*norb6, gbra->data());
    dgemm_("N", "N", fbra->size()/norb_, norb_, norb_, -1.0, fbra->data(), fbra->size()/norb_, fock->data(), norb_, 1.0, gbra->data(), fbra->size()/norb_);
  }

  return make_tuple(fbra, gbra);
}
