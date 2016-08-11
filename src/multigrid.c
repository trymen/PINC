/**
 * @file		multigrid.c
 * @author		Gullik Vetvik Killie <gullikvk@student.matnat.uio.no>
 * @copyright	University of Oslo, Norway
 * @brief		Poisson Solver, multigrid.
 * @date		26.10.15
 *
 *
 * Functions dealing with the initialisation and destruction of multigrid structures and
 * a multigrid solver containing restriction, prolongation operatorors and smoothers
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include "core.h"
#include "multigrid.h"

/******************************************************************************
 * 				Local functions
 *****************************************************************************/

void mgSetSolver(const dictionary *ini, Multigrid *multigrid){

	char *preSmoothName = iniGetStr(ini, "multigrid:preSmooth");
    char *postSmoothName = iniGetStr(ini, "multigrid:postSmooth");
    char *coarseSolverName = iniGetStr(ini, "multigrid:coarseSolv");

	int nDims = multigrid->grids[0]->rank-1;

	if(!strcmp(preSmoothName,"mgGS")){
		if(nDims == 2)	multigrid->preSmooth = &mgGS2D;
		else if(nDims == 3) multigrid->preSmooth = &mgGS3D;
		else msg(ERROR, "No Presmoothing algorithm set for dimensions %d", nDims);
    } else if (!strcmp(preSmoothName, "mgJacob")){
		if(nDims == 2) multigrid->preSmooth = &mgJacob2D;
		else if(nDims == 3) multigrid->preSmooth = &mgJacob3D;
		else msg(ERROR, "No Presmoothing algorithm set for dimensions %d", nDims);
 	} else {
    	msg(ERROR, "No Presmoothing algorithm specified");
    }

    if(!strcmp(postSmoothName,"mgGS")){
		if(nDims == 2)	multigrid->postSmooth = &mgGS2D;
		else if(nDims == 3) multigrid->postSmooth = &mgGS3D;
		else msg(ERROR, "No postsmoothing algorithm set for dimensions %d", nDims);
	} else if (!strcmp(postSmoothName, "mgJacob")){
		if(nDims == 2) multigrid->postSmooth = &mgJacob2D;
		else if(nDims == 3) multigrid->postSmooth = &mgJacob3D;
	} else {
    	msg(ERROR, "No Postsmoothing algorithm specified");
    }

    if(!strcmp(coarseSolverName,"mgGS")){
		if(nDims == 2)	multigrid->coarseSolv = &mgGS2D;
		else if(nDims == 3) multigrid->coarseSolv = &mgGS3D;
		else msg(ERROR, "No coarsesolver algorithm set for dimensions %d", nDims);
	} else if (!strcmp(coarseSolverName, "mgJacob")){
		if(nDims == 2) multigrid->coarseSolv = &mgJacob2D;
		else if(nDims == 3) multigrid->coarseSolv = &mgJacob3D;
 	} else {
    	msg(ERROR, "No coarse Grid Solver algorithm specified");
    }

    free(preSmoothName);
    free(postSmoothName);
    free(coarseSolverName);
}

void mgsetRestrictProlong(const dictionary *ini,Multigrid *multigrid){
	char *restrictor = iniGetStr(ini, "multigrid:restrictor");
	char *prolongator = iniGetStr(ini, "multigrid:prolongator");

	int rank = multigrid->grids[0]->rank;

	if(!strcmp(restrictor, "halfWeight")){
		if(rank == 3)	multigrid->restrictor = &mgHalfRestrict2D;
		else if(rank == 4) multigrid->restrictor = &mgHalfRestrict3D;
		else msg(ERROR, "No restricting algorithm for D%d", rank-1);
	} else {
		msg(ERROR, "No restrict stencil specified");
	}
	if(!strcmp(prolongator, "bilinear")){
		if(rank == 3)	multigrid->prolongator = &mgBilinProl2D;
		else if(rank==4)	multigrid->prolongator = &mgBilinProl3D;
		else msg(ERROR, "No restricting algorithm for D%d", rank-1);
	} else {
		msg(ERROR, "No prolongation stencil specified");
	}

	free(restrictor);
	free(prolongator);
}

MgAlgo getMgAlgo(const dictionary *ini){

	char *mgAlgo = iniGetStr(ini, "multigrid:cycle");

	MgAlgo algorithm;

	if(!strcmp(mgAlgo, "mgVRegular"))	algorithm = &mgVRegular;
	if(!strcmp(mgAlgo, "mgVRecursive"))	algorithm = &mgVRecursive;
	if(!strcmp(mgAlgo, "mgFMG"))		algorithm = &mgFMG;
	if(!strcmp(mgAlgo, "mgW"))			algorithm = &mgW;

	return algorithm;
}


