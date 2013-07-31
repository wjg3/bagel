//
// BAGEL - Parallel electron correlation program.
// Filename: space.cc
// Copyright (C) 2013 Toru Shiozaki
//
// Author: Michael Caldwell <caldwell@u.northwestern.edu >
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

#include <stdexcept>
#include <src/zfci/relspace.h>
#include <src/math/comb.h>
#include <src/util/combination.hpp>

using namespace std;
using namespace bagel;

RelSpace::RelSpace(const int _norb, const int _nunbar, const int _nbar, const bool _mute)
  : norb_(_norb), nunbar_(_nunbar), nbar_(_nbar), mute_(_mute) {

  common_init();
}
#if 0
Space::Space(shared_ptr<const Determinants> det, int _M, const bool _compress, const bool _mute) :
  norb_(det->norb()), nelea_(det->nelea()), neleb_(det->neleb()), M_(_M), compress_(_compress), mute_(_mute) {

  common_init();
}
#endif
void RelSpace::common_init() {
  if (!mute_) cout << " **Constructing " << 2*(norb_ - nelec()/2)+1 << " determinant spaces by Kramers index" << endl;
  for (int M_ = -(norb_-nelec()/2) ; M_ <= norb_ - nelec()/2; ++M_) {
    if (!mute_) cout << " **Constructing space of all determinants with Kramers index "
                     << kramers(M_) << endl << endl;
    // TODO double check that this is putting all of the possible dets with correct Mk in same space
    auto tmpdet = make_shared<Determinants>(norb_, nunbar_ + M_, nbar_ - M_, 0, 0);
    detmap_.insert(pair<int,shared_ptr<Determinants>>(kramers(M_), tmpdet));
  }
    if (!mute_) cout << " **Total space is made up of " << detmap_.size() << " determinants." << endl;
#if 0
  if (!mute_) cout << "  o forming alpha links" << endl;

  int nlinks = 0;
  for(auto idet = detmap_.begin(); idet != detmap_.end(); ++idet) {
    int na = idet->second->nelea(); int nb = idet->second->neleb();
    auto jdet = detmap_.find(key_(na-nelea_+1,nb-neleb_));
    if(jdet==detmap_.end()) continue;
    else {
      idet->second->link<0>(jdet->second);
      ++nlinks;
    }
  }
  if (!mute_) cout << "    - " << nlinks << " links formed" << endl;

  if (!mute_) cout << "  o forming beta links" << endl;

  nlinks = 0;
  for(auto idet = detmap_.begin(); idet != detmap_.end(); ++idet) {
    int na = idet->second->nelea(); int nb = idet->second->neleb();
    auto jdet = detmap_.find(key_(na-nelea_,nb-neleb_+1));
    if(jdet==detmap_.end()) continue;
    else {
      idet->second->link<1>(jdet->second);
      ++nlinks;
    }
  }
  if (!mute_) cout << "    - " << nlinks << " links formed" << endl;
#endif
}
