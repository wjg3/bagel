//
// BAGEL - Parallel electron correlation program.
// Filename: civec.h
// Copyright (C) 2011 Toru Shiozaki
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


#ifndef BAGEL_FCI_CIVEC_H
#define BAGEL_FCI_CIVEC_H

#include <list>
#include <numeric>
#include <src/parallel/staticdist.h>
#include <src/math/algo.h>
#include <src/util/f77.h>
#include <src/fci/determinants.h>
#include <src/parallel/accrequest.h>
#include <src/parallel/recvrequest.h>

namespace bagel {

template<typename DataType> class Civector;

template<typename DataType>
class DistCivector {
  protected:
    mutable std::shared_ptr<const Determinants> det_;

    // global dimension
    size_t lena_;
    size_t lenb_;

    // local storage
    std::unique_ptr<DataType[]> local_;

    // local alpha strings
    size_t astart_;
    size_t aend_;

    // allocation size
    size_t alloc_;

    // table for alpha string distribution
    const StaticDist dist_;

    // MPI send/receive management
    mutable std::shared_ptr<AccRequest> accum_;
    mutable std::shared_ptr<SendRequest> send_;
    mutable std::shared_ptr<PutRequest> put_;
    mutable std::shared_ptr<RecvRequest> recv_;

    // mutex for write accesses to local_
    mutable std::vector<std::mutex> mutex_;

    // for transpose, buffer can be appended
    mutable std::shared_ptr<DistCivector<DataType>> buf_;
    mutable std::vector<int> transp_;

  public:
    DistCivector(std::shared_ptr<const Determinants> det) : det_(det), lena_(det->lena()), lenb_(det->lenb()), dist_(lena_, mpi__->size()) {
      std::tie(astart_, aend_) = dist_.range(mpi__->rank());
      alloc_ = size();
      local_ = std::unique_ptr<DataType[]>(new DataType[alloc_]);
      std::fill_n(local_.get(), alloc_, 0.0);
      mutex_ = std::vector<std::mutex>(asize());
    }

    DistCivector(const DistCivector<DataType>& o) : det_(o.det_), lena_(o.lena_), lenb_(o.lenb_), dist_(lena_, mpi__->size()) {
      std::tie(astart_, aend_) = dist_.range(mpi__->rank());
      alloc_ = size();
      local_ = std::unique_ptr<DataType[]>(new DataType[alloc_]);
      std::copy_n(o.local_.get(), alloc_, local_.get());
      mutex_ = std::vector<std::mutex>(asize());
    }

    DistCivector<DataType>& operator=(const DistCivector<DataType>& o) {
      assert(o.size() == size());
      std::copy_n(o.local_.get(), alloc_, local_.get());
      return *this;
    }

    DataType& local(const size_t i) { return local_[i]; }
    const DataType& local(const size_t i) const { return local_[i]; }

    DataType* local() { return local_.get(); }
    const DataType* local() const { return local_.get(); }

    size_t size() const { return lenb_*(aend_-astart_); }
    size_t global_size() const { return lena_*lenb_; }
    size_t lena() const { return lena_; }
    size_t lenb() const { return lenb_; }

    size_t astart() const { return astart_; }
    size_t aend() const { return aend_; }
    size_t asize() const { return aend_ - astart_; }

    void zero() { std::fill_n(local_.get(), size(), DataType(0.0)); }

    std::shared_ptr<Civector<DataType>> civec() const { return std::make_shared<Civector<DataType>>(*this); }
    std::shared_ptr<const Determinants> det() const { return det_; }

    std::shared_ptr<DistCivector<DataType>> clone() const { return std::make_shared<DistCivector<DataType>>(det_); }

    // MPI Isend Irecv
    void init_mpi_accumulate() const {
      send_  = std::make_shared<SendRequest>();
      accum_ = std::make_shared<AccRequest>(local_.get(), &mutex_);
    }

    void accumulate_bstring_buf(std::unique_ptr<DataType[]>& buf, const size_t a) const {
      assert(accum_ && send_);
      const size_t mpirank = mpi__->rank();
      size_t rank, off;
      std::tie(rank, off) = dist_.locate(a);

      if (mpirank == rank) {
        std::lock_guard<std::mutex> lock(mutex_[off]);
        std::transform(buf.get(), buf.get()+lenb_, local_.get()+off*lenb_, local_.get()+off*lenb_,
                       [](DataType p, DataType q){ return p+q; });
      } else {
        send_->request_send(std::move(buf), lenb_, rank, off*lenb_);
      }
    }

