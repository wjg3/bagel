//
// BAGEL - Parallel electron correlation program.
// Filename: dirac.h
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


#ifndef __SRC_REL_DIRAC_H
#define __SRC_REL_DIRAC_H

#include <string>
#include <map>
#include <src/wfn/method.h>
#include <src/wfn/geometry.h>
#include <src/rel/relhcore.h>
#include <src/rel/reloverlap.h>

namespace bagel {

class Dirac : public Method {
  protected:

    int max_iter_;
    int diis_start_;
    double thresh_scf_;
    double energy_;
    std::unique_ptr<double[]> eig_;
    int ncharge_;
    int nele_;
    int nneg_;

    bool gaunt_;
    bool breit_;

    std::shared_ptr<const RelHcore> hcore_;
    std::shared_ptr<const RelOverlap> overlap_;
    std::shared_ptr<const RelOverlap> s12_;

    std::shared_ptr<const ZMatrix> coeff_;

    void common_init(const std::shared_ptr<const PTree>);
    std::shared_ptr<const DistZMatrix> initial_guess(const std::shared_ptr<const DistZMatrix> s12, const std::shared_ptr<const DistZMatrix> hcore) const;

  public:
    Dirac(const std::shared_ptr<const PTree> idata_, const std::shared_ptr<const Geometry> geom,
          const std::shared_ptr<const Reference> re = std::shared_ptr<const Reference>());

    ~Dirac() { geom_->discard_relativistic(); }

    void compute() override;

    std::shared_ptr<const Reference> conv_to_ref() const override;

    void print_eig() const;
    double energy() const { return energy_; }

    std::shared_ptr<const Geometry> geom() const { return geom_; }

};

}

#endif