Grid **mgAllocSubGrids(const dictionary *ini, Grid *grid,
						const int nLevels){

	//Gather information on finest grid
	Grid **grids = malloc((nLevels) * sizeof(Grid));

	int *trueSize = grid->trueSize;
	int *nGhostLayers = grid->nGhostLayers;
	double *stepSize = grid->stepSize;
	bndType *bnd = grid->bnd;
	int rank = grid->rank;

	//Set first grid to point to f grid
	grids[0] = grid;

	//Cycle through subgrids
	for(int q = 1; q < nLevels; q++){
		//Allocate
		int *subTrueSize = malloc(rank*sizeof(subTrueSize));
		int *subSize = malloc(rank *sizeof(*subSize));

		//Set first entries (nValues)
		subTrueSize[0] = trueSize[0];
		subSize[0] = subTrueSize[0];

		//The subgrid needs half the grid points
		for(int d = 1; d < rank; d++)	subTrueSize[d] = trueSize[d]/(pow(2,q));

		// Calculate the number of grid points (True points + ghost points)
		subSize[0] = trueSize[0];
		for(int d = 1 ; d < rank ; d ++){
			subSize[d] = subTrueSize[d] + nGhostLayers[d] + nGhostLayers[rank + d];
		}
		//Slice elements
		long int nSliceMax = 0;
		for(int d=0;d<rank;d++){
			long int nSlice = 1;
			for(int dd=0;dd<rank;dd++){
				if(dd!=d) nSlice *= subSize[dd];
			}
			if(nSlice>nSliceMax) nSliceMax = nSlice;
		}

		long int *subSizeProd = malloc((rank+1)*sizeof(*subSizeProd));
		ailCumProd(subSize, subSizeProd, rank);

		//Alloc slice and val
		double *val = malloc(subSizeProd[rank]*sizeof(*val));
		double *sendSlice = malloc(nSliceMax*sizeof(*sendSlice));
		double *recvSlice = malloc(nSliceMax*sizeof(*recvSlice));
		double *bndSlice = malloc(2*rank*nSliceMax*sizeof(*bndSlice));

		//Ghost layer vector
		int *subNGhostLayers = malloc(rank*2*sizeof(*subNGhostLayers));
		for(int d = 0; d < 2*rank; d++)	subNGhostLayers[d] = nGhostLayers[d];

		//Copying boundaries and stepSize
		bndType *subBnd = malloc(rank*2*sizeof(*subBnd));
		for(int d = 0; d < 2*rank; d++)	subBnd[d] = bnd[d];



		double *subStepSize = malloc(rank*sizeof(*subStepSize));
		for(int d = 0; d < rank; d++)	subStepSize[d] = stepSize[d];


		//Assign to grid
		Grid *grid = malloc(sizeof(Grid));
		grid->val = val;
		grid->rank = rank;
		grid->size = subSize;
		grid->trueSize = subTrueSize;
		grid->sizeProd = subSizeProd;
		grid->nGhostLayers = subNGhostLayers;
		grid->stepSize = subStepSize;

		grid->sendSlice = sendSlice;
		grid->recvSlice = recvSlice;
		grid->bndSlice = bndSlice;
		grid->h5 = 0;
		grid->bnd = subBnd;

		grids[q] = grid;
	}

	return grids;
}

/*************************************************
 *		Inline functions
 ************************************************/

 inline static void loopRedBlack2D(double *rhoVal,double *phiVal,long int *sizeProd, int *trueSize, int kEdgeInc,
 				long int g){

 	long int gj = g + sizeProd[1];
 	long int gjj= g - sizeProd[1];
 	long int gk = g + sizeProd[2];
 	long int gkk= g - sizeProd[2];

 	for(int k = 1; k < trueSize[2]; k +=2){
 		for(int j = 1; j < trueSize[1]; j += 2){
 			phiVal[g] = 0.25*(	phiVal[gj] + phiVal[gjj] +
 								phiVal[gk] + phiVal[gkk] + rhoVal[g]);
 			g	+=2;
 			gj	+=2;
 			gjj	+=2;
 			gk	+=2;
 			gkk	+=2;
 		}
 		g	+=kEdgeInc;
 		gj	+=kEdgeInc;
 		gjj	+=kEdgeInc;
 		gk	+=kEdgeInc;
 		gkk	+=kEdgeInc;
 	}

 	return;
 }

 inline static void loopRedBlack3D(double *rhoVal,double *phiVal,long int *sizeProd, int *trueSize, int kEdgeInc, int lEdgeInc,
 				long int g){

 	long int gj = g + sizeProd[1];
 	long int gjj= g - sizeProd[1];
 	long int gk = g + sizeProd[2];
 	long int gkk= g - sizeProd[2];
 	long int gl = g + sizeProd[3];
 	long int gll= g - sizeProd[3];

 	for(int l = 0; l<trueSize[3]; l+=2){
 		for(int k = 0; k < trueSize[2]; k+=2){
 			for(int j = 0; j < trueSize[1]; j+=2){
 				// msg(STATUS, "g=%d", g);
 				phiVal[g] = 0.125*(phiVal[gj] + phiVal[gjj] +
 								phiVal[gk] + phiVal[gkk] +
 								phiVal[gl] + phiVal[gll] + rhoVal[g]);
 				g	+=2;
 				gj	+=2;
 				gjj	+=2;
 				gk	+=2;
 				gkk	+=2;
 				gl	+=2;
 				gll	+=2;
 			}
 		g	+=kEdgeInc;
 		gj	+=kEdgeInc;
 		gjj	+=kEdgeInc;
 		gk	+=kEdgeInc;
 		gkk	+=kEdgeInc;
 		gl	+=kEdgeInc;
 		gll	+=kEdgeInc;
 		}
 	g	+=lEdgeInc;
 	gj	+=lEdgeInc;
 	gjj	+=lEdgeInc;
 	gk	+=lEdgeInc;
 	gkk	+=lEdgeInc;
 	gl	+=lEdgeInc;
 	gll	+=lEdgeInc;
 	}

 	return;
 }



/*************************************************
 *		DEFINITIONS
 ************************************************/
/*************************************************
 * 		ALLOCATIONS
 * 		DESTRUCTORS
 ************************************************/


Multigrid *mgAlloc(const dictionary *ini, Grid *grid){

	//Multigrid
	int nLevels = iniGetInt(ini, "multigrid:mgLevels");
	int nMGCycles = iniGetInt(ini, "multigrid:mgCycles");
	int nPreSmooth = iniGetInt(ini, "multigrid:nPreSmooth");
	int nPostSmooth = iniGetInt(ini, "multigrid:nPostSmooth");
	int nCoarseSolve = iniGetInt(ini, "multigrid:nCoarseSolve");
	//Load data
	int nDims = grid->rank-1;
	int *trueSize = grid->trueSize;


	//Sanity checks
	if(nLevels<1) msg(ERROR, "Multi Grid levels is 0, need 1 grid level \n");
	if(nLevels==1) msg(WARNING|ONCE, "Multi Grid levels is 1, using Gauss-Seidel Red'Black \n");

	if(!nMGCycles) msg(ERROR, "MG cycles is 0 \n");


	// Sanity check (true grid points need to be a multiple of 2^(multigrid levels)
	for(int d = 0; d < nDims; d++){
		if(trueSize[d+1] % (int) 2*nLevels){ //Sloppy and wrong
			msg(ERROR, "The number of True Grid Points needs to be a multiple of 2^nLevels");
		}
	}

	Grid **grids = mgAllocSubGrids(ini, grid, nLevels);

	//Store in multigrid struct
    Multigrid *multigrid = malloc(sizeof(Multigrid));

    multigrid->nLevels = nLevels;
    multigrid->nMGCycles = nMGCycles;
	multigrid->nPreSmooth = nPreSmooth;
	multigrid->nPostSmooth = nPostSmooth;
	multigrid->nCoarseSolve = nCoarseSolve;
    multigrid->grids = grids;

    //Setting the algorithms to be used, pointer functions
	mgSetSolver(ini, multigrid);
	mgsetRestrictProlong(ini, multigrid);

  	return multigrid;

}