    void terminate_mpi_accumulate() const {
      assert(accum_ && send_);
      bool done;
      do {
        done = send_->test();
        done &= accum_->test();
#ifndef USE_SERVER_THREAD
        // in case no thread is running behind, we need to cycle this to flush
        size_t d = done ? 0 : 1;
        mpi__->soft_allreduce(&d, 1);
        done = d == 0;
        if (!done) send_->flush();
        if (!done) accum_->flush();
#endif
        if (!done) std::this_thread::sleep_for(sleeptime__);
      } while (!done);
      // cancel all MPI calls
      send_  = std::shared_ptr<SendRequest>();
      accum_ = std::shared_ptr<AccRequest>();
    }

    void init_mpi_recv() const {
      put_   = std::make_shared<PutRequest>(local_.get());
      recv_  = std::make_shared<RecvRequest>();
    }

    int get_bstring_buf(double* buf, const size_t a) const {
      assert(put_ && recv_);
      const size_t mpirank = mpi__->rank();
      size_t rank, off;
      std::tie(rank, off) = dist_.locate(a);

      int out = -1;
      if (mpirank == rank) {
        std::copy_n(local_.get()+off*lenb_, lenb_, buf);
      } else {
        out = recv_->request_recv(buf, lenb_, rank, off*lenb_);
      }
      return out;
    }

    void terminate_mpi_recv() const {
      assert(put_ && recv_);
      bool done;
      do {
        done = recv_->test();
#ifndef USE_SERVER_THREAD
        // in case no thread is running behind, we need to cycle this to flush
        size_t d = done ? 0 : 1;
        mpi__->soft_allreduce(&d, 1);
        done = d == 0;
        if (!done) put_->flush();
#endif
        if (!done) std::this_thread::sleep_for(sleeptime__);
      } while (!done);
      // cancel all MPI calls
      recv_  = std::shared_ptr<RecvRequest>();
      put_   = std::shared_ptr<PutRequest>();
    }

    // utility functions
    DataType dot_product(const DistCivector<DataType>& o) const {
      assert(size() == o.size());
      DataType sum = size() ? inner_product(local(), local()+size(), o.local(), DataType(0.0), std::plus<DataType>(), [](DataType p, DataType q){ return detail::conj(p)*q; })
                            : 0.0;
      mpi__->allreduce(&sum, 1);
      return sum;
    }
    double norm() const { return std::sqrt(detail::real(dot_product(*this))); }
    double variance() const { return detail::real(dot_product(*this)) / (lena_*lenb_); }

    void scale(const DataType a) {
      for (size_t i = 0; i != asize(); ++i) {
        std::lock_guard<std::mutex> lock(mutex_[i]);
        std::transform(local()+i*lenb_, local()+(i+1)*lenb_, local()+i*lenb_, [&a](DataType p){ return a*p; });
      }
    }

    void ax_plus_y(const DataType a, const DistCivector<DataType>& o) {
      assert(size() == o.size());
      for (size_t i = 0; i != asize(); ++i) {
        std::lock_guard<std::mutex> lock(mutex_[i]);
        std::transform(o.local()+i*lenb_, o.local()+(i+1)*lenb_, local()+i*lenb_, local()+i*lenb_, [&a](DataType p, DataType q){ return a*p+q; });
      }
    }

    void project_out(std::shared_ptr<const DistCivector<DataType>> o) { ax_plus_y(-dot_product(*o), *o); }

    double orthog(std::list<std::shared_ptr<const DistCivector<DataType>>> c) {
      for (auto& iter : c)
        project_out(iter);
      const double norm = this->norm();
      const double scal = (norm*norm<1.0e-60 ? 0.0 : 1.0/norm);
      scale(DataType(scal));
      return norm;
    }

    double orthog(std::shared_ptr<const DistCivector<DataType>> o) {
      return orthog(std::list<std::shared_ptr<const DistCivector<DataType>>>{o});
    }

    // mutex
    std::mutex& cimutex(const size_t& i) const { return mutex_[i]; }

// if we use a dedicated server. currently not using this. defined in src/util/serverflush.h
#ifndef USE_SERVER_THREAD
    void flush() const {
      if (accum_) accum_->flush();
      if (send_ ) send_->flush();
      if (put_  ) put_->flush();
    }
#endif

