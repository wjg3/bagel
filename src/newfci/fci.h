//
// BAGEL - Parallel electron correlation program.
// Filename: fci.h
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

// Desc :: The implementation closely follows Knowles and Handy 1984 CPL.
//         It is amazing how easy it is to implement NewFCI !!
//

#ifndef __NEWINT_NEWFCI_FCI_H
#define __NEWINT_NEWFCI_FCI_H

#define __NEWFCI_DEBUGGING

#include <tuple>
#include <src/scf/scf.h>
#include <cassert>
#include <iostream>
#include <memory>
#include <bitset>
#include <src/util/input.h>
#include <src/util/constants.h>
#include <src/newfci/dvec.h>
#include <src/newfci/mofile.h>
#include <src/wfn/rdm.h>
#include <src/wfn/reference.h>
#include <src/newfci/determinants.h>

namespace bagel {

class NewFCI {

  protected:
    // input
    std::multimap<std::string, std::string> idata_; 
    // reference
    std::shared_ptr<const Reference> ref_;
    // geometry file
    const std::shared_ptr<const Geometry> geom_;
    // max #iteration
    int max_iter_;
    // threshold for variants
    double thresh_;

    // numbers of electrons
    int nelea_;
    int neleb_;
    int ncore_;
    int norb_;

    // number of states
    int nstate_;

    // total energy
    std::vector<double> energy_;

    // CI vector at convergence
    std::shared_ptr<NewDvec> cc_;
    // RDMs; should be resized in constructors
    std::vector<std::shared_ptr<RDM<1> > > rdm1_;
    std::vector<std::shared_ptr<RDM<2> > > rdm2_;
    // state averaged RDM
    std::vector<double> weight_;
    std::shared_ptr<RDM<1> > rdm1_av_;
    std::shared_ptr<RDM<2> > rdm2_av_;
    // MO integrals 
    std::shared_ptr<NewMOFile> jop_;

    //
    // Below here, internal private members
    //

    // Determinant space
    std::shared_ptr<const NewDeterminants> det_;

    // denominator
    std::shared_ptr<NewCivec> denom_;

    // some init functions
    void common_init();
    void create_Jiiii();
    // generate spin-adapted guess configurations
    void generate_guess(const int nspin, const int nstate, std::shared_ptr<NewDvec>);
    // denominator
    void const_denom();
    // obtain determinants for guess generation
    std::vector<std::pair<std::bitset<nbit__>, std::bitset<nbit__> > > detseeds(const int ndet);

  
    // run-time functions
    void sigma_1(std::shared_ptr<const NewCivec> cc, std::shared_ptr<NewCivec> sigma, std::shared_ptr<const NewMOFile> jop) const;
    void sigma_3(std::shared_ptr<const NewCivec> cc, std::shared_ptr<NewCivec> sigma, std::shared_ptr<const NewMOFile> jop) const;
    void sigma_2a1(std::shared_ptr<const NewCivec> cc, std::shared_ptr<NewDvec> d) const;
    void sigma_2a2(std::shared_ptr<const NewCivec> cc, std::shared_ptr<NewDvec> d) const;
    void sigma_2b (std::shared_ptr<NewDvec> d, std::shared_ptr<NewDvec> e, std::shared_ptr<const NewMOFile> jop) const;
    void sigma_2c1(std::shared_ptr<NewCivec> sigma, std::shared_ptr<const NewDvec> e) const;
    void sigma_2c2(std::shared_ptr<NewCivec> sigma, std::shared_ptr<const NewDvec> e) const;

    // functions related to natural orbitals
    void update_rdms(const std::vector<double>& coeff); 

    // internal function for RDM1 and RDM2 computations 
    std::tuple<std::shared_ptr<RDM<1> >, std::shared_ptr<RDM<2> > >
      compute_rdm12_last_step(std::shared_ptr<const NewDvec>, std::shared_ptr<const NewDvec>, std::shared_ptr<const NewCivec>) const;

    // print functions
    void print_header() const;
    void print_timing_(const std::string, int& time, std::vector<std::pair<std::string, double> >&) const; 

  public:
    // this constructor is ugly... to be fixed some day...
    NewFCI(const std::multimap<std::string, std::string>, std::shared_ptr<const Reference>,
        const int ncore = -1, const int nocc = -1, const int nstate = -1);
    ~NewFCI();
    void compute();
    void update(std::shared_ptr<const Coeff> );

    // returns members
    int norb() const { return norb_; };
    int nelea() const { return nelea_; };
    int neleb() const { return neleb_; };
    int nij() const { return norb_*(norb_+1)/2; };
    int ncore() const { return ncore_; };
    double core_energy() const { return jop_->core_energy(); };

    double weight(const int i) const { return weight_[i]; };

    // application of Hamiltonian
    std::shared_ptr<NewDvec> form_sigma(std::shared_ptr<const NewDvec> c, std::shared_ptr<const NewMOFile> jop, const std::vector<int>& conv) const;

    // rdms
    void compute_rdm12(); // compute all states at once + averaged rdm
    void compute_rdm12(const int istate);

    std::tuple<std::shared_ptr<RDM<1> >, std::shared_ptr<RDM<2> > >
      compute_rdm12_from_civec(std::shared_ptr<const NewCivec>, std::shared_ptr<const NewCivec>) const;
    std::tuple<std::shared_ptr<RDM<1> >, std::shared_ptr<RDM<2> > >
      compute_rdm12_av_from_dvec(std::shared_ptr<const NewDvec>, std::shared_ptr<const NewDvec>,
                                 std::shared_ptr<const NewDeterminants> o = std::shared_ptr<const NewDeterminants>()) const;

    std::vector<std::shared_ptr<RDM<1> > > rdm1() { return rdm1_; };
    std::vector<std::shared_ptr<RDM<2> > > rdm2() { return rdm2_; };
    std::shared_ptr<RDM<1> > rdm1(const int i) { return rdm1_.at(i); };
    std::shared_ptr<RDM<2> > rdm2(const int i) { return rdm2_.at(i); };
    std::shared_ptr<const RDM<1> > rdm1(const int i) const { return rdm1_.at(i); };
    std::shared_ptr<const RDM<2> > rdm2(const int i) const { return rdm2_.at(i); };
    std::shared_ptr<RDM<1> > rdm1_av() { return rdm1_av_; };
    std::shared_ptr<RDM<2> > rdm2_av() { return rdm2_av_; };
    std::shared_ptr<const RDM<1> > rdm1_av() const { return rdm1_av_; };
    std::shared_ptr<const RDM<2> > rdm2_av() const { return rdm2_av_; };
    // move to natural orbitals
    std::pair<std::vector<double>, std::vector<double> > natorb_convert();

    const std::shared_ptr<const Geometry> geom() const { return geom_; };

    std::shared_ptr<const NewDeterminants> det() const { return det_; };

    // returns integral files
    std::shared_ptr<const NewMOFile> jop() const { return jop_; };

    // returns a denominator
    std::shared_ptr<const NewCivec> denom() const { return denom_; };

    // returns total energy
    std::vector<double> energy() const { return energy_; };
    double energy(const int i) const { return energy_.at(i); };

    // returns CI vectors
    std::shared_ptr<NewDvec> civectors() const { return cc_; };

};

}

#endif