void mgFree(Multigrid *multigrid){

	Grid **grids = multigrid->grids;
	int nLevels = multigrid->nLevels;

	for(int n = 1; n < nLevels; n++){
		gFree(grids[n]);
	}
	free(multigrid);

	return;
}

/******************************************************
 *		Iterative Solvers
 *****************************************************/


void mgJacob2D(Grid *phi,const Grid *rho, const int nCycles, const  MpiInfo *mpiInfo){
	//Warning not optimzed

	// msg(STATUS, "Hello");
	//Common variables
	int rank = phi->rank;
	long int *sizeProd = phi->sizeProd;

	//Seperate values
	double *phiVal = phi->val;
	double *rhoVal = rho->val;

	//Temporary value
	double *tempVal = malloc (sizeProd[rank]*sizeof(*tempVal));

	for(int c = 0; c < nCycles; c++){
		// Index of neighboring nodes
		int gj = sizeProd[1];
		int gjj= -sizeProd[1];
		int gk = sizeProd[2];
		int gkk= -sizeProd[2];

		for(long int g = 0; g < sizeProd[rank]; g++){
			tempVal[g] = 0.25*(	phiVal[gj] + phiVal[gjj] +
								phiVal[gk] + phiVal[gkk] - rhoVal[g]);

			gj++;
			gjj++;
			gk++;
			gkk++;
		}

		for(long int q = 0; q < sizeProd[rank]; q++) phiVal[q] = tempVal[q];

		gHaloOp(setSlice, phi, mpiInfo, 0);

	}

	return;
}

void mgJacob3D(Grid *phi,const Grid *rho, const int nCycles, const  MpiInfo *mpiInfo){

	// msg(STATUS, "Hello");

	//Common variables
	int rank = phi->rank;
	long int *sizeProd = phi->sizeProd;

	//Seperate values
	double *phiVal = phi->val;
	double *rhoVal = rho->val;

	//Temporary value
	double *tempVal = malloc (sizeProd[rank]*sizeof(*tempVal));
	double coeff = 1./6;

	for(int c = 0; c < nCycles; c++){
		// Index of neighboring nodes
		long int g =  sizeProd[1] + sizeProd[2] + sizeProd[3];

		long int gj = sizeProd[1];
		long int gjj= -sizeProd[1];
		long int gk = sizeProd[2];
		long int gkk= -sizeProd[2];
		long int gl = sizeProd[3];
		long int gll= -sizeProd[3];

		long int end = sizeProd[rank] - 2*g;

		for(long int q = 0; q < end; q++){
			tempVal[g] = coeff*(phiVal[gj] + phiVal[gjj] +
								phiVal[gk] + phiVal[gkk] +
								phiVal[gl] + phiVal[gll] +
								- rhoVal[g]);

			gj++;
			gjj++;
			gk++;
			gkk++;
			gl++;
			gll++;
		}

		for(long int q = 0; q < sizeProd[rank]; q++) phiVal[q] = tempVal[q];

		gHaloOp(setSlice, phi, mpiInfo, 0);

	}

	return;
}



void mgGS2D(Grid *phi, const Grid *rho, int nCycles, const MpiInfo *mpiInfo){

	//Common variables
	int *trueSize = phi->trueSize;
	int *nGhostLayers = phi->nGhostLayers;
	long int *sizeProd = phi->sizeProd;
	int rank = phi->rank;

	//Seperate values
	double *phiVal = phi->val;
	double *rhoVal = rho->val;

	//Indexes
	long int g;

	for(int c = 0; c < nCycles;c++){

		//Increments
		int kEdgeInc = nGhostLayers[2] + nGhostLayers[rank + 2] + sizeProd[2];

		/**************************
		 *	Red Pass
		 *************************/
		//Odd numbered rows
		g = nGhostLayers[1] + sizeProd[2];
		loopRedBlack2D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, g);

		//Even numbered columns
		g = nGhostLayers[1] + 1 + 2*sizeProd[2];
		loopRedBlack2D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, g);

		gHaloOp(setSlice, phi, mpiInfo, 0);
		// gBnd(rho,mpiInfo);
		// gBnd(phi,mpiInfo);


		/***********************************
		 *	Black pass
		 **********************************/
		//Odd numbered rows
		g = nGhostLayers[1] + 1 + sizeProd[2];
		loopRedBlack2D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, g);

		//Even numbered columns
		g = nGhostLayers[1] + 2*sizeProd[2];
		loopRedBlack2D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, g);


		gHaloOp(setSlice, phi, mpiInfo, 0);
		// gBnd(rho,mpiInfo);
		// gBnd(phi,mpiInfo);

	}


	return;
}