    std::shared_ptr<DistCivector<DataType>> transpose() const {
      auto out = std::make_shared<DistCivector<DataType>>(det_->transpose());
      const size_t myrank = mpi__->rank();

      // transpose each segment
      std::shared_ptr<DistCivector<DataType>> trans = clone();
      for (int i = 0; i != mpi__->size(); ++i) {
        std::tuple<size_t, size_t> outrange = out->dist_.range(i);
        std::tuple<size_t, size_t> thisrange = dist_.range(i);

        std::unique_ptr<DataType[]> tmp(new DataType[out->dist_.size(i)*asize()]);
        for (size_t j = 0; j != asize(); ++j)
          std::copy_n(local()+std::get<0>(outrange)+j*lenb_, out->dist_.size(i), tmp.get()+j*out->dist_.size(i));

        const size_t off = std::get<0>(outrange)*asize();
        std::copy_n(tmp.get(), out->dist_.size(i)*asize(), trans->local()+off);
        if (i != myrank) {
          out->transp_.push_back(mpi__->request_send(trans->local()+off, out->dist_.size(i)*asize(), i, myrank));
          out->transp_.push_back(mpi__->request_recv(out->local()+out->asize()*std::get<0>(thisrange), out->asize()*dist_.size(i), i, i));
        } else {
          std::copy_n(trans->local()+off, out->asize()*asize(), out->local()+astart()*out->asize());
        }
      }
      // keep trans
      out->buf_ = trans;
      return out;
    }

    void transpose_wait() {
      for (auto& i : transp_)
        mpi__->wait(i);
      buf_ = std::shared_ptr<DistCivector<DataType>>();
      buf_ = clone();
      mytranspose_(local(), asize(), lenb_, buf_->local());
      std::copy_n(buf_->local(), asize()*lenb_, local());
      buf_ = std::shared_ptr<DistCivector<DataType>>();
    }

};

using DistCivec = DistCivector<double>;
using ZDistCivec = DistCivector<std::complex<double>>;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// DataType is double or complex<double>
template<typename DataType>
class Civector {
  protected:
    // The determinant space in which this Civec object is defined
    mutable std::shared_ptr<const Determinants> det_;

    size_t lena_;
    size_t lenb_;

    // !!CAUTION!!
    // cc is formated so that B runs first.
    // Also, cc_ can be null if this is constructed by Dvec.
    std::unique_ptr<DataType[]> cc_;

    DataType* cc_ptr_;

    DataType& cc(const size_t& i) { return *(cc_ptr_+i); }
    const DataType& cc(const size_t& i) const { return *(cc_ptr_+i); }
    DataType* cc() { return cc_ptr_; }
    const DataType* cc() const { return cc_ptr_; }

  public:
    Civector(std::shared_ptr<const Determinants> det) : det_(det), lena_(det->lena()), lenb_(det->lenb()) {
      cc_ = std::unique_ptr<DataType[]>(new DataType[lena_*lenb_]);
      cc_ptr_ = cc_.get();
      std::fill_n(cc(), lena_*lenb_, 0.0);
    }

    // constructor that is called by Dvec.
    Civector(std::shared_ptr<const Determinants> det, DataType* din_) : det_(det), lena_(det->lena()), lenb_(det->lenb()) {
      cc_ptr_ = din_;
      std::fill_n(cc(), lena_*lenb_, 0.0);
    }

    // copy constructor
    Civector(const Civector<DataType>& o) : det_(o.det_), lena_(o.lena_), lenb_(o.lenb_) {
      cc_ = std::unique_ptr<DataType[]>(new DataType[lena_*lenb_]);
      cc_ptr_ = cc_.get();
      std::copy_n(o.cc(), lena_*lenb_, cc());
    }

    // from a distribtued Civec  TODO not efficient TODO only works for double
    Civector(const DistCivector<DataType>& o) : det_(o.det()), lena_(o.lena()), lenb_(o.lenb()) {
      cc_ = std::unique_ptr<DataType[]>(new DataType[size()]);
      cc_ptr_ = cc_.get();
      std::fill_n(cc_ptr_, size(), 0.0);
      std::copy_n(o.local(), o.asize()*lenb_, cc()+o.astart()*lenb_);
      mpi__->allreduce(cc_ptr_, size());
    }

    // this is not a copy constructor.
    Civector(std::shared_ptr<Civector<DataType>> o, std::shared_ptr<const Determinants> det) : det_(det), lena_(o->lena_), lenb_(o->lenb_) {
      assert(lena_ == det->lena() && lenb_ == det->lenb());
      cc_ = std::move(o->cc_);
      cc_ptr_ = cc_.get();
    }

