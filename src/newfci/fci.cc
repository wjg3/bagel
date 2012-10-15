//
// BAGEL - Parallel electron correlation program.
// Filename: fci.cc
// Copyright (C) 2011 Toru Shiozaki
//
// Author: Toru Shiozaki <shiozaki@northwestern.edu>
// Maintainer: Shiozaki group
//
// This file is part of the BAGEL package.
//
// The BAGEL package is free software; you can redistribute it and\/or modify
// it under the terms of the GNU Library General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
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

#include <string>
#include <vector>
#include <bitset>
#include <iostream>
#include <iomanip>
#include <stdexcept>

#include <src/newfci/fci.h>
#include <src/newfci/space.h>
#include <src/rysint/eribatch.h>
#include <src/util/combination.hpp>
#include <src/util/constants.h>
#include <src/util/davidson.h>

using namespace std;
using namespace std::chrono;
using namespace bagel;

NewFCI::NewFCI(std::multimap<std::string, std::string> idat, shared_ptr<const Reference> r, const int ncore, const int norb, const int nstate)
 : idata_(idat), ref_(r), geom_(r->geom()), ncore_(ncore), norb_(norb), nstate_(nstate) {
  common_init();
}

void NewFCI::common_init() {
  print_header();

  const bool frozen = read_input<bool>(idata_, "frozen", false);
  max_iter_ = read_input<int>(idata_, "maxiter", 100);
  max_iter_ = read_input<int>(idata_, "maxiter_fci", max_iter_);
  thresh_ = read_input<double>(idata_, "thresh", 1.0e-20);
  thresh_ = read_input<double>(idata_, "thresh_fci", thresh_);

  if (nstate_ < 0) nstate_ = read_input<int>(idata_, "nstate", 1);
  if (ncore_ < 0) ncore_ = read_input<int>(idata_, "ncore", (frozen ? geom_->num_count_ncore_only()/2 : 0));
  if (norb_  < 0) norb_ = read_input<int>(idata_, "norb", ref_->coeff()->ndim()-ncore_);

  // nspin is #unpaired electron 0:singlet, 1:doublet, 2:triplet, ... (i.e., Molpro convention).
  const int nspin = read_input<int>(idata_, "nspin", 0);
  if ((geom_->nele()+nspin) % 2 != 0) throw runtime_error("Invalid nspin specified");
  nelea_ = (geom_->nele()+nspin)/2 - ncore_;
  neleb_ = (geom_->nele()-nspin)/2 - ncore_;

  // TODO allow for zero electron (quick return)
  if (nelea_ <= 0 || neleb_ <= 0) throw runtime_error("#electrons cannot be zero/negative in NewFCI");
  for (int i = 0; i != nstate_; ++i) weight_.push_back(1.0/static_cast<double>(nstate_));

  // resizing rdm vectors (with null pointers)
  rdm1_.resize(nstate_);
  rdm2_.resize(nstate_);
  energy_.resize(nstate_);

  // construct a determinant space in which this NewFCI will be performed.
  det_ = shared_ptr<const NewDeterminants>(new NewDeterminants(norb_, nelea_, neleb_));
}

NewFCI::~NewFCI() { }

void NewFCI::print_timing_(const string label, high_resolution_clock::time_point& time, std::vector<pair<string, double> >& timing) const {
  timing.push_back(make_pair(label, duration_cast<milliseconds>(high_resolution_clock::now()-time).count()*0.001));
  time = high_resolution_clock::now();
}

// generate initial vectors
//   - bits: bit patterns of low-energy determinants
//   - nspin: #alpha - #beta
//   - out:
void NewFCI::generate_guess(const int nspin, const int nstate, std::shared_ptr<NewDvec> out) {
  int ndet = nstate_*10;
  start_over:
  vector<pair<bitset<nbit__>, bitset<nbit__> > > bits = detseeds(ndet);

  // Spin adapt detseeds
  int oindex = 0;
  vector<bitset<nbit__> > done;
  for( auto& it : bits ) {
    bitset<nbit__> alpha = it.second;
    bitset<nbit__> beta = it.first;
    bitset<nbit__> open_bit = (alpha^beta);

    // make sure that we have enough unpaired alpha
    const int unpairalpha = (alpha ^ (alpha & beta)).count();
    const int unpairbeta  = (beta ^ (alpha & beta)).count();
    if (unpairalpha-unpairbeta < nelea_-neleb_) continue; 

    // check if this orbital configuration is already used
    if (find(done.begin(), done.end(), open_bit) != done.end()) continue;
    done.push_back(open_bit);

    pair<vector<tuple<int, int, int> >, double> adapt = det()->spin_adapt(nelea_-neleb_, alpha, beta);
    const double fac = adapt.second;
    for( auto& iter : adapt.first ) {
      out->data(oindex)->element(get<0>(iter), get<1>(iter)) = get<2>(iter)*fac;
    }   

    cout << "     guess " << setw(3) << oindex << ":   closed " <<
          setw(20) << left << det()->print_bit(alpha&beta) << " open " << setw(20) << det()->print_bit(open_bit) << right << endl;

    ++oindex;
    if (oindex == nstate) break;
  }
  if (oindex < nstate) {
    out->zero();
    ndet *= 4;
    goto start_over;
  }
  cout << endl;
}