void mgGS3D(Grid *phi, const Grid *rho, int nCycles, const MpiInfo *mpiInfo){

	//Common variables
	int *trueSize = phi->trueSize;
	int *size = phi->size;
	int *nGhostLayers = phi->nGhostLayers;
	long int *sizeProd = phi->sizeProd;

	//Seperate values
	double *phiVal = phi->val;
	double *rhoVal = rho->val;

	//Indexes
	long int g;
	int gj = sizeProd[1];
	int gk = sizeProd[2];
	int gl = sizeProd[3];

	double coeff = 1./6.;

	for(int c = 0; c < nCycles; c++){


		/*********************
		 *	Red Pass
		 ********************/
		g = sizeProd[3]*nGhostLayers[3];
		for(int l = 0; l < trueSize[3];l++){
			for(int k = 0; k < size[2]; k++){
				for(int j = 0; j < size[1]; j+=2){
					phiVal[g] = coeff*(	phiVal[g+gj] + phiVal[g-gj] +
										phiVal[g+gk] + phiVal[g-gk] +
										phiVal[g+gl] + phiVal[g-gl] - rhoVal[g]);
					g	+=2;
				}

				// g+=(-1 + 2*(k%2))*(-1 + 2*(l%2));

				if(l%2){
					if(k%2)	g+=1; else g-=1;
				} else {
					if(k%2) g-=1; else g+=1;
				}

			}
			if(l%2) g-=1; else g+=1;
			// g -= -1 + 2*(l%2);
		}

		gHaloOp(setSlice, phi, mpiInfo, 0);
		gBnd(phi, mpiInfo);

		/*********************
		 *	Black pass
		 ********************/
		 g = sizeProd[1] + sizeProd[3]*nGhostLayers[3];
		 for(int l = 0; l < trueSize[3];l++){
		 	for(int k = 0; k < size[2]; k++){
		 		for(int j = 0; j < size[1]; j+=2){
		 			phiVal[g] = coeff*(	phiVal[g+gj] + phiVal[g-gj] +
		 								phiVal[g+gk] + phiVal[g-gk] +
		 								phiVal[g+gl] + phiVal[g-gl] - rhoVal[g]);

		 			g	+=2;
		 		}
					if(l%2){
						if(k%2)	g-=1; else g+=1;
					} else {
						if(k%2) g+=1; else g-=1;
					}
				// g+=(-1 + 2*(k%2))*(-1 + 2*(l%2));


		 	}
				if(l%2) g+=1; else g-=1;
			// g -= -1 + 2*(l%2);
		 }

		gHaloOp(setSlice, phi, mpiInfo, 0);
		gBnd(phi, mpiInfo);
	}


	return;
}



void mgGS3DNew(Grid *phi, const Grid *rho, int nCycles, const MpiInfo *mpiInfo){

	//Common variables
	int *trueSize = phi->trueSize;
	int *nGhostLayers = phi->nGhostLayers;
	long int *sizeProd = phi->sizeProd;
	int rank = phi->rank;

	//Seperate values
	double *phiVal = phi->val;
	double *rhoVal = rho->val;

	//Indexes
	long int g;

	int kEdgeInc = (nGhostLayers[1] + nGhostLayers[rank+1]) + sizeProd[2];
	int lEdgeInc = (nGhostLayers[2] + nGhostLayers[rank+2])*sizeProd[2] + sizeProd[3];


	for(int c = 0; c < nCycles;c++){

		/**************************
		 *	Red Pass
		 *************************/
		//Odd layers - Odd Rows
		g = nGhostLayers[1]*sizeProd[1] + nGhostLayers[2]*sizeProd[2] + nGhostLayers[3]*sizeProd[3];
		loopRedBlack3D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, lEdgeInc, g);

		//Odd layers - Even Rows
		g = (nGhostLayers[1]+1)*sizeProd[1] + (nGhostLayers[2]+1)*sizeProd[2] + nGhostLayers[3]*sizeProd[3];
		loopRedBlack3D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, lEdgeInc,	g);

		//Even layers - Odd Rows
		g = (nGhostLayers[1])*sizeProd[1] + (nGhostLayers[2])*sizeProd[2] + (nGhostLayers[3]+1)*sizeProd[3];
		loopRedBlack3D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, lEdgeInc,	g);

		//Even layers - Even Rows
		g = (nGhostLayers[1] + 1)*sizeProd[1] + (nGhostLayers[2]+1)*sizeProd[2] + (nGhostLayers[3]+1)*sizeProd[3];
		loopRedBlack3D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, lEdgeInc,	g);

		gHaloOp(setSlice, phi, mpiInfo, 0);

		/***********************************
		 *	Black pass
		 **********************************/
		 //Odd layers - Odd Rows
 		g = (nGhostLayers[1]*sizeProd[1]+1) + nGhostLayers[2]*sizeProd[2] + nGhostLayers[3]*sizeProd[3];
 		loopRedBlack3D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, lEdgeInc,	g);

 		//Odd layers - Even Rows
 		g = (nGhostLayers[1])*sizeProd[1] + (nGhostLayers[2]+1)*sizeProd[2] + nGhostLayers[3]*sizeProd[3];
 		loopRedBlack3D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, lEdgeInc,	g);

 		//Even layers - Odd Rows
 		g = (nGhostLayers[1])*sizeProd[1] + (nGhostLayers[2])*sizeProd[2] + (nGhostLayers[3]+1)*sizeProd[3];
 		loopRedBlack3D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, lEdgeInc,	g);

		//Even layers - Even Rows
 		g = (nGhostLayers[1]+1)*sizeProd[1] + (nGhostLayers[2]+1)*sizeProd[2] + (nGhostLayers[3]+1)*sizeProd[3];
 		loopRedBlack3D(rhoVal, phiVal, sizeProd, trueSize, kEdgeInc, lEdgeInc,	g);

		gHaloOp(setSlice, phi, mpiInfo, 0);
	}


	return;
}

/***********************************************************
 *			RESTRICTORS/PROLONGATORS
 **********************************************************/