    DataType* data() { return cc(); }
    DataType& element(size_t i, size_t j) { return cc(i+j*lenb_); }
    DataType* element_ptr(size_t i, size_t j) { return cc()+i+j*lenb_; }

    DataType& data(const size_t& i) { return cc(i); }
    const DataType& data(const size_t& i) const { return cc(i); }

    const DataType* data() const { return cc(); }
    const DataType* element_ptr(size_t i, size_t j) const { return cc()+i+j*lenb_; }

    std::shared_ptr<const Determinants> det() const { return det_; }
    void set_det(std::shared_ptr<const Determinants> o) const { det_ = o; }

    void zero() { std::fill(cc(), cc()+lena_*lenb_, DataType(0.0)); }

    size_t size() const { return lena_*lenb_; }

    std::shared_ptr<Civector<DataType>> transpose(std::shared_ptr<const Determinants> det = std::shared_ptr<Determinants>()) const {
      if (det == nullptr) det = det_->transpose();
      auto ct = std::make_shared<Civector<DataType>>(det);
      mytranspose_(cc(), lenb_, lena_, ct->data());
      return ct;
    }

    size_t lena() const { return lena_; }
    size_t lenb() const { return lenb_; }

    // some functions for convenience
    void ax_plus_y(DataType a, const Civector<DataType>& other) {
      assert((lena_ == other.lena_) && (lenb_ == other.lenb_));
      std::transform(other.data(), other.data()+size(), cc(), cc(), [&a](DataType p, DataType q){ return a*p+q; });
    }
    void ax_plus_y(DataType a, std::shared_ptr<const Civector> other) { ax_plus_y(a, *other); }

    DataType dot_product(const Civector<DataType>& other) const {
      assert((lena_ == other.lena_) && (lenb_ == other.lenb_));
      return std::inner_product(cc(), cc()+size(), other.data(), DataType(0.0), std::plus<DataType>(), [](DataType p, DataType q){ return detail::conj(p)*q; });
    }
    DataType dot_product(std::shared_ptr<const Civector> other) const { return dot_product(*other); }

    double norm() const { return std::sqrt(detail::real(dot_product(*this))); }
    double variance() const { return detail::real(dot_product(*this)) / size(); }

    void scale(const DataType a) {
      std::transform(cc(), cc()+size(), cc(), [&a](DataType p){ return a*p; });
    }

    // Spin functions are only implememted as specialized functions for double (see civec.cc)
    double spin_expectation() const { assert(false); return 0.0; } // returns < S^2 >
    std::shared_ptr<Civector<DataType>> spin() const { assert(false); return std::shared_ptr<Civector<DataType>>();} // returns S^2 | civec >
    std::shared_ptr<Civector<DataType>> spin_lower(std::shared_ptr<const Determinants> target_det = std::shared_ptr<Determinants>()) const
      { assert(false); return std::shared_ptr<Civector<DataType>>(); } // S_-
    std::shared_ptr<Civector<DataType>> spin_raise(std::shared_ptr<const Determinants> target_det = std::shared_ptr<Determinants>()) const
      { assert(false); return std::shared_ptr<Civector<DataType>>(); } // S_+
    void spin_decontaminate(const double thresh = 1.0e-12) { assert(false); }

    std::shared_ptr<Civector<DataType>> apply(const int orbital, const bool action, const bool spin) const {
      // action: true -> create; false -> annihilate
      // spin: true -> alpha; false -> beta

      std::shared_ptr<const Determinants> source_det = this->det();
      const int norb = source_det->norb();

      const int source_lena = source_det->lena();
      const int source_lenb = source_det->lenb();

      std::shared_ptr<Civector<DataType>> out;

      if (spin) {
        std::shared_ptr<const Determinants> target_det = ( action ? source_det->addalpha() : source_det->remalpha() );
        out = std::make_shared<Civector<DataType>>(target_det);

        const int target_lena = target_det->lena();
        const int target_lenb = target_det->lenb();

        DataType* target_base = out->data();
        const DataType* source_base = this->data();
        for (auto& iter : ( action ? source_det->phiupa(orbital) : source_det->phidowna(orbital) )) {
          const DataType sign = static_cast<DataType>(iter.sign);
          DataType* target = target_base + target_lenb * iter.target;
          const DataType* source = source_base + source_lenb * iter.source;
          std::transform(source, source + target_lenb, target, target, [&sign] (DataType p, DataType q) { return sign * p + q; });
        }
      }
      else {
        std::shared_ptr<const Determinants> target_det = ( action ? source_det->addbeta() : source_det->rembeta() );

        const int target_lena = target_det->lena();
        const int target_lenb = target_det->lenb();

        out = std::make_shared<Civector<DataType>>(target_det);

        for (int i = 0; i < target_lena; ++i) {
          DataType* target_base = out->element_ptr(0,i);
          const DataType* source_base = this->element_ptr(0,i);
          for (auto& iter : ( action ? source_det->phiupb(orbital) : source_det->phidownb(orbital) )) {
            const DataType sign = static_cast<DataType>(iter.sign);
            target_base[iter.target] += sign * source_base[iter.source];
          }
        }
      }

      return out;
    }

