#define USE_FC_LEN_T
#include <string>
#include "util.h"
#include "rpg.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#define R_NO_REMAP
#include <R.h>
#include <Rmath.h>
#include <Rinternals.h>
#include <R_ext/Linpack.h>
#include <R_ext/Lapack.h>
#include <R_ext/BLAS.h>
#ifndef FCONE
# define FCONE
#endif

//Description: update B and F.
void updateBF1Int(double *B, double *F, double *c, double *C, double *coords, int *nnIndx, int *nnIndxLU, int n, int m, double sigmaSq, double phi, double nu, int covModel, double *bk, double nuUnifb){
    
  int i, k, l;
  int info = 0;
  int inc = 1;
  double one = 1.0;
  double zero = 0.0;
  char lower = 'L';

  //bk must be 1+(int)floor(alpha) * nthread
  int nb = 1+static_cast<int>(floor(nuUnifb));
  int threadID = 0;
  double e;
  int mm = m*m;
  
#ifdef _OPENMP
#pragma omp parallel for private(k, l, info, threadID, e)
#endif
    for(i = 0; i < n; i++){
#ifdef _OPENMP
      threadID = omp_get_thread_num();
#endif
      if(i > 0){
	for(k = 0; k < nnIndxLU[n+i]; k++){
	  e = dist2(coords[i], coords[n+i], coords[nnIndx[nnIndxLU[i]+k]], coords[n+nnIndx[nnIndxLU[i]+k]]);
	  c[m*threadID+k] = sigmaSq*spCor(e, phi, nu, covModel, &bk[threadID*nb]);
	  for(l = 0; l <= k; l++){
	    e = dist2(coords[nnIndx[nnIndxLU[i]+k]], coords[n+nnIndx[nnIndxLU[i]+k]], coords[nnIndx[nnIndxLU[i]+l]], coords[n+nnIndx[nnIndxLU[i]+l]]); 
	    C[mm*threadID+l*nnIndxLU[n+i]+k] = sigmaSq*spCor(e, phi, nu, covModel, &bk[threadID*nb]); 
	  }
	}
	F77_NAME(dpotrf)(&lower, &nnIndxLU[n+i], &C[mm*threadID], &nnIndxLU[n+i], &info FCONE); if(info != 0){Rf_error("c++ error: dpotrf failed\n");}
	F77_NAME(dpotri)(&lower, &nnIndxLU[n+i], &C[mm*threadID], &nnIndxLU[n+i], &info FCONE); if(info != 0){Rf_error("c++ error: dpotri failed\n");}
	F77_NAME(dsymv)(&lower, &nnIndxLU[n+i], &one, &C[mm*threadID], &nnIndxLU[n+i], &c[m*threadID], &inc, &zero, &B[nnIndxLU[i]], &inc FCONE);
	F[i] = sigmaSq - F77_NAME(ddot)(&nnIndxLU[n+i], &B[nnIndxLU[i]], &inc, &c[m*threadID], &inc);
      }else{
	B[i] = 0;
	F[i] = sigmaSq;
      }
    }

}