void mgHalfRestrict3D(const Grid *fine, Grid *coarse){

	//Load fine grid
	double *fVal = fine->val;
	long int *fSizeProd = fine->sizeProd;
	int rank = fine->rank;
	int *nGhostLayers = fine->nGhostLayers;

	//Load coarse grid
	double *cVal = coarse->val;
	long int *cSizeProd = coarse->sizeProd;
	int *cTrueSize = coarse->trueSize;


	//Indexes
	long int c = cSizeProd[1]*nGhostLayers[1] + cSizeProd[2]*nGhostLayers[2] + cSizeProd[3]*nGhostLayers[3];

	long int f = fSizeProd[1]*nGhostLayers[1] + fSizeProd[2]*nGhostLayers[2] + fSizeProd[3]*nGhostLayers[3];
	long int fj  = f + fSizeProd[1];
	long int fjj = f - fSizeProd[1];
	long int fk  = f + fSizeProd[2];
	long int fkk = f - fSizeProd[2];
	long int fl  = f + fSizeProd[3];
	long int fll = f - fSizeProd[3];

	int cKEdgeInc = nGhostLayers[2] + nGhostLayers[rank + 2];
	int fKEdgeInc = nGhostLayers[2] + nGhostLayers[rank + 2] + fSizeProd[2];
	int cLEdgeInc = (nGhostLayers[3] + nGhostLayers[rank + 3])*cSizeProd[2];
	int fLEdgeInc = (nGhostLayers[3] + nGhostLayers[rank + 3])*fSizeProd[2] + fSizeProd[3];

	double coeff = 1./12.;


	//Cycle Coarse grid
	for(int l = 0; l<cTrueSize[3]; l++){
		for(int k = 0; k < cTrueSize[2]; k++){
			for(int j = 0; j < cTrueSize[1]; j++){
				cVal[c] = coeff*(6*fVal[f] + fVal[fj] + fVal[fjj] + fVal[fk] + fVal[fkk] + fVal[fl] + fVal[fll]);
				c++;
				f  +=2;
				fj +=2;
				fjj+=2;
				fk +=2;
				fkk+=2;
				fl +=2;
				fll+=2;
			}
			c  += cKEdgeInc;
			f  += fKEdgeInc;
			fj += fKEdgeInc;
			fjj+= fKEdgeInc;
			fk += fKEdgeInc;
			fkk+= fKEdgeInc;
			fl += fKEdgeInc;
			fll+= fKEdgeInc;
		}
		c  += cLEdgeInc;
		f  += fLEdgeInc;
		fj += fLEdgeInc;
		fjj+= fLEdgeInc;
		fk += fLEdgeInc;
		fkk+= fLEdgeInc;
		fl += fLEdgeInc;
		fll+= fLEdgeInc;
	}

	return;
}

void mgHalfRestrict2D(const Grid *fine, Grid *coarse){

	//Load fine grid
	double *fVal = fine->val;
	long int *fSizeProd = fine->sizeProd;
	int rank = fine->rank;
	int *nGhostLayers = fine->nGhostLayers;

	//Load coarse grid
	double *cVal = coarse->val;
	long int *cSizeProd = coarse->sizeProd;
	int *cSize = coarse->size;

	//Indexes
	long int c = cSizeProd[2] + cSizeProd[1];

	long int f = fSizeProd[2] + fSizeProd[1];
	long int fj = f + fSizeProd[1];
	long int fjj = f - fSizeProd[1];
	long int fk = f + fSizeProd[2];
	long int fkk = f - fSizeProd[2];

	int cKEdgeInc = nGhostLayers[2] + nGhostLayers[rank + 2];
	int fKEdgeInc = cKEdgeInc + fSizeProd[2];

	//Cycle Coarse grid
	for(int k = nGhostLayers[2]; k < cSize[2]-nGhostLayers[rank + 2]; k++){
		for(int j = nGhostLayers[1]; j < cSize[1]-nGhostLayers[rank+1]; j++){
			// msg(STATUS, "c=%d, f = [%d] (%d %d %d %d)", c, f , fj, fjj, fk, fkk);
			cVal[c] = 0.125*(4*fVal[f] + fVal[fj] + fVal[fjj] + fVal[fk] + fVal[fkk]);
			c++;
			f  +=2;
			fj +=2;
			fjj+=2;
			fk +=2;
			fkk+=2;
		}
		c  += cKEdgeInc;
		f  += fKEdgeInc;
		fj += fKEdgeInc;
		fjj+= fKEdgeInc;
		fk += fKEdgeInc;
		fkk+= fKEdgeInc;
	}

	return;
}

void mgBilinProl3D(Grid *fine, const Grid *coarse,const  MpiInfo *mpiInfo){

	//Load fine grid
	double *fVal = fine->val;
	long int *fSizeProd = fine->sizeProd;
	int *fSize = fine->size;
	int *fTrueSize =fine->trueSize;
	int rank = fine->rank;
	int *nGhostLayers = fine->nGhostLayers;

	// aiPrint(fTrueSize, rank);

	//Load coarse grid
	double *cVal = coarse->val;
	long int *cSizeProd = coarse->sizeProd;
	int *cTrueSize = coarse->trueSize;

	//Help Indexes
	long int f = fSizeProd[1] + fSizeProd[2] + fSizeProd[3];
	long int c = cSizeProd[1] + cSizeProd[2] + cSizeProd[3];
	long int fNext;
	long int fPrev;

	//Edge jumps
	int cKEdgeInc = nGhostLayers[2] + nGhostLayers[rank + 2];
	int fKEdgeInc = cKEdgeInc + fSizeProd[2];
	int cLEdgeInc = (nGhostLayers[3] + nGhostLayers[rank + 3])*cSizeProd[2];
	int fLEdgeInc = (nGhostLayers[3] + nGhostLayers[rank + 3])*fSizeProd[2] + fSizeProd[3];

	//Direct insertion c->f
	for(int l = 0; l < cTrueSize[3]; l++){
		for(int k = 0; k < cTrueSize[2]; k++){
			for(int j = 0; j < cTrueSize[1]; j++){
				fVal[f] = cVal[c];
				c++;
				f+=2;
			}
			c+= cKEdgeInc;
			f+= fKEdgeInc;
		}
		c+= cLEdgeInc;
		f+= fLEdgeInc;
	}

	//Filling ghostlayer
	gHaloOpDim(setSlice, fine, mpiInfo, 3, 0);

	//Interpolation 3rd Dim
	f = fSizeProd[1] + fSizeProd[2] + 2*fSizeProd[3];
	fNext = f + fSizeProd[3];
	fPrev = f - fSizeProd[3];

	for(int l = 0; l < fTrueSize[3]; l+=2){
		for(int k = 0; k < fSize[2]; k+=2){
			for(int j = 0; j < fSize[1]; j+=2){
				fVal[f] = 0.5*(fVal[fPrev]+fVal[fNext]);
				f +=2;
				fNext +=2;
				fPrev +=2;
			}
			f		+=fSizeProd[2];
			fNext 	+=fSizeProd[2];
			fPrev 	+=fSizeProd[2];
		}
		f		+=fSizeProd[3];
		fNext 	+=fSizeProd[3];
		fPrev 	+=fSizeProd[3];
	}

	gHaloOpDim(setSlice, fine, mpiInfo, 2, 0);

	//Interpolation 2nd Dim
	f = fSizeProd[1] + 2*fSizeProd[2] + fSizeProd[3];
	fNext = f + fSizeProd[2];
	fPrev = f - fSizeProd[2];

	for(int l = 0; l < fTrueSize[3]; l++){
		for(int k = 0; k < fSize[2]; k+=2){
			for(int j = 0; j < fSize[1]; j+=2){
				fVal[f] = 0.5*(fVal[fPrev]+fVal[fNext]);
				f +=2;
				fNext +=2;
				fPrev +=2;
			}
			f		+=fSizeProd[2];
			fNext 	+=fSizeProd[2];
			fPrev 	+=fSizeProd[2];
		}
	}

	gHaloOpDim(setSlice, fine, mpiInfo, 1, 0);

	//Interpolation 2nd Dim
	f = 2*fSizeProd[1] + fSizeProd[2] + fSizeProd[3];
	fNext = f + fSizeProd[1];
	fPrev = f - fSizeProd[1];

	for(int l = 0; l < fTrueSize[3]; l++){
		for(int k = 0; k < fTrueSize[2]; k++){
			for(int j = 0; j < fSize[1]; j+=2){
				fVal[f] = 0.5*(fVal[fPrev]+fVal[fNext]);
				f +=2;
				fNext +=2;
				fPrev +=2;
			}
		}
		f		+=2*fSizeProd[2];
		fNext 	+=2*fSizeProd[2];
		fPrev 	+=2*fSizeProd[2];
	}


	return;
}


