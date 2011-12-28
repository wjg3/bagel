//
// Author : Toru Shiozaki
// Date   : May 2009
//

#include <src/rysint/carsphlist.h>
#include <src/util/f77.h>


void CarSphList::carsph_40(const int nloop, const double* source, double* target) {
  const double c7 = 6.7082039324993694;
  const double c4 = 6.2749501990055663;
  const double c1 = 4.4370598373247123;
  const double c5 = 3.3541019662496847;
  const double c9 = 3.1622776601683795;
  const double c11 = 3;
  const double c2 = 2.9580398915498081;
  const double c10 = 2.3717082451262845;
  const double c3 = 2.0916500663351889;
  const double c8 = 1.1180339887498949;
  const double c13 = 0.75;
  const double c0 = 0.73950997288745202;
  const double c6 = 0.55901699437494745;
  const double c12 = 0.375;
  for (int iloop = 0; iloop != nloop; ++iloop, target += 9, source += 15) {
    target[0] =  c0 * source[0] - c1 * source[2] + c0 * source[4];
    target[1] =  c2 * source[1] - c2 * source[3];
    target[2] =  c3 * source[5] - c4 * source[7];
    target[3] =  c4 * source[6] - c3 * source[8];
    target[4] =  c5 * source[9] - c5 * source[11] - c6 * source[0]
                  + c6 * source[2] - c6 * source[2] + c6 * source[4];
    target[5] =  c7 * source[10] - c8 * source[1] - c8 * source[3];
    target[6] =  c9 * source[12] - c10 * source[5] - c10 * source[7];
    target[7] =  c9 * source[13] - c10 * source[6] - c10 * source[8];
    target[8] =  source[14] - c11 * source[9] - c11 * source[11]
                  + c12 * source[0] + c13 * source[2] + c12 * source[4];
  }
}