// returns seed determinants for initial guess
vector<pair<bitset<nbit__> , bitset<nbit__> > > NewFCI::detseeds(const int ndet) {
  multimap<double, pair<bitset<nbit__>,bitset<nbit__> > > tmp;
  for (int i = 0; i != ndet; ++i) tmp.insert(make_pair(-1.0e10*(1+i), make_pair(bitset<nbit__>(0),bitset<nbit__>(0))));

  double* diter = denom_->data();
  for (auto& aiter : det()->stringa()) {
    for (auto& biter : det()->stringb()) {
      const double din = -(*diter);
      if (tmp.begin()->first < din) {
        tmp.insert(make_pair(din, make_pair(biter, aiter)));
        tmp.erase(tmp.begin());
      } 
      ++diter;
    }
  }
  assert(tmp.size() == ndet || ndet > det()->stringa().size()*det()->stringb().size());
  vector<pair<bitset<nbit__> , bitset<nbit__> > > out;
  for (auto iter = tmp.rbegin(); iter != tmp.rend(); ++iter)
    out.push_back(iter->second);
  return out;
}

void NewFCI::print_header() const {
  cout << "  ---------------------------" << endl;
  cout << "        NewFCI calculation      " << endl;
  cout << "  ---------------------------" << endl << endl;
}

void NewFCI::compute() {
  // at the moment I only care about C1 symmetry, with dynamics in mind
  if (geom_->nirrep() > 1) throw runtime_error("NewFCI: C1 only at the moment."); 

  // some constants
  //const int ij = nij(); 

  // Creating an initial CI vector
  shared_ptr<NewDvec> cc_tmp(new NewDvec(det_, nstate_)); // B runs first
  cc_ = cc_tmp;

  // find determinants that have small diagonal energies
  generate_guess(nelea_-neleb_, nstate_, cc_); 

  // nuclear energy retrieved from geometry
  const double nuc_core = geom_->nuclear_repulsion() + jop_->core_energy();

  // Davidson utility
  DavidsonDiag<NewCivec> davidson(nstate_, max_iter_);

  // main iteration starts here
  cout << "  === NewFCI iteration ===" << endl << endl;
  // 0 means not converged
  vector<int> conv(nstate_,0);

  for (int iter = 0; iter != max_iter_; ++iter) { 
    auto tp1 = high_resolution_clock::now();

    // form a sigma vector given cc
    shared_ptr<NewDvec> sigma = form_sigma(cc_, jop_, conv);

    // constructing NewDvec's for Davidson
    shared_ptr<const NewDvec> ccn(new NewDvec(cc_));
    shared_ptr<const NewDvec> sigman(new NewDvec(sigma));
    const vector<double> energies = davidson.compute(ccn->dvec(conv), sigman->dvec(conv));

    // get residual and new vectors
    vector<shared_ptr<NewCivec> > errvec = davidson.residual();

    // compute errors
    vector<double> errors;
    for (int i = 0; i != nstate_; ++i) {
      errors.push_back(errvec[i]->variance());
      conv[i] = static_cast<int>(errors[i] < thresh_);
    }

    if (!*min_element(conv.begin(), conv.end())) {
      // denominator scaling 
      for (int ist = 0; ist != nstate_; ++ist) {
        if (conv[ist]) continue;
        const int size = cc_->data(ist)->size();
        double* target_array = cc_->data(ist)->data();
        double* source_array = errvec[ist]->data();
        double* denom_array = denom_->data();
        const double en = energies[ist];
        for (int i = 0; i != size; ++i) {
          target_array[i] = source_array[i] / min(en - denom_array[i], -0.1);
        }
        davidson.orthog(cc_->data(ist));
        list<shared_ptr<const NewCivec> > tmp;
        for (int jst = 0; jst != ist; ++jst) tmp.push_back(cc_->data(jst)); 
        cc_->data(ist)->orthog(tmp);
      }
    }

    // printing out
    auto tp2 = high_resolution_clock::now();
    auto dr = duration_cast<milliseconds>(tp2-tp1);
    if (nstate_ != 1 && iter) cout << endl;
    for (int i = 0; i != nstate_; ++i) {
      cout << setw(7) << iter << setw(3) << i << setw(2) << (conv[i] ? "*" : " ")
                              << setw(17) << fixed << setprecision(8) << energies[i]+nuc_core << "   "
                              << setw(10) << scientific << setprecision(2) << errors[i] << fixed << setw(10) << setprecision(2)
                              << dr.count()*0.001 << endl; 
      energy_[i] = energies[i]+nuc_core;
    }
    if (*min_element(conv.begin(), conv.end())) break;
  }
  // main iteration ends here

  shared_ptr<NewDvec> s(new NewDvec(davidson.civec()));
  s->print();
  cc_ = shared_ptr<NewDvec>(new NewDvec(s));
}