void mgBilinProl2D(Grid *fine, const Grid *coarse, const MpiInfo *mpiInfo){

	//Load fine grid
	double *fVal = fine->val;
	long int *fSizeProd = fine->sizeProd;
	int *fSize = fine->size;
	int rank = fine->rank;
	int *nGhostLayers = fine->nGhostLayers;

	//Load coarse grid
	double *cVal = coarse->val;
	long int *cSizeProd = coarse->sizeProd;
	int *cSize = coarse->size;

	//Help Indexes
	long int f = fSizeProd[2] + fSizeProd[1];
	long int c = cSizeProd[2] + cSizeProd[1];
	long int fNext;
	long int fPrev;

	int cKEdgeInc = nGhostLayers[2] + nGhostLayers[rank + 2];
	int fKEdgeInc = cKEdgeInc + fSizeProd[2];

	//Direct insertion c->f
	for(int k = nGhostLayers[2]; k < cSize[2]-nGhostLayers[rank + 2]; k++){
		for(int j = nGhostLayers[1]; j < cSize[1]-nGhostLayers[rank+1]; j++){
			fVal[f] = cVal[c];
			c++;
			f+=2;
		}
		c+= cKEdgeInc;
		f+= fKEdgeInc;
	}

	//Filling ghost cells
	gHaloOpDim(setSlice, fine, mpiInfo, 2, 0);

 	f= fSizeProd[1];
	fNext = f + fSizeProd[2];
	fPrev = f - fSizeProd[2];
	//Odd numbered columns, interpolating vertically
	for(int k = 0; k < fSize[2]; k+=2){
		for(int j = 0; j < fSize[1]; j+=2){
			fVal[f] += 0.5*(fVal[fPrev]+fVal[fNext]);
			f 		+= 2;
			fNext 	+= 2;
			fPrev 	+= 2;
		}
		f		+= fSizeProd[2];
		fNext 	+= fSizeProd[2];
		fPrev 	+= fSizeProd[2];
	}

	//Filling ghost cells
	gHaloOpDim(setSlice, fine, mpiInfo, 1, 0);

	//Even numbered columns, interpolating horizontally
	f = 0;
	fNext = f + fSizeProd[1];
	fPrev = f - fSizeProd[1];

	for(int k = 0; k < fSize[2]; k+=1){
		for(int j = 0; j < fSize[1]; j+=2){
			fVal[f] += 0.5*(fVal[fPrev]+fVal[fNext]);
			f 		+= 2;
			fNext 	+= 2;
			fPrev 	+= 2;
		}
	}

	return;
}

void mgRestrictBnd(Multigrid *mgGrid){

	int nLevels = mgGrid->nLevels;
	Grid **grid = mgGrid->grids;
	int rank = grid[0]->rank;

	//Set inside grid loop
	double *fineBnd;
	double *coarseBnd;
	int *fineSize;
	int *coarseSize;


	//Restrict down all grids
	for(int lvl = 0; lvl < nLevels-1; lvl++ ){
		//Setting size and
		fineSize = grid[lvl]->size;
		fineBnd = grid[lvl]->bndSlice;
		long int nFineSlice = 0;

		coarseBnd = grid[lvl+1]->bndSlice;
		coarseSize = grid[lvl+1]->size;
		long int nCoarseSlice = 0;

		//Number of elements in slice
		for(int d=0;d<rank;d++){
			long int nSlice = 1;
			for(int dd=0;dd<rank;dd++){
				if(dd!=d) nSlice *= fineSize[dd];
			}
			if(nSlice>nFineSlice) nFineSlice = nSlice;
		}


		for(int d=0;d<rank;d++){
			long int nSlice = 1;
			for(int dd=0;dd<rank;dd++){
				if(dd!=d) nSlice *= coarseSize[dd];
			}
			if(nSlice>nCoarseSlice) nCoarseSlice = nSlice;
		}


		// msg(STATUS, "Fineslice = %d", nFineSlice);
		// msg(STATUS, "CoarseSlice = %d", nCoarseSlice);

		/**************************************************
		 *		This is probably not correct for nonconstant
		 * 		boundaries
		 *************************************************/
		//Lower part
		for(int d = 1; d < rank; d++){
			for(int s = 0; s < nCoarseSlice; s++){
				coarseBnd[s + (nCoarseSlice * d)] = fineBnd[2*s + (nFineSlice*d)];
			}
		}

		//Upper part
		for(int d = rank+1; d < 2*rank; d++){
			for(int s = 0; s < nCoarseSlice; s++){
				coarseBnd[s + (nCoarseSlice * d)] = fineBnd[2*s + (nFineSlice*d)];
			}
		}


		//Restriction by injection
		// for(int s = 0; s < nSliceMax*2*rank; s++){
		// 	coarseBnd[s/2] = fineBnd[s];
		// }
		// adPrint(coarseBnd, 72*2*rank);

	}


}

