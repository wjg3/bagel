//
// Author : Toru Shiozaki
// Date   : Dec 2011
// Blas & Lapack interface 
//

#ifndef __src_util_f77_h
#define __src_util_f77_h

#include <complex>

extern "C" {

 // transposition
 void mytranspose_(const double*, const int*, const int*, double*);
 void mytranspose1_(const double*, const int*, const int*, double*);
 void mytranspose4_(const double*, const int*, const int*, double*);
 void mytranspose_complex_(const std::complex<double>*, const int*, const int*, std::complex<double>*);

 void dcopy_(const int*, const double*, const int*, double*, const int*);
 void daxpy_(const int*, const double*, const double*, const int*, double*, const int*);
 void dsyev_(const char*, const char*, const int*, double*, const int*, double*, double*, const int*, int*); 
 void dscal_(const int*, const double*, double*, const int*);
 const double ddot_(const int*, const double*, const int*, const double*, const int*); 
 void dgemv_(const char*, const int*, const int*, const double*, const double*, const int*, const double*, const int*,
             const double*, double*, const int*);
 void dgemm_(const char* transa, const char* transb, const int* m, const int* n, const int* k, 
             const double* alpha, const double* a, const int* lda, const double* b, const int* ldb, 
             const double* beta, double* c, const int* ldc);
 void dsysv_(const char* uplo, const int* n, const int* nrhs, double* a, const int* lda, int* ipiv, 
             double* b, const int* ldb, double* work, const int* lwork, int* info);

 void zcopy_(const int*, const std::complex<double>*, const int*, std::complex<double>*, const int*);
 void zscal_(const int*, const std::complex<double>*, std::complex<double>*, const int*);
#ifndef ZDOT_RETURN 
 void zdotu_(std::complex<double>*, const int*, const std::complex<double>*, const int*, const std::complex<double>*, const int*); 
 void zdotc_(std::complex<double>*, const int*, const std::complex<double>*, const int*, const std::complex<double>*, const int*); 
#else
 std::complex<double> zdotu_(const int*, const std::complex<double>*, const int*, const std::complex<double>*, const int*); 
 std::complex<double> zdotc_(const int*, const std::complex<double>*, const int*, const std::complex<double>*, const int*); 
#endif
 void zaxpy_(const int*, const std::complex<double>*, const std::complex<double>*, const int*, std::complex<double>*, const int*);
 void zheev_(const char*, const char*, const int*, std::complex<double>*, const int*, double*, std::complex<double>*, const int*, double*, int*); 
 void zgeev_(const char*, const char*, const int*, std::complex<double>*, const int*, std::complex<double>*, 
             std::complex<double>*, const int*, std::complex<double>*, const int*, std::complex<double>*, const int*, double*, int*);
 void zgemm_(const char* transa, const char* transb, const int* m, const int* n, const int* k, 
             const std::complex<double>* alpha, const std::complex<double>* a, const int* lda, const std::complex<double>* b, const int* ldb, 
             const std::complex<double>* beta, std::complex<double>* c, const int* ldc);
 void zhesv_(const char* uplo, const int* n, const int* nrhs, std::complex<double>* a, const int* lda, int* ipiv, 
             std::complex<double>* b, const int* ldb, std::complex<double>* work, const int* lwork, int* info);
 void zgesv_(const int* n, const int* nrhs, std::complex<double>* a, const int* lda, int* ipiv, 
             std::complex<double>* b, const int* ldb, int* info);
 void zgesvd_(const char*, const char*, const int*, const int*, std::complex<double>*, const int*, const double*,
     std::complex<double>*, const int*, std::complex<double>*, const int*,  std::complex<double>*, const int*, double*, int*);
 void zgesdd_(const char*, const int*, const int*, std::complex<double>*, const int*, const double*,
     std::complex<double>*, const int*, std::complex<double>*, const int*,  std::complex<double>*, const int*, double*, int*, int*);

};


static void dgemm_(const char* transa, const char* transb, const int m, const int n, const int k, 
                   const double alpha, const double* a, const int lda, const double* b, const int ldb, 
                   const double beta, double* c, const int ldc) { dgemm_(transa,transb,&m,&n,&k,&alpha,a,&lda,b,&ldb,&beta,c,&ldc); }; 
static void daxpy_(const int a, const double b, const double* c, const int d, double* e, const int f) {daxpy_(&a,&b,c,&d,e,&f); };

#endif
