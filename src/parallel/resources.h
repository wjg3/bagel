//
// BAGEL - Parallel electron correlation program.
// Filename: resources.h
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


#ifndef __SRC_PARALLEL_RESOURCES_H
#define __SRC_PARALLEL_RESOURCES_H

// CAUTION last-in-first-out stack to avoid the overhead of new'ing every time

#include <stddef.h>
#include <memory>
#include <atomic>
#include <vector>
#ifdef LIBINT_INTERFACE
  #include <libint2.h>
#endif
#include <src/parallel/process.h>

namespace bagel {

class StackMem {
  protected:
    std::unique_ptr<double[]> stack_area_;
    size_t pointer_;
    const size_t total_;

#ifdef LIBINT_INTERFACE
    std::unique_ptr<Libint_t[]> libint_t_;
#endif

  public:
    StackMem();

    double* get(const size_t size);
    void release(const size_t size, double* p);

    void clear() { pointer_ = 0LU; }
    size_t pointer() const { return pointer_; }

#ifdef LIBINT_INTERFACE
    Libint_t* libint_t_ptr(const int i) { return &libint_t_[i]; }
#endif

};


class Resources {
  private:
    std::shared_ptr<Process> proc_;
    std::vector<std::shared_ptr<StackMem>> stackmem_;
    std::vector<std::shared_ptr<std::atomic_flag>> flag_;
    size_t max_num_threads_;

  public:
    Resources(const int max);

    std::shared_ptr<StackMem> get();
    void release(std::shared_ptr<StackMem> o);

    size_t max_num_threads() const { return max_num_threads_; }
    std::shared_ptr<Process> proc() { return proc_; }
};

extern Resources* resources__;

}

#endif