/*******************************************************
 *			VARIOUS COMPUTATIONS (RESIDUAL)
 ******************************************************/

void mgResidual(Grid *res, const Grid *rho, const Grid *phi,const MpiInfo *mpiInfo){

	//Load
	long int *sizeProd = res->sizeProd;
	int rank = res->rank;
	double *resVal = res->val;
	double *rhoVal = rho->val;

	//Should consider changing to function pointers
	if(rank == 4){
		gFinDiff2nd3D(res, phi);
	} else if(rank == 3){
		gFinDiff2nd2D(res,phi);
	}

	for (long int g = 0; g < sizeProd[rank]; g++) resVal[g] -= rhoVal[g];

	return;
}


double mgResMass3D(Grid *grid, MpiInfo *mpiInfo){

	//Load MPI
	int mpiRank = mpiInfo->mpiRank;
	int mpiSize = mpiInfo->mpiSize;

	//Load
	int rank = grid->rank;
	int *size = grid->size;
	long int *sizeProd = grid->sizeProd;
	int *nGhostLayers = grid->nGhostLayers;
	double *val = grid->val;

	double mass = 0;
	double massRecv;

	//Cycle start and edge jumps
	long int g = nGhostLayers[1]*sizeProd[1] + nGhostLayers[2]*sizeProd[2] + nGhostLayers[3]*sizeProd[3];
	int kEdgeInc = (nGhostLayers[1]+nGhostLayers[1+rank])*sizeProd[1];
	int lEdgeInc = (nGhostLayers[2]+nGhostLayers[2+rank])*sizeProd[2];

	// int index = 0;

	//Cycle through true grid
	for(int l = nGhostLayers[3]; l < size[3]-nGhostLayers[rank+3]; l++){
		for(int k = nGhostLayers[2]; k < size[2]-nGhostLayers[rank+2]; k++){
			for(int j = nGhostLayers[1]; j < size[1]-nGhostLayers[rank+1]; j++){
				mass += abs(val[g]);//*val[g];
				// mass += val[g]*val[g];
				g++;
			}
			g+=kEdgeInc;
		}
		g+=lEdgeInc;
	}

	if(mpiRank != 0) MPI_Send(&mass, 1, MPI_DOUBLE, 0, mpiRank, MPI_COMM_WORLD);
	if(mpiRank == 0){
		for(int r = 1; r < mpiSize; r++){
			MPI_Recv(&massRecv, 1, MPI_DOUBLE, r, r, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			mass += massRecv;
		}
	}

	return mass;
}


void mgCompError(const Grid *numerical,const Grid *analytical, Grid *error){

	gCopy(numerical, error);
	gSubFrom(error, analytical);

	return;
}

double mgSumTrueSquared(Grid *error,const MpiInfo *mpiInfo){

	//Square and sum
	gSquare(error);
	double sum = gSumTruegrid(error);

	//Reduce
	MPI_Allreduce(&sum, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

	return sum;
}


void parseMGOptim(dictionary *ini, Multigrid *multigrid){


	return;
}



/*****************************************************
 *			MG CYCLES
 ****************************************************/

 void inline static mgVRecursiveInner(int level, int bottom, int top, Multigrid *mgRho, Multigrid *mgPhi,
  									Multigrid *mgRes, const MpiInfo *mpiInfo){


 	//Solve and return at coarsest level
 	if(level == bottom){
 		gHaloOp(setSlice, mgPhi->grids[level], mpiInfo, 0);
		gHaloOp(setSlice, mgRho->grids[level], mpiInfo, 0);
		gNeutralizeGrid(mgRho->grids[level], mpiInfo);
 		mgRho->coarseSolv(mgPhi->grids[level], mgRho->grids[level], mgRho->nCoarseSolve, mpiInfo);
		gNeutralizeGrid(mgPhi->grids[level], mpiInfo);
 		mgRho->prolongator(mgRes->grids[level-1], mgPhi->grids[level], mpiInfo);
		// msg(STATUS|ONCE, "Prolongating to lvl: %d", level-1);

 		return;
 	}



 	//Gathering info
 	int nPreSmooth = mgRho->nPreSmooth;
 	int nPostSmooth= mgRho->nPostSmooth;

 	Grid *phi = mgPhi->grids[level];
 	Grid *rho = mgRho->grids[level];
 	Grid *res = mgRes->grids[level];

 	//Boundary
 	gHaloOp(setSlice, rho, mpiInfo, 0);
	msg(STATUS|ONCE, "Bnd Starts");
 	gNeutralizeGrid(rho,mpiInfo);
	msg(STATUS|ONCE, "Bnd Fails?");

 	//Prepare to go down
	MPI_Barrier(MPI_COMM_WORLD);
	msg(STATUS, "Solving at lvl = %d", level);
 	mgRho->preSmooth(phi, rho, nPreSmooth, mpiInfo);
	MPI_Barrier(MPI_COMM_WORLD);


	msg(STATUS|ONCE, "Restricting from lvl %d -> %d", level, level+1);

 	mgResidual(res, rho, phi, mpiInfo);

 	gHaloOp(setSlice, res, mpiInfo, 0);

	// msg(STATUS|ONCE, "Restricting to lvl: %d", level+1);
 	//Go down
 	mgRho->restrictor(res, mgRho->grids[level + 1]);
 	mgVRecursiveInner(level + 1, bottom, top, mgRho, mgPhi, mgRes, mpiInfo);
	//
 	//Prepare to go up
 	gSubFrom( phi, res );
	// gAddTo(phi, res);
 	gHaloOp(setSlice, phi,mpiInfo, 0);
 	gBnd(phi,mpiInfo);
 	mgRho->postSmooth(phi, rho, nPostSmooth, mpiInfo);
	gNeutralizeGrid(phi, mpiInfo);

 	//Go up
 	if(level >top){
		// msg(STATUS|ONCE, "Prolongating to lvl: %d", level-1);
 		mgRho->prolongator(mgRes->grids[level-1], phi, mpiInfo);
 	}

 	return;
 }

 void mgVRecursive(int level, int bottom, int top, Multigrid *mgRho, Multigrid *mgPhi,
  					Multigrid *mgRes, const MpiInfo *mpiInfo){

 	mgVRecursiveInner(level, bottom, top, mgRho, mgPhi, mgRes, mpiInfo);

 	return;
 }


void mgVRegular(int level, int bottom, int top, Multigrid *mgRho, Multigrid *mgPhi,
 									Multigrid *mgRes, const MpiInfo *mpiInfo){

	msg(STATUS|ONCE, "Running mgV; start = %d, bottom = %d, top = %d",level, bottom, top);

	//Gathering info
	int nPreSmooth = mgRho->nPreSmooth;
	int nPostSmooth= mgRho->nPostSmooth;
	int nCoarseSolv= mgRho->nCoarseSolve;

	//Needed grids
	Grid *phi;
	Grid *rho;
	Grid *res;

	//Solvers
	void (*coarseSolv)(Grid *phi, const Grid *rho, const int nCycles,
		const MpiInfo *mpiInfo) = mgRho->coarseSolv;
	void (*postSmooth)(Grid *phi, const Grid *rho, const int nCycles,
		const MpiInfo *mpiInfo) = mgRho->postSmooth;
	void (*preSmooth)(Grid *phi, const Grid *rho, const int nCycles,
		const MpiInfo *mpiInfo) = mgRho->preSmooth;

	//Restriction/Prolongators
	void (*restrictor)(const Grid *fine, Grid *coarse) = mgRho->restrictor;
	void (*prolongator)(Grid *fine, const Grid *coarse,
		const MpiInfo *mpiInfo) = mgRho->prolongator;

	//Down to coarsest level
	for(int current = level; current < bottom; current ++){
		//Load grids
		phi = mgPhi->grids[current];
		rho = mgRho->grids[current];
		res = mgRes->grids[current];

		//Boundary
		gHaloOp(setSlice, phi, mpiInfo, 0);
		gBnd(phi, mpiInfo);
		gNeutralizeGrid(rho, mpiInfo);


		preSmooth(phi, rho, nPreSmooth, mpiInfo);

		gHaloOp(setSlice, rho, mpiInfo, 0);
		gBnd(phi, mpiInfo);

		gZero(res);
		mgResidual(res, rho, phi, mpiInfo);

		gHaloOp(setSlice, res, mpiInfo, 0);

		restrictor(res, mgRho->grids[current + 1]);
		msg(STATUS|ONCE, "Restricting from lvl %d -> %d", current, current+1);
	}

	rho = mgRho->grids[bottom];
	phi = mgPhi->grids[bottom];

	/*****************************************************
	 *	//OBS, ONLY NEEDED FOR PERIODIC (neutralize)
	 *****************************************************/
	gNeutralizeGrid(rho, mpiInfo);

	//Solve at coarsest
	gHaloOp(setSlice, rho, mpiInfo, 0);
	coarseSolv(phi, rho, nCoarseSolv, mpiInfo);

	//Send up
	gHaloOp(setSlice, phi, mpiInfo, 0);
	gBnd(phi,mpiInfo);
	prolongator(mgRes->grids[bottom-1], phi, mpiInfo);
	msg(STATUS|ONCE, "Interpolating from lvl %d -> %d", bottom, bottom-1);


	//Up to finest
	for(int current = bottom-1; current >= top; current --){

		//Load grids
		phi = mgPhi->grids[current];
		rho = mgRho->grids[current];
		res = mgRes->grids[current];

		//Prepare to go up
		gSubFrom( phi, res );

		gHaloOp(setSlice, phi,mpiInfo, 0);
		gBnd(phi,mpiInfo);

		postSmooth(phi, rho, nPostSmooth, mpiInfo);
		gBnd(phi, mpiInfo);

		msg(STATUS|ONCE, "Interpolating from lvl %d -> %d", current, current-1);

		if(current > top) prolongator(mgRes->grids[current-1], phi, mpiInfo);
	}

	return;
}

void mgFMG(int level, int bottom, int top, Multigrid *mgRho, Multigrid *mgPhi,
 			Multigrid *mgRes, const MpiInfo *mpiInfo){
	//Info
	Grid **rho = &mgRho->grids[0];;
	Grid **rhoNext;
	void (*restrictor)(const Grid *fine, Grid *coarse) = mgRho->restrictor;

	//Restrict down problem
	for(int current = 0; current < bottom; current ++){
		rhoNext = &mgRho->grids[current+1];

		gHaloOp(setSlice, *rho, mpiInfo,0);
		restrictor(*rho, *rhoNext);

		*rho = *rhoNext;
	}

	//Solve problem
	mgVRegular(bottom, bottom, 0, mgRho, mgPhi, mgRes, mpiInfo);

	return;
}

void mgW(int level, int bottom, int top, Multigrid *mgRho, Multigrid *mgPhi,
			Multigrid *mgRes, const MpiInfo *mpiInfo){

	int middle = bottom/2;

	mgVRecursive(0, bottom, middle, mgRho, mgPhi, mgRes, mpiInfo);
	mgVRecursive(middle, bottom, 0, mgRho, mgPhi, mgRes, mpiInfo);

}




void mgSolver(MgAlgo mgAlgo, Multigrid *mgRho, Multigrid *mgPhi, Multigrid *mgRes, const MpiInfo *mpiInfo){

	int nMGCycles = mgRho->nMGCycles;
	int bottom = mgRho->nLevels-1;
	int nLevels = mgRho->nLevels;

	// gZero(mgPhi->grids[0]);
	if(nLevels >1){
		for(int c = 0; c < nMGCycles; c++){
			// msg(STATUS|ONCE, "Cycle = %d", c);
			mgAlgo(0, bottom, 0, mgRho, mgPhi, mgRes, mpiInfo);
		}
	}	else {
		for(int c = 0; c < nMGCycles; c++){

			Grid *phi = mgPhi->grids[0];
			Grid *rho = mgRho->grids[0];
			gHaloOp(setSlice, rho, mpiInfo, 0);
			gBnd(rho, mpiInfo);
			mgRho->coarseSolv(phi, rho,
								mgRho->nPreSmooth+mgRho->nPostSmooth, mpiInfo);
		}
	}

	return;
}