extern "C" {
  SEXP spIntPGOccNNGP(SEXP y_r, SEXP X_r, SEXP Xp_r, SEXP coords_r, 
		      SEXP pOcc_r, SEXP pDet_r, SEXP pDetLong_r, 
	              SEXP J_r, SEXP JLong_r, SEXP K_r, SEXP nObs_r, SEXP nObsLong_r, SEXP nData_r, 
	              SEXP m_r, SEXP nnIndx_r, SEXP nnIndxLU_r, SEXP uIndx_r, 
		      SEXP uIndxLU_r, SEXP uiIndx_r,
		      SEXP betaStarting_r, SEXP alphaStarting_r, SEXP zStarting_r, 
		      SEXP wStarting_r, SEXP phiStarting_r, SEXP sigmaSqStarting_r, 
		      SEXP nuStarting_r, SEXP zLongIndx_r, SEXP dataIndx_r, SEXP alphaIndx_r, 
		      SEXP muBeta_r, SEXP muAlpha_r, SEXP SigmaBeta_r, SEXP sigmaAlpha_r, 
		      SEXP phiA_r, SEXP phiB_r, SEXP sigmaSqA_r, SEXP sigmaSqB_r, 
		      SEXP nuA_r, SEXP nuB_r, SEXP tuning_r, SEXP covModel_r, 
		      SEXP nBatch_r, SEXP batchLength_r, 
		      SEXP acceptRate_r, SEXP nThreads_r, SEXP verbose_r, SEXP nReport_r, 
      	              SEXP nBurn_r, SEXP nThin_r, SEXP nPost_r, SEXP currChain_r, SEXP nChain_r, 
		      SEXP fixedSigmaSq_r, SEXP sigmaSqIG_r){
   
    /**********************************************************************
     * Initial constants
     * *******************************************************************/
    int i, j, k, t, s, r, q, info, nProtect=0;
    const int inc = 1;
    const double one = 1.0;
    const double zero = 0.0;
    char const *lower = "L";
    char const *ntran = "N";
    char const *ytran = "T";
    
    /**********************************************************************
     * Get Inputs
     * *******************************************************************/
    // Sorted by data set, then visit, then by site. 
    double *y = REAL(y_r);
    double *X = REAL(X_r);
    // Sorted by parameter, then data set, site, visit
    double *Xp = REAL(Xp_r);
    int pOcc = INTEGER(pOcc_r)[0];
    int pDet = INTEGER(pDet_r)[0];
    int nData = INTEGER(nData_r)[0]; 
    int *pDetLong = INTEGER(pDetLong_r); 
    int ppDet = pDet * pDet;
    int ppOcc = pOcc * pOcc; 
    double *coords = REAL(coords_r); 
    int m = INTEGER(m_r)[0]; 
    // Priors for regression coefficients
    double *muBeta = (double *) R_alloc(pOcc, sizeof(double));   
    F77_NAME(dcopy)(&pOcc, REAL(muBeta_r), &inc, muBeta, &inc);
    double *muAlpha = (double *) R_alloc(pDet, sizeof(double));   
    F77_NAME(dcopy)(&pDet, REAL(muAlpha_r), &inc, muAlpha, &inc);
    double *SigmaBetaInv = (double *) R_alloc(ppOcc, sizeof(double));   
    F77_NAME(dcopy)(&ppOcc, REAL(SigmaBeta_r), &inc, SigmaBetaInv, &inc);
    double *sigmaAlpha = REAL(sigmaAlpha_r); 
    double phiA = REAL(phiA_r)[0];
    double phiB = REAL(phiB_r)[0]; 
    double nuA = REAL(nuA_r)[0]; 
    double nuB = REAL(nuB_r)[0]; 
    double sigmaSqA = REAL(sigmaSqA_r)[0]; 
    double sigmaSqB = REAL(sigmaSqB_r)[0]; 
    double *tuning = REAL(tuning_r); 
    int *nnIndx = INTEGER(nnIndx_r);
    int *nnIndxLU = INTEGER(nnIndxLU_r);
    int *uIndx = INTEGER(uIndx_r);
    int *uIndxLU = INTEGER(uIndxLU_r);
    int *uiIndx = INTEGER(uiIndx_r);
    int covModel = INTEGER(covModel_r)[0];
    std::string corName = getCorName(covModel);
    int J = INTEGER(J_r)[0];
    int *zLongIndx = INTEGER(zLongIndx_r); 
    int nObs = INTEGER(nObs_r)[0]; 
    // Rprintf("nObs: %i\n", nObs); 
    int *nObsLong = INTEGER(nObsLong_r); 
    int *dataIndx = INTEGER(dataIndx_r); 
    int *alphaIndx = INTEGER(alphaIndx_r); 
    int nBatch = INTEGER(nBatch_r)[0]; 
    int batchLength = INTEGER(batchLength_r)[0]; 
    int nSamples = nBatch * batchLength; 
    int nThin = INTEGER(nThin_r)[0]; 
    int nBurn = INTEGER(nBurn_r)[0]; 
    int nPost = INTEGER(nPost_r)[0]; 
    int currChain = INTEGER(currChain_r)[0];
    int nChain = INTEGER(nChain_r)[0];
    double acceptRate = REAL(acceptRate_r)[0];
    int nThreads = INTEGER(nThreads_r)[0];
    int verbose = INTEGER(verbose_r)[0];
    int nReport = INTEGER(nReport_r)[0]; 
    int status = 0; 
    // For looping through data sets
    int stNObs = 0; 
    int stAlpha = 0; 
    int thinIndx = 0; 
    int fixedSigmaSq = INTEGER(fixedSigmaSq_r)[0];
    int sigmaSqIG = INTEGER(sigmaSqIG_r)[0];
    int sPost = 0; 

#ifdef _OPENMP
    omp_set_num_threads(nThreads);
#else
    if(nThreads > 1){
      Rf_warning("n.omp.threads > %i, but source not compiled with OpenMP support.", nThreads);
      nThreads = 1;
    }
#endif
    
    /**********************************************************************
     * Print Information 
     * *******************************************************************/
    if(verbose){
      if (currChain == 1) {
        Rprintf("----------------------------------------\n");
        Rprintf("\tModel description\n");
        Rprintf("----------------------------------------\n");
        Rprintf("NNGP Integrated Occupancy Model with Polya-Gamma latent\nvariable fit with %i sites.\n\n", J);
        Rprintf("Integrating %i occupancy data sets.\n\n", nData); 
        Rprintf("Samples per chain: %i (%i batches of length %i)\n", nSamples, nBatch, batchLength);
        Rprintf("Burn-in: %i \n", nBurn); 
        Rprintf("Thinning Rate: %i \n", nThin); 
        Rprintf("Number of Chains: %i \n", nChain);
        Rprintf("Total Posterior Samples: %i \n\n", nPost * nChain); 
        Rprintf("Using the %s spatial correlation model.\n\n", corName.c_str());
        Rprintf("Using %i nearest neighbors.\n\n", m);
#ifdef _OPENMP
        Rprintf("\nSource compiled with OpenMP support and model fit using %i thread(s).\n\n", nThreads);
#else
        Rprintf("Source not compiled with OpenMP support.\n\n");
#endif
        Rprintf("Adaptive Metropolis with target acceptance rate: %.1f\n", 100*acceptRate);
      }
      Rprintf("----------------------------------------\n");
      Rprintf("\tChain %i\n", currChain);
      Rprintf("----------------------------------------\n");
      Rprintf("Sampling ... \n");
    }

    /**********************************************************************
     * Parameters
     * *******************************************************************/
    // Occupancy covariates
    double *beta = (double *) R_alloc(pOcc, sizeof(double));   
    F77_NAME(dcopy)(&pOcc, REAL(betaStarting_r), &inc, beta, &inc);
    // Detection covariates
    double *alpha = (double *) R_alloc(pDet, sizeof(double));   
    F77_NAME(dcopy)(&pDet, REAL(alphaStarting_r), &inc, alpha, &inc);
    // Spatial random effects
    double *w = (double *) R_alloc(J, sizeof(double));   
    F77_NAME(dcopy)(&J, REAL(wStarting_r), &inc, w, &inc);
    // Latent Occurrence
    double *z = (double *) R_alloc(J, sizeof(double));   
    F77_NAME(dcopy)(&J, REAL(zStarting_r), &inc, z, &inc);
    // Spatial smooth parameter for matern. 
    double nu = REAL(nuStarting_r)[0]; 
    // Auxiliary variables
    double *omegaDet = (double *) R_alloc(nObs, sizeof(double)); zeros(omegaDet, nObs);
    double *omegaOcc = (double *) R_alloc(J, sizeof(double)); zeros(omegaOcc, J);
    double *kappaDet = (double *) R_alloc(nObs, sizeof(double)); zeros(kappaDet, nObs);
    double *kappaOcc = (double *) R_alloc(J, sizeof(double)); zeros(kappaOcc, J);

    /**********************************************************************
     * Return Stuff
     * *******************************************************************/
    SEXP betaSamples_r;
    PROTECT(betaSamples_r = Rf_allocMatrix(REALSXP, pOcc, nPost)); nProtect++;
    SEXP alphaSamples_r; 
    PROTECT(alphaSamples_r = Rf_allocMatrix(REALSXP, pDet, nPost)); nProtect++;
    SEXP zSamples_r; 
    PROTECT(zSamples_r = Rf_allocMatrix(REALSXP, J, nPost)); nProtect++; 
    SEXP psiSamples_r; 
    PROTECT(psiSamples_r = Rf_allocMatrix(REALSXP, J, nPost)); nProtect++; 
    SEXP wSamples_r; 
    PROTECT(wSamples_r = Rf_allocMatrix(REALSXP, J, nPost)); nProtect++; 
    
    /**********************************************************************
     * Other initial starting stuff
     * *******************************************************************/
    int JpOcc = J * pOcc; 
    int nObspDet = nObs * pDet;
    int jj, kk;
    double *tmp_ppDet = (double *) R_alloc(ppDet, sizeof(double));
    double *tmp_ppOcc = (double *) R_alloc(ppOcc, sizeof(double)); 
    double *tmp_pDet = (double *) R_alloc(pDet, sizeof(double));
    double *tmp_pOcc = (double *) R_alloc(pOcc, sizeof(double));
    double *tmp_pDet2 = (double *) R_alloc(pDet, sizeof(double));
    double *tmp_pOcc2 = (double *) R_alloc(pOcc, sizeof(double));
    int *tmp_J = (int *) R_alloc(J, sizeof(int));
    for (j = 0; j < J; j++) {
      tmp_J[j] = zero; 
    }
    double *tmp_JpOcc = (double *) R_alloc(JpOcc, sizeof(double));
    double *tmp_nObspDet = (double *) R_alloc(nObspDet, sizeof(double));
    double *tmp_J1 = (double *) R_alloc(J, sizeof(double));
   
    // For latent occupancy
    double psiNum; 
    double *detProb = (double *) R_alloc(nObs, sizeof(double)); 
    double *psi = (double *) R_alloc(J, sizeof(double)); 
    zeros(psi, J); 
    double *piProd = (double *) R_alloc(J, sizeof(double)); 
    ones(piProd, J); 
    double *ySum = (double *) R_alloc(J, sizeof(double)); zeros(ySum, J);

    // For normal priors
    // Occupancy regression coefficient priors. 
    // Compute cholesky
    F77_NAME(dpotrf)(lower, &pOcc, SigmaBetaInv, &pOcc, &info FCONE); 
    if(info != 0){Rf_error("c++ error: dpotrf SigmaBetaInv failed\n");}
    // Compute inverse
    F77_NAME(dpotri)(lower, &pOcc, SigmaBetaInv, &pOcc, &info FCONE); 
    if(info != 0){Rf_error("c++ error: dpotri SigmaBetaInv failed\n");}
    double *SigmaBetaInvMuBeta = (double *) R_alloc(pOcc, sizeof(double)); 
    F77_NAME(dsymv)(lower, &pOcc, &one, SigmaBetaInv, &pOcc, muBeta, &inc, &zero, 
        	    SigmaBetaInvMuBeta, &inc FCONE);
    // Detection regression coefficient priors. 
    // Have "separate" multivariate normal priors for the different sets of coefficients
    // that vary across the data sets. 
    // Get size of vector
    int currSize = 0; 
    // Index of starting prior values. 
    int *alphaSigmaIndx = (int *) R_alloc(nData, sizeof(int)); 
    int *alphaMuIndx = (int *) R_alloc(nData, sizeof(int)); 
    int tmp0 = 0; 
    int tmp02 = 0; 
    for (q = 0; q < nData; q++) {
      currSize += pDetLong[q] * pDetLong[q];  
      alphaSigmaIndx[q] = tmp0; 
      tmp0 += pDetLong[q] * pDetLong[q]; 
      alphaMuIndx[q] = tmp02; 
      tmp02 += pDetLong[q]; 
    } // q
    double *SigmaAlphaInv = (double *) R_alloc(currSize, sizeof(double)); zeros(SigmaAlphaInv, currSize); 
    double *SigmaAlphaInvMuAlpha = (double *) R_alloc(pDet, sizeof(double)); 
    // Fill SigmaAlpha
    for (q = 0, j = 0; q < nData; q++) {
      for (i = 0; i < pDetLong[q]; i++, j++) {
        SigmaAlphaInv[alphaSigmaIndx[q] + i * pDetLong[q] + i] = sigmaAlpha[j]; 
	// Rprintf("Index: %i\n", alphaSigmaIndx[q] + i * pDetLong[q] + i); 
      } // i
      F77_NAME(dpotrf)(lower, &pDetLong[q], &SigmaAlphaInv[alphaSigmaIndx[q]], &pDetLong[q], &info FCONE); 
      if(info != 0){Rf_error("c++ error: dpotrf SigmaAlphaInv failed\n");}
      F77_NAME(dpotri)(lower, &pDetLong[q], &SigmaAlphaInv[alphaSigmaIndx[q]], &pDetLong[q], &info FCONE); 
      if(info != 0){Rf_error("c++ error: dpotri SigmaAlphaInv failed\n");}
      F77_NAME(dsymv)(lower, &pDetLong[q], &one, &SigmaAlphaInv[alphaSigmaIndx[q]], &pDetLong[q], &muAlpha[alphaMuIndx[q]], &inc, &zero, 
                     &SigmaAlphaInvMuAlpha[alphaMuIndx[q]], &inc FCONE);
    } // q

    /**********************************************************************
     * Set up spatial stuff and MH stuff
     * *******************************************************************/
    int nTheta, sigmaSqIndx, phiIndx, nuIndx;
    if (corName != "matern") {
      nTheta = 2; // sigma^2, phi 
      sigmaSqIndx = 0; phiIndx = 1; 
    } else {
      nTheta = 3; // sigma^2, phi, nu 
      sigmaSqIndx = 0; phiIndx = 1; nuIndx = 2; 
    }  
    double *accept = (double *) R_alloc(nTheta, sizeof(double)); zeros(accept, nTheta); 
    double *theta = (double *) R_alloc(nTheta, sizeof(double));
    double logPostCurrent = 0.0, logPostCand = 0.0;
    double logDet;  
    double phiCand = 0.0, nuCand = 0.0, sigmaSqCand = 0.0;  
    SEXP acceptSamples_r; 
    PROTECT(acceptSamples_r = Rf_allocMatrix(REALSXP, nTheta, nBatch)); nProtect++; 
    SEXP tuningSamples_r; 
    PROTECT(tuningSamples_r = Rf_allocMatrix(REALSXP, nTheta, nBatch)); nProtect++; 
    SEXP thetaSamples_r; 
    PROTECT(thetaSamples_r = Rf_allocMatrix(REALSXP, nTheta, nPost)); nProtect++; 
    double a, v, b, e, mu, var, aij; 
    // Initiate spatial values
    theta[sigmaSqIndx] = REAL(sigmaSqStarting_r)[0]; 
    theta[phiIndx] = REAL(phiStarting_r)[0]; 
    if (corName == "matern") {
      theta[nuIndx] = nu; 
    } 
    // Allocate for the U index vector that keep track of which locations have 
    // the i-th location as a neighbor
    int nIndx = static_cast<int>(static_cast<double>(1+m)/2*m+(J-m-1)*m);

    // For NNGP
    int mm = m*m;
    double *B = (double *) R_alloc(nIndx, sizeof(double));
    double *F = (double *) R_alloc(J, sizeof(double));
    double *BCand = (double *) R_alloc(nIndx, sizeof(double));
    double *FCand = (double *) R_alloc(J, sizeof(double));
    double *c =(double *) R_alloc(m*nThreads, sizeof(double));
    double *C = (double *) R_alloc(mm*nThreads, sizeof(double));


    double *bk = (double *) R_alloc(nThreads*(1.0+static_cast<int>(floor(nuB))), sizeof(double));

    if (corName == "matern") {
      nu = theta[nuIndx];
    }
    updateBF1Int(B, F, c, C, coords, nnIndx, nnIndxLU, J, m, theta[sigmaSqIndx], theta[phiIndx], nu, covModel, bk, nuB);

    GetRNGstate();
   
    for (s = 0, t = 0; s < nBatch; s++) {
      for (r = 0; r < batchLength; r++, t++) {
        /********************************************************************
         *Update Occupancy Auxiliary Variables 
         *******************************************************************/
        for (j = 0; j < J; j++) {
          omegaOcc[j] = rpg(1.0, F77_NAME(ddot)(&pOcc, &X[j], &J, beta, &inc) + w[j]);
        } // j
        /********************************************************************
         *Update Detection Auxiliary Variables 
         *******************************************************************/
        for (i = 0; i < nObs; i++) {
          stAlpha = which(dataIndx[i], alphaIndx, pDet); 
          omegaDet[i] = 0.0;
	  if (z[zLongIndx[i]] == 1.0) {
            omegaDet[i] = rpg(1.0, F77_NAME(ddot)(&pDetLong[dataIndx[i]], &Xp[i], &nObs, &alpha[stAlpha], &inc));
	  }
        } // i
           
        /********************************************************************
         *Update Occupancy Regression Coefficients
         *******************************************************************/
        for (j = 0; j < J; j++) {
          kappaOcc[j] = z[j] - 1.0 / 2.0; 
	  tmp_J1[j] = kappaOcc[j] - omegaOcc[j] * w[j]; 
        } // j
        /********************************
         * Compute b.beta
         *******************************/
        // X * tmp_J1 + 0 * tmp_p. Output is stored in tmp_p
        F77_NAME(dgemv)(ytran, &J, &pOcc, &one, X, &J, tmp_J1, &inc, &zero, tmp_pOcc, &inc FCONE); 	 
        for (j = 0; j < pOcc; j++) {
          tmp_pOcc[j] += SigmaBetaInvMuBeta[j]; 
        } // j 
        /********************************
         * Compute A.beta
         * *****************************/
        // tmp_JpOcc is X %*% omegaOcc. 
        for(j = 0; j < J; j++){
          for(i = 0; i < pOcc; i++){
            tmp_JpOcc[i*J+j] = X[i*J+j]*omegaOcc[j];
          }
        }
        // This finishes off A.beta
        // 1 * X * tmp_JpOcc + 0 * tmp_ppOcc = tmp_ppOcc
        F77_NAME(dgemm)(ytran, ntran, &pOcc, &pOcc, &J, &one, X, &J, tmp_JpOcc, &J, &zero, tmp_ppOcc, &pOcc FCONE FCONE);
        for (j = 0; j < ppOcc; j++) {
          tmp_ppOcc[j] += SigmaBetaInv[j]; 
        } // j
        F77_NAME(dpotrf)(lower, &pOcc, tmp_ppOcc, &pOcc, &info FCONE); 
        if(info != 0){Rf_error("c++ error: dpotrf here failed\n");}
        F77_NAME(dpotri)(lower, &pOcc, tmp_ppOcc, &pOcc, &info FCONE); 
        if(info != 0){Rf_error("c++ error: dpotri here failed\n");}
        // 1 * tmp_ppOcc * tmp_pOcc + 0 * tmp_pOcc2 
        F77_NAME(dsymv)(lower, &pOcc, &one, tmp_ppOcc, &pOcc, tmp_pOcc, &inc, &zero, tmp_pOcc2, &inc FCONE);
        F77_NAME(dpotrf)(lower, &pOcc, tmp_ppOcc, &pOcc, &info FCONE); 
	if(info != 0){Rf_error("c++ error: dpotrf here failed\n");}
        // Args: destination, mu, cholesky of the covariance matrix, dimension
        mvrnorm(beta, tmp_pOcc2, tmp_ppOcc, pOcc);

      
        /********************************************************************
         *Update Detection Regression Coefficients
         *******************************************************************/
        for (q = 0; q < nData; q++) {
          // Rprintf("q: %i\n", q); 
          // Starting locations
          stNObs = which(q, dataIndx, nObs); 
          stAlpha = which(q, alphaIndx, pDet); 
          // Rprintf("nObsLong[%i]: %i\n", q, nObsLong[q]); 
          /********************************
           * Compute b.alpha
           *******************************/
          // First multiply kappaDet * the current occupied values, such that values go 
          // to 0 if z == 0 and values go to kappaDet if z == 1
          for (i = 0; i < nObsLong[q]; i++) {
            // 1.0 is currently hardcoded in for occupancy data
            kappaDet[stNObs + i] = (y[stNObs + i] - 1.0/2.0) * z[zLongIndx[stNObs + i]];
          } // i
          // Xp * kappaDet + 0 * tmp_pDet. Output is stored in tmp_pDet
          F77_NAME(dgemv)(ytran, &nObsLong[q], &pDetLong[q], &one, &Xp[stNObs], &nObs, &kappaDet[stNObs], &inc, &zero, &tmp_pDet[stAlpha], &inc FCONE); 	  
          for (j = 0; j < pDetLong[q]; j++) {
            tmp_pDet[stAlpha + j] += SigmaAlphaInvMuAlpha[stAlpha + j]; 
            // Rprintf("tmp_pDet: %f\n", tmp_pDet[stAlpha + j]); 
          } // j


          /********************************
           * Compute A.alpha
           * *****************************/
          for (j = 0; j < nObsLong[q]; j++) {
            for (i = 0; i < pDetLong[q]; i++) {
              tmp_nObspDet[stNObs + i*nObs + j] = Xp[stNObs + i * nObs + j] * omegaDet[stNObs + j] * z[zLongIndx[stNObs + j]];
              // Rprintf("tmp_nObspDet: %f\n", tmp_nObspDet[stNObs + i*nObs + j]);  
              // Rprintf("omegaDet[%i]: %f\n", stNObs + j, omegaDet[stNObs + j]); 
            } // i
          } // j

          // This finishes off A.alpha
          // 1 * Xp * tmp_nObspDet + 0 * tmp_ppDet = tmp_ppDet
          F77_NAME(dgemm)(ytran, ntran, &pDetLong[q], &pDetLong[q], &nObsLong[q], &one, &Xp[stNObs], &nObs, 
			  &tmp_nObspDet[stNObs], &nObs, &zero, &tmp_ppDet[alphaSigmaIndx[q]], &pDetLong[q] FCONE FCONE);

          for (j = 0; j < pDetLong[q] * pDetLong[q]; j++) {
            tmp_ppDet[alphaSigmaIndx[q] + j] += SigmaAlphaInv[alphaSigmaIndx[q] + j]; 
            // Rprintf("tmp_ppDet: %f\n", tmp_ppDet[alphaSigmaIndx[q] + j]); 
          } // j

          // This gives the Cholesky of A.alpha
          // Computes cholesky of tmp_ppDet. Output stored in tmp_ppOcc
          F77_NAME(dpotrf)(lower, &pDetLong[q], &tmp_ppDet[alphaSigmaIndx[q]], &pDetLong[q], &info FCONE); 
          if(info != 0){Rf_error("c++ error: dpotrf A.alpha failed\n");}
          // Computes the inverse tmp_ppOcc. Stored in tmp_ppOcc. This is A.beta.inv. 
          F77_NAME(dpotri)(lower, &pDetLong[q], &tmp_ppDet[alphaSigmaIndx[q]], &pDetLong[q], &info FCONE); 
          if(info != 0){Rf_error("c++ error: dpotri A.alpha failed\n");}
          // A.alpha.inv %*% b.alpha
          // 1 * tmp_ppDet * tmp_pDet + 0 * tmp_pDet2 
          // (which is currently nothing) = tmp_pDet2
          F77_NAME(dsymv)(lower, &pDetLong[q], &one, &tmp_ppDet[alphaSigmaIndx[q]], &pDetLong[q], &tmp_pDet[stAlpha], &inc, &zero, &tmp_pDet2[stAlpha], &inc FCONE);
          // Computes cholesky of tmp_ppDet again stored back in tmp_ppDet. This chol(A.alpha.inv)
          F77_NAME(dpotrf)(lower, &pDetLong[q], &tmp_ppDet[alphaSigmaIndx[q]], &pDetLong[q], &info FCONE); 
          if(info != 0){Rf_error("c++ error: dpotrf here failed\n");}
          // Args: destination, mu, cholesky of the covariance matrix, dimension
          mvrnorm(&alpha[stAlpha], &tmp_pDet2[stAlpha], &tmp_ppDet[alphaSigmaIndx[q]], pDetLong[q]);
          // for (j = 0; j < pDetLong[q]; j++) {
          //   Rprintf("alpha[%i]: %f\n", stAlpha + j, alpha[stAlpha + j]); 
          // } // j
        } // q

        /********************************************************************
         *Update w (spatial random effects)
         *******************************************************************/
	for (i = 0; i < J; i++ ) {
          a = 0;
	  v = 0;
	  if (uIndxLU[J + i] > 0){ // is i a neighbor for anybody
	    for (j = 0; j < uIndxLU[J+i]; j++){ // how many locations have i as a neighbor
	      b = 0;
	      // now the neighbors for the jth location who has i as a neighbor
	      jj = uIndx[uIndxLU[i]+j]; // jj is the index of the jth location who has i as a neighbor
	      for(k = 0; k < nnIndxLU[J+jj]; k++){ // these are the neighbors of the jjth location
	        kk = nnIndx[nnIndxLU[jj]+k]; // kk is the index for the jth locations neighbors
	        if(kk != i){ //if the neighbor of jj is not i
	  	b += B[nnIndxLU[jj]+k]*w[kk]; //covariance between jj and kk and the random effect of kk
	        }
	      }
	      aij = w[jj] - b;
	      a += B[nnIndxLU[jj]+uiIndx[uIndxLU[i]+j]]*aij/F[jj];
	      v += pow(B[nnIndxLU[jj]+uiIndx[uIndxLU[i]+j]],2)/F[jj];
	    }
	  }
	  
	  e = 0;
	  for(j = 0; j < nnIndxLU[J+i]; j++){
	    e += B[nnIndxLU[i]+j]*w[nnIndx[nnIndxLU[i]+j]];
	  }
	  
	  mu = (kappaOcc[i] / omegaOcc[i] - F77_NAME(ddot)(&pOcc, &X[i], &J, beta, &inc))*omegaOcc[i] + e/F[i] + a;
	  
	  var = 1.0/(omegaOcc[i] + 1.0/F[i] + v);
	  
	  w[i] = rnorm(mu*var, sqrt(var));

        } // i 

        /********************************************************************
         *Update sigmaSq
         *******************************************************************/
	if (!fixedSigmaSq) {
          if (sigmaSqIG) {
#ifdef _OPENMP
#pragma omp parallel for private (e, i, b) reduction(+:a, logDet)
#endif
            for (j = 0; j < J; j++){
              if(nnIndxLU[J+j] > 0){
                e = 0;
                for(i = 0; i < nnIndxLU[J+j]; i++){
                  e += B[nnIndxLU[j]+i]*w[nnIndx[nnIndxLU[j]+i]];
                }
                b = w[j] - e;
              }else{
                b = w[j];
              }	
              a += b*b/F[j];
            }

	    theta[sigmaSqIndx] = rigamma(sigmaSqA + J / 2.0, sigmaSqB + 0.5 * a * theta[sigmaSqIndx]); 
	  }
	}

        /********************************************************************
         *Update phi (and nu if matern)
         *******************************************************************/
        // Current
        if (corName == "matern"){ nu = theta[nuIndx]; }
        updateBF1Int(B, F, c, C, coords, nnIndx, nnIndxLU, J, m, theta[sigmaSqIndx], theta[phiIndx], nu, covModel, bk, nuB);
        
        a = 0;
        logDet = 0;

#ifdef _OPENMP
#pragma omp parallel for private (e, i, b) reduction(+:a, logDet)
#endif
        for (j = 0; j < J; j++){
          if (nnIndxLU[J+j] > 0){
            e = 0;
            for (i = 0; i < nnIndxLU[J+j]; i++){
              e += B[nnIndxLU[j]+i]*w[nnIndx[nnIndxLU[j]+i]];
            }
            b = w[j] - e;
          } else{
            b = w[j];
          }	
          a += b*b/F[j];
          logDet += log(F[j]);
        }
      
        logPostCurrent = -0.5*logDet - 0.5*a;
        logPostCurrent += log(theta[phiIndx] - phiA) + log(phiB - theta[phiIndx]); 
        if(corName == "matern"){
        	logPostCurrent += log(theta[nuIndx] - nuA) + log(nuB - theta[nuIndx]); 
        }
	if (sigmaSqIG == 0) {
          logPostCurrent += log(theta[sigmaSqIndx] - sigmaSqA) + log(sigmaSqB - theta[sigmaSqIndx]);
	}
        
        // Candidate
        phiCand = logitInv(rnorm(logit(theta[phiIndx], phiA, phiB), exp(tuning[phiIndx])), phiA, phiB);
        if (corName == "matern"){
      	  nuCand = logitInv(rnorm(logit(theta[nuIndx], nuA, nuB), exp(tuning[nuIndx])), nuA, nuB);
        }
	if (sigmaSqIG == 0) {
	  sigmaSqCand = logitInv(rnorm(logit(theta[sigmaSqIndx], sigmaSqA, sigmaSqB), 
				 exp(tuning[sigmaSqIndx])), sigmaSqA, sigmaSqB); 
	}

	if (sigmaSqIG) { 
        updateBF1Int(BCand, FCand, c, C, coords, nnIndx, nnIndxLU, J, m, theta[sigmaSqIndx], phiCand, nuCand, covModel, bk, nuB);
	} else {
          updateBF1Int(BCand, FCand, c, C, coords, nnIndx, nnIndxLU, J, m, sigmaSqCand, phiCand, nuCand, covModel, bk, nuB);
	}
      
        a = 0;
        logDet = 0;
      
#ifdef _OPENMP
#pragma omp parallel for private (e, i, b) reduction(+:a, logDet)
#endif
        for (j = 0; j < J; j++){
          if (nnIndxLU[J+j] > 0){
            e = 0;
            for (i = 0; i < nnIndxLU[J+j]; i++){
              e += BCand[nnIndxLU[j]+i]*w[nnIndx[nnIndxLU[j]+i]];
            }
            b = w[j] - e;
          } else{
            b = w[j];
            }	
            a += b*b/FCand[j];
            logDet += log(FCand[j]);
        }
        
        logPostCand = -0.5*logDet - 0.5*a;      
        logPostCand += log(phiCand - phiA) + log(phiB - phiCand); 
        if (corName == "matern"){
          logPostCand += log(nuCand - nuA) + log(nuB - nuCand); 
        }
	if (sigmaSqIG == 0) {
          logPostCand += log(sigmaSqCand - sigmaSqA) + log(sigmaSqB - sigmaSqCand);
	}

        if (runif(0.0,1.0) <= exp(logPostCand - logPostCurrent)) {

          std::swap(BCand, B);
          std::swap(FCand, F);
          
          theta[phiIndx] = phiCand;
          accept[phiIndx]++;
          if(corName == "matern"){
            theta[nuIndx] = nuCand; 
            accept[nuIndx]++; 
          }
	  if (sigmaSqIG == 0) {
            theta[sigmaSqIndx] = sigmaSqCand;
	    accept[sigmaSqIndx]++;
	  }
        }

        /********************************************************************
         *Update Latent Occupancy
         *******************************************************************/
        // Compute detection probability 
        for (i = 0; i < nObs; i++) {
          stAlpha = which(dataIndx[i], alphaIndx, pDet); 
          detProb[i] = logitInv(F77_NAME(ddot)(&pDetLong[dataIndx[i]], &Xp[i], &nObs, &alpha[stAlpha], &inc), zero, one);
          if (tmp_J[zLongIndx[i]] == 0) {
            psi[zLongIndx[i]] = logitInv(F77_NAME(ddot)(&pOcc, &X[zLongIndx[i]], &J, beta, &inc) + w[zLongIndx[i]], zero, one); 
          }
          piProd[zLongIndx[i]] *= (1.0 - detProb[i]);
          ySum[zLongIndx[i]] += y[i]; 	
          tmp_J[zLongIndx[i]]++;
        } // i
        // Compute occupancy probability 
        for (j = 0; j < J; j++) {
          psiNum = psi[j] * piProd[j]; 
          if (ySum[j] == zero) {
            z[j] = rbinom(one, psiNum / (psiNum + (1.0 - psi[j])));           
          } else {
            z[j] = one; 
          }
          piProd[j] = one;
          ySum[j] = zero; 
          tmp_J[j] = 0; 
        } // j

        /********************************************************************
         *Save samples
         *******************************************************************/
	if (t >= nBurn) {
          thinIndx++; 
	  if (thinIndx == nThin) {
            F77_NAME(dcopy)(&pOcc, beta, &inc, &REAL(betaSamples_r)[sPost*pOcc], &inc);
            F77_NAME(dcopy)(&pDet, alpha, &inc, &REAL(alphaSamples_r)[sPost*pDet], &inc);
            F77_NAME(dcopy)(&J, psi, &inc, &REAL(psiSamples_r)[sPost*J], &inc); 
            F77_NAME(dcopy)(&J, w, &inc, &REAL(wSamples_r)[sPost*J], &inc); 
	    F77_NAME(dcopy)(&nTheta, theta, &inc, &REAL(thetaSamples_r)[sPost*nTheta], &inc); 
	    F77_NAME(dcopy)(&J, z, &inc, &REAL(zSamples_r)[sPost*J], &inc); 
	    sPost++; 
	    thinIndx = 0; 
	  }
	}

        R_CheckUserInterrupt();
      } // end batch

      /********************************************************************
       *Adjust tuning 
       *******************************************************************/
      for (j = 0; j < nTheta; j++) {
        REAL(acceptSamples_r)[s * nTheta + j] = accept[j]/batchLength; 
        REAL(tuningSamples_r)[s * nTheta + j] = tuning[j]; 
        if (accept[j] / batchLength > acceptRate) {
          tuning[j] += std::min(0.01, 1.0/sqrt(static_cast<double>(s)));
        } else{
            tuning[j] -= std::min(0.01, 1.0/sqrt(static_cast<double>(s)));
          }
        accept[j] = 0;
      }
      /********************************************************************
       *Report 
       *******************************************************************/
      if (verbose) {
	if (status == nReport) {
	  Rprintf("Batch: %i of %i, %3.2f%%\n", s, nBatch, 100.0*s/nBatch);
	  Rprintf("\tParameter\tAcceptance\tTuning\n");	  
	  Rprintf("\tphi\t\t%3.1f\t\t%1.5f\n", 100.0*REAL(acceptSamples_r)[s * nTheta + phiIndx], exp(tuning[phiIndx]));
	  if (corName == "matern") {
	    Rprintf("\tnu\t\t%3.1f\t\t%1.5f\n", 100.0*REAL(acceptSamples_r)[s * nTheta + nuIndx], exp(tuning[nuIndx]));
	  }
	  if (sigmaSqIG == 0) {
	    Rprintf("\tsigmaSq\t\t%3.1f\t\t%1.5f\n", 100.0*REAL(acceptSamples_r)[s * nTheta + sigmaSqIndx], exp(tuning[sigmaSqIndx]));
	  }
	  Rprintf("-------------------------------------------------\n");
          #ifdef Win32
	  R_FlushConsole();
          #endif
	  status = 0;
	}
      }
      status++;        
    } // s (sample loop)
    if (verbose) {
      Rprintf("Batch: %i of %i, %3.2f%%\n", s, nBatch, 100.0*s/nBatch);
    }

    // This is necessary when generating random numbers in C.     
    PutRNGstate();

    //make return object (which is a list)
    SEXP result_r, resultName_r;
    int nResultListObjs = 8;

    PROTECT(result_r = Rf_allocVector(VECSXP, nResultListObjs)); nProtect++;
    PROTECT(resultName_r = Rf_allocVector(VECSXP, nResultListObjs)); nProtect++;

    // Setting the components of the output list.
    SET_VECTOR_ELT(result_r, 0, betaSamples_r);
    SET_VECTOR_ELT(result_r, 1, alphaSamples_r);
    SET_VECTOR_ELT(result_r, 2, zSamples_r); 
    SET_VECTOR_ELT(result_r, 3, psiSamples_r);
    SET_VECTOR_ELT(result_r, 4, thetaSamples_r); 
    SET_VECTOR_ELT(result_r, 5, wSamples_r); 
    SET_VECTOR_ELT(result_r, 6, tuningSamples_r); 
    SET_VECTOR_ELT(result_r, 7, acceptSamples_r); 
    // Rf_mkChar turns a C string into a CHARSXP
    SET_VECTOR_ELT(resultName_r, 0, Rf_mkChar("beta.samples")); 
    SET_VECTOR_ELT(resultName_r, 1, Rf_mkChar("alpha.samples")); 
    SET_VECTOR_ELT(resultName_r, 2, Rf_mkChar("z.samples")); 
    SET_VECTOR_ELT(resultName_r, 3, Rf_mkChar("psi.samples"));
    SET_VECTOR_ELT(resultName_r, 4, Rf_mkChar("theta.samples")); 
    SET_VECTOR_ELT(resultName_r, 5, Rf_mkChar("w.samples")); 
    SET_VECTOR_ELT(resultName_r, 6, Rf_mkChar("phi.tune")); 
    SET_VECTOR_ELT(resultName_r, 7, Rf_mkChar("phi.accept")); 
   
    // Set the names of the output list.  
    Rf_namesgets(result_r, resultName_r);
    
    //unprotect
    UNPROTECT(nProtect);
    
    return(result_r);
  }
}