    Civector<DataType>& operator*=(const double& a) { scale(a); return *this; }
    Civector<DataType>& operator+=(const double& a) { std::transform(cc(), cc()+size(), cc(), [&a](DataType p){ return p+a; }); return *this; }
    Civector<DataType>& operator-=(const double& a) { std::transform(cc(), cc()+size(), cc(), [&a](DataType p){ return p-a; }); return *this; }

    Civector<DataType>& operator=(const Civector<DataType>& o) { assert(size() == o.size()); std::copy_n(o.cc(), size(), cc()); return *this; }
    Civector<DataType>& operator+=(const Civector<DataType>& o) { ax_plus_y( 1.0, o); return *this; }
    Civector<DataType>& operator-=(const Civector<DataType>& o) { ax_plus_y(-1.0, o); return *this; }
    Civector<DataType>& operator/=(const Civector<DataType>& o) {
      for (size_t i = 0; i != size(); ++i)
        data(i) /= o.data(i);
      return *this;
    }
    Civector<DataType> operator/(const Civector<DataType>& o) const {
      Civector<DataType> out(*this);
      out /= o;
      return out;
    }

    // assumes that Civec's in c are already orthogonal with each other.
    // returns scaling factor (see implementation)

    double orthog(std::list<std::shared_ptr<const Civector<DataType>>> c) {
      for (auto& iter : c)
        project_out(iter);
      const double norm = this->norm();
      const double scal = (norm*norm<1.0e-60 ? 0.0 : 1.0/norm);
      scale(scal);
      return 1.0/scal;
    }

    double orthog(std::shared_ptr<const Civector<DataType>> o) {
      return orthog(std::list<std::shared_ptr<const Civector<DataType>>>{o});
    }

    void project_out(std::shared_ptr<const Civector<DataType>> o) { ax_plus_y(-detail::conj(dot_product(*o)), *o); }

    void print(const double thr) const {
      const DataType* i = cc();
      // multimap sorts elements so that they will be shown in the descending order in magnitude
      std::multimap<double, std::tuple<DataType, std::bitset<nbit__>, std::bitset<nbit__>>> tmp;
      for (auto& ia : det_->stringa()) {
        for (auto& ib : det_->stringb()) {
          if (std::abs(*i) > thr)
            tmp.insert(std::make_pair(-std::abs(*i), std::make_tuple(*i, ia, ib)));
          ++i;
        }
      }
      for (auto& iter : tmp)
        std::cout << "       " << det_->print_bit(std::get<1>(iter.second), std::get<2>(iter.second))
                  << "  " << std::setprecision(10) << std::setw(15) << std::get<0>(iter.second) << std::endl;
    }

    std::shared_ptr<DistCivector<DataType>> distcivec() const {
      auto dist = std::make_shared<DistCivector<DataType>>(det_);
      std::copy_n(cc_ptr_+dist->astart()*lenb_, dist->asize()*lenb_, dist->local());
      return dist;
    }
};

template<> double Civector<double>::spin_expectation() const; // returns < S^2 >
template<> std::shared_ptr<Civector<double>> Civector<double>::spin() const; // returns S^2 | civec >
template<> std::shared_ptr<Civector<double>> Civector<double>::spin_lower(std::shared_ptr<const Determinants>) const; // S_-
template<> std::shared_ptr<Civector<double>> Civector<double>::spin_raise(std::shared_ptr<const Determinants>) const; // S_+
template<> void Civector<double>::spin_decontaminate(const double thresh);

using Civec = Civector<double>;
using ZCivec = Civector<std::complex<double>>;

}

#endif
