/**
* @file		 	collisions.c
* @brief		Collisional module, Montecarlo Collision Method.
* @author		Steffen Mattias Brask <steffen.brask@fys.uio.no>
*
* Main module collecting functions conserning collisions
*
* MCC details......
*
*/


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include <gsl/gsl_randist.h>

#include "core.h"
#include "collisions.h"
#include "multigrid.h"
#include "pusher.h"

/******************************************************************************
* 				Local functions
*****************************************************************************/

int mccTest(int one, int two){
	//bogus test to check if function calls and includes work
	return one+two;
}

/******************************************************************************
* 				Global functions
*****************************************************************************/


/******************************************************************************
* 				Local functions
*****************************************************************************/

static void mccSanity(dictionary *ini, const char* name, int nSpecies){
	// check error, see vahedi + surendra p 181
	// check for v_ion + v_neutral > maxVel (moves beyond a cell in one timestep)
	int countSpecies = iniGetInt(ini,"population:nSpecies");
	if(countSpecies!=nSpecies){
		msg(ERROR,"%s only supports 2 species",name);
	}
}

static void mccNormalize(Units *units,dictionary *ini){

	//make shure units is already normalized!

	//double *charge = iniGetDoubleArr(ini, "population:charge", nSpecies);

	//iniSetDoubleArr(ini, "population:charge", charge, nSpecies);

	//collision frequency given in 1/s
	double collFrqElectronElastic = iniGetDouble(ini,"collisions:collFrqElectronElastic");
	double collFrqIonElastic = iniGetDouble(ini,"collisions:collFrqIonElastic");
	double collFrqCEX = iniGetDouble(ini,"collisions:collFrqCEX");

	collFrqElectronElastic /= units->frequency;
	collFrqIonElastic /= units->frequency;
	collFrqCEX /= units->frequency;

	iniSetDouble(ini,"collisions:collFrqElectronElastic",collFrqElectronElastic);
	iniSetDouble(ini,"collisions:collFrqIonElastic",collFrqIonElastic);
	iniSetDouble(ini,"collisions:collFrqCEX",collFrqCEX);

	//numberdensityneutrals given as numberofparticles/m^3
	double nt = iniGetDouble(ini,"collisions:numberDensityNeutrals");

	nt /= units->density; // assumes same for elecron and ion
	nt /= units->weights[0];
	//we use computational particles that contain many real particles.
	iniSetDouble(ini,"collisions:numberDensityNeutrals",nt);

	// in m/s
	double NvelThermal = iniGetDouble(ini,"collisions:thermalVelocityNeutrals");
	NvelThermal /= (units->length/units->time);
	iniSetDouble(ini,"collisions:thermalVelocityNeutrals",NvelThermal);

	// cross sections given in m^2
	double StaticSigmaCEX = iniGetDouble(ini,"collisions:sigmaCEX");
	double StaticSigmaIonElastic = iniGetDouble(ini,"collisions:sigmaIonElastic");
	double StaticSigmaElectronElastic = iniGetDouble(ini,"collisions:sigmaElectronElastic");

	StaticSigmaCEX /= (units->length*units->length);
	StaticSigmaIonElastic /= (units->length*units->length);
	StaticSigmaElectronElastic /= (units->length*units->length);

	iniSetDouble(ini,"collisions:sigmaCEX",StaticSigmaCEX);
	iniSetDouble(ini,"collisions:sigmaIonElastic",StaticSigmaIonElastic);
	iniSetDouble(ini,"collisions:sigmaElectronElastic",StaticSigmaElectronElastic);

	// a parameter is max crossect (m^2)
	// b parameter decides velocity to center about.
	// b given as 1/v^2

	double CEX_a = iniGetDouble(ini,"collisions:CEX_a");
	double CEX_b = iniGetDouble(ini,"collisions:CEX_b");
	double ion_elastic_a = iniGetDouble(ini,"collisions:ion_elastic_a");
	double ion_elastic_b = iniGetDouble(ini,"collisions:ion_elastic_b");
	double electron_a = iniGetDouble(ini,"collisions:electron_a");
	double electron_b = iniGetDouble(ini,"collisions:electron_b");

	CEX_a /= (units->length*units->length);
	ion_elastic_a /= (units->length*units->length);
	electron_a /= (units->length*units->length);

	CEX_b *= ((units->length/units->time)*(units->length/units->time));
	ion_elastic_b *= ((units->length/units->time)*(units->length/units->time));
	electron_b *= ((units->length/units->time)*(units->length/units->time));

	iniSetDouble(ini,"collisions:CEX_a",CEX_a);
	iniSetDouble(ini,"collisions:CEX_b",CEX_b);
	iniSetDouble(ini,"collisions:ion_elastic_a",ion_elastic_a);
	iniSetDouble(ini,"collisions:ion_elastic_b",ion_elastic_b);
	iniSetDouble(ini,"collisions:electron_a",electron_a);
	iniSetDouble(ini,"collisions:electron_b",electron_b);


}

static inline void addCross(const double *a, const double *b, double *res){
	//msg(STATUS, "addCross b (S or T) is %f, %f, %f",b[0],b[1],b[2]);
	res[0] +=  (a[1]*b[2]-a[2]*b[1]);
	res[1] += -(a[0]*b[2]-a[2]*b[0]);
	res[2] +=  (a[0]*b[1]-a[1]*b[0]);
}

double mccGetMaxVel(const Population *pop, int species){

	double *vel = pop->vel;
	double MaxVelocity = 0;
	double NewVelocity = 0;
	int nDims = pop->nDims;

	long int iStart = pop->iStart[species];
	long int iStop  = pop->iStop[species];
	for(int i=iStart; i<iStop; i++){
		NewVelocity = sqrt(vel[i*nDims]*vel[i*nDims]+vel[i*nDims+1]\
			*vel[i*nDims+1]+vel[i*nDims+2]*vel[i*nDims+2]);
		if(NewVelocity>MaxVelocity){
			MaxVelocity=NewVelocity;
		}
	}
	return MaxVelocity;
}
double mccGetMinVel(const Population *pop, int species){
	//degug function
	double *vel = pop->vel;
	double MinVelocity = 100000000000000;
	double NewVelocity = 0;
	int nDims = pop->nDims;

	long int iStart = pop->iStart[species];
	long int iStop  = pop->iStop[species];
	for(int i=iStart; i<iStop; i++){
		NewVelocity = sqrt(vel[i*nDims]*vel[i*nDims]+vel[i*nDims+1]\
			*vel[i*nDims+1]+vel[i*nDims+2]*vel[i*nDims+2]);
		if(NewVelocity<MinVelocity){
			MinVelocity=NewVelocity;
		}
	}
	return MinVelocity;
}

double mccSigmaCEX(const dictionary *ini, double eps){
	// need a function for calculating real cross sects from data...

	//double a = 0.7*pow(10,-1); //minimum value of sigma
	//double b = 0.5*pow(10,-2); //slew? rate
	//double temp = a+(b/(sqrt(eps)));
	//temp = temp/(DebyeLength*DebyeLength); // convert to PINC dimensions
	double temp = iniGetDouble(ini,"collisions:sigmaCEX");
	return temp;//2*0.00121875*pow(10,8);//0.5*pow(10,-1);//temp;
}
double mccSigmaIonElastic(const dictionary *ini, double eps){
	// need a function for calculating real cross sects from data...

	//double a = 0.3*pow(10,-1); //minimum value of sigma
	//double b = 0.5*pow(10,-2); //slew? rate
	//double temp = a+(b/(sqrt(eps)));
	//temp = temp/(DebyeLength*DebyeLength); // convert to PINC dimensions
	double temp = iniGetDouble(ini,"collisions:sigmaIonElastic");
	return temp;//0.00121875*pow(10,8);//5*pow(10,-3);//temp;
}
double mccSigmaCEXFunctional(double a,double b,double v){
	// educated guess on funtional form

	double sigma = a*exp(-b*v*v);
	return sigma;//2*0.00121875*pow(10,8);//0.5*pow(10,-1);//temp;
}
double mccSigmaIonElasticFunctional(double a,double b,double v){
	// educated guess on funtional form

	double sigma = a*exp(-b*v*v);
	return sigma;//2*0.00121875*pow(10,8);//0.5*pow(10,-1);//temp;
}
double mccSigmaElectronElasticFunctional(double a,double b,double v){
	// educated guess on funtional form

	double sigma = a*exp(-b*v*v);
	return sigma;//2*0.00121875*pow(10,8);//0.5*pow(10,-1);//temp;
}
double mccSigmaElectronElastic(const dictionary *ini, double eps){
	// need a function for calculating real cross sects from data...
	//double temp = 0.3*pow(10,-1); // hardcoded order of---
	//temp = temp/(DebyeLength*DebyeLength); // convert to PINC dimensions
	double temp = iniGetDouble(ini,"collisions:sigmaElectronElastic");
	return temp;//2*0.00121875*pow(10,7);//temp;
}

void mccGetPmaxElectronConstantFrq(const dictionary *ini,
	double *Pmax, double *collFrqElectronElastic,Population *pop,
	MpiInfo *mpiInfo){

	double max_v = mccGetMaxVel(pop,0); //2.71828*thermalVel; // e*thermalVel, needs to be max_velocity function
	msg(STATUS,"maxVelocity Electron =  %f", max_v);
	*Pmax = 1-exp(-((*collFrqElectronElastic)));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "P_coll Electron = %f \n", *Pmax);
		fMsg(ini, "collision", "max velocity Electron = %f \n", max_v);

	}
}
void mccGetPmaxIonConstantFrq(const dictionary *ini,double *Pmax,
	double *collFrqIonElastic,double *collFrqCEX, Population *pop,
	MpiInfo *mpiInfo){

	double max_v = mccGetMaxVel(pop,1); //2.71828*thermalVel; // e*thermalVel, needs to be max_velocity function
	msg(STATUS,"maxVelocity Ion =  %f", max_v);

	*Pmax = 1-exp(-((*collFrqIonElastic+*collFrqCEX)));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "P_coll Ion = %f \n", *Pmax);
		fMsg(ini, "collision", "max velocity Ion = %f \n", max_v);

	}
}

void mccGetPmaxIon(const dictionary *ini,double m, double thermalVel,
	double dt, double nt, double *Pmax, double *maxfreq, Population *pop,
	MpiInfo *mpiInfo){
	// determine max local number desity / max_x(n_t(x_i)) (for target species, neutrals)
	// Get å functional form of sigma_T (total crossect as funct. of energy)
	// determine the speed, v(eps_i) = sqrt(2*eps_i *m_s)
	// determine, max_eps(sigma_T *v)

	// for now lets do
	double max_v = mccGetMaxVel(pop,1); //2.71828*thermalVel; // e*thermalVel, needs to be max_velocity function
	double min_v = mccGetMinVel(pop,1);
	msg(STATUS,"maxVelocity Ion =  %f minVelocity Ion =  %f", max_v, min_v);
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "max velocity Ion = %f \n", max_v);
	}
	*maxfreq = 0; //start at zero
	double eps = 0;

	double *vel = pop->vel;
	double NewFreq = 0;
	double NewVelocity = 0;
	int nDims = pop->nDims;

	long int iStart = pop->iStart[1]; //ions as specie 1
	long int iStop  = pop->iStop[1];
	for(int i=iStart; i<iStop; i++){
		NewVelocity = sqrt(vel[i*nDims]*vel[i*nDims]+vel[i*nDims+1]\
			*vel[i*nDims+1]+vel[i*nDims+2]*vel[i*nDims+2]);
		eps = 0.5*NewVelocity*NewVelocity*pop->mass[1];
		NewFreq = (mccSigmaCEX(ini,eps)+mccSigmaIonElastic(ini,eps))*NewVelocity*nt;
		if(NewFreq>*maxfreq){
			*maxfreq=NewFreq;
			//msg(STATUS,"sigma*vel =  %.32f", (*maxfreq/nt) );
		}
	}

	//*maxfreq = (mccSigmaCEX(eps, DebyeLength)+mccSigmaIonElastic(eps, DebyeLength))*max_v*nt; //maxfreq = max_eps(sigma_T *v)*nt (or max frequency of colls?)
	//msg(STATUS,"maxfreq Ion =  %f", *maxfreq );
	*Pmax = 1-exp(-(*maxfreq*dt));
	//*Pmax = 0.01;
	//msg(STATUS,"getPmax Ion =  %f", *Pmax);
	//return Pmax, maxfreq;
}




void mccGetPmaxElectron(const dictionary *ini, double m, double thermalVel,
	double dt, double nt,double *Pmax, double *maxfreq, Population *pop,
MpiInfo *mpiInfo){
	// determine max local number desity / max_x(n_t(x_i)) (for target species, neutrals)
	// Get a functional form of sigma_T (total crossect as funct. of energy)
	// determine the speed, v(eps_i) = sqrt(2*eps_i *m_s)
	// determine, max_eps(sigma_T *v)

	// for now lets do
	double max_v = mccGetMaxVel(pop,0);//2.71828*thermalVel; // e*thermalVel, needs to be max_velocity
	double min_v = mccGetMinVel(pop,0); // only used for debugging now
	msg(STATUS,"maxVelocity Electron =  %f minVelocity Electron =  %f", max_v, min_v);
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "max velocity electron = %f \n", max_v);
	}
	double eps = 0.0;//0.5*max_v*max_v*pop->mass[0];

	double *vel = pop->vel;
	double NewFreq = 0;
	double NewVelocity = 0;
	int nDims = pop->nDims;

	long int iStart = pop->iStart[0]; //ions as specie 1
	long int iStop  = pop->iStop[0];
	for(int i=iStart; i<iStop; i++){
		NewVelocity = sqrt(vel[i*nDims]*vel[i*nDims]+vel[i*nDims+1]\
			*vel[i*nDims+1]+vel[i*nDims+2]*vel[i*nDims+2]);
		eps = 0.5*NewVelocity*NewVelocity*pop->mass[0];
		NewFreq = mccSigmaElectronElastic(ini,eps)*NewVelocity*nt;
		if(NewFreq>*maxfreq){
			*maxfreq=NewFreq;
		}
	}

	//*maxfreq = mccSigmaElectronElastic(eps, DebyeLength)*max_v*nt; //maxfreq = max_eps(sigma_T *v)*nt (or max frequency of colls?)
	msg(STATUS,"maxfreq electron =  %f", *maxfreq );
	*Pmax = 1-exp(-(*maxfreq*dt));
	//*Pmax = 0.01;
	msg(STATUS,"getPmax Electron =  %f", *Pmax);
	//return Pmax, maxfreq;
}
void mccGetPmaxIonFunctional(const dictionary *ini,
	double dt, double nt, double *Pmax, double *maxfreq, Population *pop,
	MpiInfo *mpiInfo,double CEX_a,double CEX_b,double Elastic_a,
	double Elastic_b){
	// determine max local number desity / max_x(n_t(x_i)) (for target species, neutrals)
	// Get å functional form of sigma_T (total crossect as funct. of energy)
	// determine the speed, v(eps_i) = sqrt(2*eps_i *m_s)
	// determine, max_eps(sigma_T *v)

	// for now lets do
	double max_v = mccGetMaxVel(pop,1); //2.71828*thermalVel; // e*thermalVel, needs to be max_velocity function
	double min_v = mccGetMinVel(pop,1);
	msg(STATUS,"maxVelocity Ion =  %f minVelocity Ion =  %f", max_v, min_v);
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "max velocity Ion = %f \n", max_v);
	}
	*maxfreq = 0; //start at zero
	//double eps = 0;

	double *vel = pop->vel;
	double NewFreq = 0;
	double NewVelocity = 0;
	int nDims = pop->nDims;

	long int iStart = pop->iStart[1]; //ions as specie 1
	long int iStop  = pop->iStop[1];
	for(int i=iStart; i<iStop; i++){
		NewVelocity = sqrt(vel[i*nDims]*vel[i*nDims]+vel[i*nDims+1]\
			*vel[i*nDims+1]+vel[i*nDims+2]*vel[i*nDims+2]);
		//eps = 0.5*NewVelocity*NewVelocity*pop->mass[1];
		NewFreq = (mccSigmaCEXFunctional(CEX_a,CEX_b,NewVelocity)
			+mccSigmaIonElasticFunctional(Elastic_a,Elastic_b,NewVelocity))*NewVelocity*nt;
		if(NewFreq>*maxfreq){
			*maxfreq=NewFreq;
			//msg(STATUS,"sigma*vel =  %.32f", (*maxfreq/nt) );
		}
	}

	//*maxfreq = (mccSigmaCEX(eps, DebyeLength)+mccSigmaIonElastic(eps, DebyeLength))*max_v*nt; //maxfreq = max_eps(sigma_T *v)*nt (or max frequency of colls?)
	//msg(STATUS,"maxfreq Ion =  %f", *maxfreq );
	*Pmax = 1-exp(-(*maxfreq*dt));
	//*Pmax = 0.01;
	//msg(STATUS,"getPmax Ion =  %f", *Pmax);
	//return Pmax, maxfreq;
}
void mccGetPmaxElectronFunctional(const dictionary *ini,
	double dt, double nt,double *Pmax, double *maxfreq, Population *pop,
	MpiInfo *mpiInfo,double a, double b){
	// determine max local number desity / max_x(n_t(x_i)) (for target species, neutrals)
	// Get a functional form of sigma_T (total crossect as funct. of energy)
	// determine the speed, v(eps_i) = sqrt(2*eps_i *m_s)
	// determine, max_eps(sigma_T *v)

	// for now lets do
	double max_v = mccGetMaxVel(pop,0);//2.71828*thermalVel; // e*thermalVel, needs to be max_velocity
	double min_v = mccGetMinVel(pop,0); // only used for debugging now
	msg(STATUS,"maxVelocity Electron =  %f minVelocity Electron =  %f", max_v, min_v);
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "max velocity electron = %f \n", max_v);
	}
	//double eps = 0.0;//0.5*max_v*max_v*pop->mass[0];

	double *vel = pop->vel;
	double NewFreq = 0;
	double NewVelocity = 0;
	int nDims = pop->nDims;

	long int iStart = pop->iStart[0]; //ions as specie 1
	long int iStop  = pop->iStop[0];
	for(int i=iStart; i<iStop; i++){
		NewVelocity = sqrt(vel[i*nDims]*vel[i*nDims]+vel[i*nDims+1]\
			*vel[i*nDims+1]+vel[i*nDims+2]*vel[i*nDims+2]);
		//eps = 0.5*NewVelocity*NewVelocity*pop->mass[0];
		NewFreq = mccSigmaElectronElasticFunctional(a,b,NewVelocity)*NewVelocity*nt;
		if(NewFreq>*maxfreq){
			*maxfreq=NewFreq;
		}
	}

	//*maxfreq = mccSigmaElectronElastic(eps, DebyeLength)*max_v*nt; //maxfreq = max_eps(sigma_T *v)*nt (or max frequency of colls?)
	msg(STATUS,"maxfreq electron =  %f", *maxfreq );
	*Pmax = 1-exp(-(*maxfreq*dt));
	//*Pmax = 0.01;
	msg(STATUS,"getPmax Electron =  %f", *Pmax);
	//return Pmax, maxfreq;
}

void mccGetPmaxIonStatic(const dictionary *ini,const double mccSigmaCEX,
	const double mccSigmaIonElastic, double dt, double nt, double *Pmax,
	double *maxfreq, Population *pop, MpiInfo *mpiInfo){

	// Faster static version. uses static cross sections

	// determine max local number desity / max_x(n_t(x_i)) (for target species, neutrals)
	// Get å functional form of sigma_T (total crossect as funct. of energy)
	// determine the speed, v(eps_i) = sqrt(2*eps_i *m_s)
	// determine, max_eps(sigma_T *v)

	double max_v = mccGetMaxVel(pop,1); //2.71828*thermalVel; // e*thermalVel, needs to be max_velocity function
	*maxfreq = (mccSigmaCEX+mccSigmaIonElastic)*max_v*nt;
	*Pmax = 1-exp(-(*maxfreq));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision","getPmax Ion =  %f \n", *Pmax);
		fMsg(ini, "collision", "max velocity Ion = %f \n", max_v);
		fMsg(ini, "collision","dt =  %f \n", dt);
	}

}



void mccGetPmaxElectronStatic(const dictionary *ini,const double StaticSigmaElectronElastic,
	double dt, double nt,double *Pmax, double *maxfreq, Population *pop,
	MpiInfo *mpiInfo){

	// Faster static version. uses static cross sections

	// determine max local number desity / max_x(n_t(x_i)) (for target species, neutrals)
	// Get a functional form of sigma_T (total crossect as funct. of energy)
	// determine the speed, v(eps_i) = sqrt(2*eps_i *m_s)
	// determine, max_eps(sigma_T *v)

	// for now lets do
	double max_v = mccGetMaxVel(pop,0);//2.71828*thermalVel; // e*thermalVel, needs to be max_velocity
	msg(STATUS,"maxVelocity electron =  %f", max_v);

	//msg(STATUS,"maxveloc electron =  %f", max_v);
	*maxfreq=StaticSigmaElectronElastic*max_v*nt;

	*Pmax = 1-exp(-(*maxfreq));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision","getPmax electron =  %f \n", *Pmax);
		fMsg(ini, "collision", "max velocity electron = %f \n", max_v);
	}
}

double mccGetMyCollFreqStatic(double sigma_T, double vx,double vy,double vz, double nt){
	// collision freq given a static (double) cross section
	// this means nu \propto v
	double v = sqrt(vx*vx + vy*vy + vz*vz);
	double MyFreq = v*sigma_T*nt;

	return MyFreq;
}
double mccGetMyCollFreqFunctional(double (*sigma)(double, double,double),
						double vx,double vy,double vz,double nt,
						double a,double b){

	// collision freq given a cross section function
	double v = sqrt(vx*vx + vy*vy + vz*vz);
	//double eps_i = 0.5*m*v*v;

	//we use v instead of Energy
	double Mysigma = sigma(a,b,v);
	double MyFreq = v*Mysigma*nt;
	return MyFreq;
}

double mccGetMyCollFreq(const dictionary *ini, double (*sigma)(const dictionary *ini, double),
						double m, double vx,double vy,double vz, double dt, double nt){
	// collision freq given a cross section function
	double v = sqrt(vx*vx + vy*vy + vz*vz);
	double eps_i = 0.5*m*v*v;
	double sigma_T = sigma(ini,eps_i);
	double MyFreq = v*sigma_T*nt;
	//msg(STATUS,"Myfreq =  %f", MyFreq);
	//double P = 1-exp(-(MyFreq*0.04));
	//msg(STATUS,"My P =  %f", P);
	return MyFreq;
}
double mccEnergyDiffIonElastic(double Ekin, double theta, double mass1, double mass2){
	double temp;
	//msg(STATUS, "Ekin = %.32f", Ekin);
	//msg(STATUS, "theta = %f", theta);
	//msg(STATUS, "mass1 = %f", mass1);
	//msg(STATUS, "mass2 = %f", mass2);

	temp = Ekin*( 1-((2*mass1*mass2)/((mass1+mass2)*(mass1+mass2)) )*(1.0-cos(theta)) );
	//temp = Ekin*(cos(2*theta)*cos(2*theta));
	//temp = Ekin*(cos(theta)*cos(theta));
	//msg(STATUS, "temp = %.32f", temp);
	return temp;
}

double mccEnergyDiffElectronElastic(double Ekin, double theta, double mass1,
	double mass2){

	double temp;
	//msg(STATUS, "Ekin = %.32f", Ekin);
	//msg(STATUS, "theta = %f", theta);
	//msg(STATUS, "mass1 = %f", mass1);
	//msg(STATUS, "mass2 = %f", mass2);

	//temp = Ekin*( 1-((2*mass1*mass2)/((mass1+mass2)*(mass1+mass2)) )*(1.0-cos(theta)) );
	//temp = Ekin*(cos(2*theta)*cos(2*theta));
	temp = ((2.0*mass1)/(mass2))*(1.0-cos(theta));
	//msg(STATUS, "temp = %.32f", 1.0-cos(theta));
	return temp;
}

void mccCollideElectron_debug(const dictionary *ini,Population *pop,
	double *Pmax, double *maxfreqElectron, const gsl_rng *rng, double dt,
	double nt){


	// DEPRECATED! Historic artifact

	msg(STATUS,"colliding Electrons");

	int nDims = pop->nDims;
	double *vel = pop->vel;
	double *mass = pop->mass;

	double R = gsl_rng_uniform_pos(rng);
	double Rp = gsl_rng_uniform(rng);

	double angleChi = 0; //scattering angle in x, y
	double anglePhi = 0; //scattering angle in j
	double angleTheta = 0;
	double A = 0; //scaling Factor used for precomputing (speedup)
	long int q = 0;
	double vx, vy, vz;

	long int errorcounter = 0;
	long int errorcounter3 = 0;
	long int errorcounter4 = 0;
	long int debugindex = 0;
	double Ekin = 0;
	double EkinAfter = 0;
	double AccumulatedEnergyDiff = 0;
	double old_EnergyDiff = -10000;
	double EnergyDiff = 0;
	long int iStart = pop->iStart[0];		// *nDims??
	long int iStop = pop->iStop[0];      // make shure theese are actually electrons!
	long int NparticleColl = (*Pmax)*(pop->iStop[0]-pop->iStart[0]); // 1 dim ?, Number of particles too collide
	long int mccStepSize = floor((double)((iStop-iStart))\
	/ (double)(NparticleColl));
	//if ((mccStepSize-1)*(NparticleColl) > (iStop-iStart)){ //errorcheck, remove
	//	msg(WARNING,"particle collisions out of bounds in mccCollideElectron");
	//}

	msg(STATUS,"colliding %i of %i electrons", NparticleColl, (iStop-iStart));
	msg(STATUS,"Particles per box to pick one from = %i", (mccStepSize));

	long int mccStop = iStart + mccStepSize*NparticleColl; //enshure non out of bounds
	if (mccStop > iStop){ //errorcheck, remove
		msg(WARNING,"particle collisions out of bounds in mccCollideElectron");
		msg(WARNING,"mccStop = %i is bigger then iStop = %i",mccStop,iStop);
	}
	for(long int i=iStart;i<mccStop-nDims;i+=mccStepSize){  //check this statement #segfault long int p=pStart;p<pStart+Pmax*(pStop-pStart);p++)
		errorcounter3 += 1;
		//msg(STATUS,"errorcounter = %i", (errorcounter));
		if (errorcounter3 > NparticleColl){ //errorcheck, remove
			msg(ERROR,"errorcounter = %i should be the same or less as the \
			number of coll	particles = %i", (errorcounter),NparticleColl/nDims);
		//	msg(WARNING,"particle collisions out of bounds in mccCollideIon");
			//errorcounter = 0;
		}
		if (i > iStop){
			msg(STATUS,"iStop = %i and index=%i", iStop, i);
		}
		if (i < iStart){
			msg(STATUS,"iStop = %i and index=%i", iStop, i);
		}
		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
		Rp = gsl_rng_uniform_pos(rng); // separate rand num. for prob.
		q = floor((i + (R*mccStepSize))*nDims);
		//q = i*nDims; //testing with first index
		if(q>iStop*nDims){
			msg(ERROR,"index out of range, q>iStop in collideElectron");
		}
		//msg(STATUS,"R=%f, Rp = %f, q = %i iStart = %i, iStop = %i", R,Rp,q,iStart,iStop );
		//if(q>iStart+5)
		//	msg(ERROR, "qsss");

		vx = vel[q];
		vy = vel[q+1];
		vz = vel[q+2];
		// need n,n+1,n+2 for x,y,j ?
		//msg(STATUS,"vx = %f, vy = %f, vz = %f", vx,vy,vz);

		double MyCollFreq = mccGetMyCollFreq(ini,mccSigmaElectronElastic, mass[0],vx,vy,vz,dt,nt); //prob of coll for particle i

		//msg(STATUS,"is Rp = %f < MyCollFreq/maxfreqElectron = %f", Rp, (MyCollFreq/ *maxfreqElectron));
		//msg(STATUS,"MyCollFreq %f, maxfreqElectron = %f", MyCollFreq, *maxfreqElectron);

		if (Rp<(MyCollFreq/ *maxfreqElectron)){
		// Pmax gives the max possible colls. but we will "never" reach this
		// number, so we need to test for each particles probability.
			errorcounter += 1;
			debugindex = i;
			if (( (MyCollFreq)/ *maxfreqElectron)>1.0000000001){
				//put in sanity later, need real cross sects
				msg(WARNING,"MyCollFreqElectron/ *maxfreqElectron > 1, MyCollFreq = %f, maxFreq = %f",MyCollFreq, *maxfreqElectron);
			}
			R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?

			Ekin = 0.5*(vx*vx + vy*vy + vz*vz)*mass[0]; // values stored in population for speed??
			//msg(STATUS,"ekin = %f, R =  %f", Ekin, R);
			double argument = (2+Ekin-2*pow((1+Ekin),R))/(Ekin); // This one has the posibility to be greater than 1!!

			if(sqrt(argument*argument)>1.0){
				double newargument = sqrt(argument*argument)/argument;
				msg(WARNING,"old argument = %.32f new arg = %.64f, R = %.32f", argument, newargument,R);
				argument = newargument;
			}
			angleChi =  acos(argument); // gives nan value if abs(argument) > 1

			//for debugging
			if(sqrt(argument*argument)>1.0){
				msg(ERROR,"argument of cos() is greater than 1. argument = %32f", argument);
			}
			if(isnan(angleChi)){
				msg(STATUS, "abs(argument) = %32f", sqrt(argument*argument));
				msg(ERROR,"angleChi == nan!!!! new velocity is cos(nan)  argument of acos() is %.32f!!", argument);
			}
			//msg(STATUS,"angleChi = %f, power is %.32f, argument is %f",angleChi, pow((1+Ekin),R),argument);
			anglePhi = 2*PI*R; // set different R?
			angleTheta = acos(vx);
			if(sin(angleTheta) == 0.0){
				msg(ERROR,"float division by zero in collideion");
			}
			EnergyDiff = mccEnergyDiffElectronElastic(Ekin, angleChi, mass[0], mass[1]);
			A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);
			vel[q] = vx*cos(angleChi)+A*(vy*vy + vz*vz); //Vx
			if(isnan(angleTheta)){
				msg(STATUS, "abs(argument) = %32f", sqrt(argument*argument));
				msg(ERROR,"angleTheta == nan!!!! new velocity is nan*(somestuffs)  = %.32f!!", vx*cos(angleChi)+A*(vy*vy + vz*vz));
			}
			if(isinf(A)){
				msg(ERROR,"A is inf in Collide Electron !!");
			}
			if(isnan(A)){
				msg(ERROR,"A is nan in Collide Electron !!");
			}
			vel[q+1] = vy*cos(angleChi)+A*vz-A*vx*vy; //Vy
			vel[q+2] = vz*cos(angleChi)-A*vy-A*vx*vz; //Vz
			//msg(STATUS,"newvx = %f, newvy = %f, newvz = %f", vel[q],vel[q+1],vel[q+2]);
			EkinAfter = 0.5*(vel[q]*vel[q] + vel[q+1]*vel[q+1] + vel[q+2]*vel[q+2])*mass[0];

			AccumulatedEnergyDiff += (Ekin-EkinAfter);
			//msg(STATUS,"this error = %.32f, analerror = %.32f",(Ekin-EkinAfter), EnergyDiff);
			//msg(STATUS,"abs((Ekin-EkinAfter)*(Ekin-EkinAfter) - EnergyDiff*EnergyDiff) = %.64f",abs(sqrt((Ekin-EkinAfter)*(Ekin-EkinAfter)) - sqrt(EnergyDiff*EnergyDiff)));
			if(old_EnergyDiff < sqrt((sqrt((Ekin-EkinAfter)*(Ekin-EkinAfter)) - sqrt(EnergyDiff*EnergyDiff))*(sqrt((Ekin-EkinAfter)*(Ekin-EkinAfter)) - sqrt(EnergyDiff*EnergyDiff)))){
				old_EnergyDiff = sqrt((sqrt((Ekin-EkinAfter)*(Ekin-EkinAfter)) - sqrt(EnergyDiff*EnergyDiff))*(sqrt((Ekin-EkinAfter)*(Ekin-EkinAfter)) - sqrt(EnergyDiff*EnergyDiff)));
				//msg(STATUS,"updated energydiff %f",sqrt((sqrt((Ekin-EkinAfter)*(Ekin-EkinAfter)) - sqrt(EnergyDiff*EnergyDiff))*(sqrt((Ekin-EkinAfter)*(Ekin-EkinAfter)) - sqrt(EnergyDiff*EnergyDiff))));
			}
			if(sqrt(vel[q]*vel[q] + vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2])>sqrt(vx*vx+vy*vy+vz*vz)){
				//msg(STATUS,"argument of acos() = %f", argument);
				//msg(STATUS,"acos() = %f",angleChi );
				//msg(STATUS,"A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta) = %f",A );
				//msg(STATUS,"vx = %f, vy = %f, vz = %f",vel[q],vel[q+1],vel[q+2]);
				errorcounter4 += 1;
			}


		}

	}
	msg(STATUS, "LargestEnergyError = %.32f",old_EnergyDiff );
	msg(STATUS, "AccumulatedEnergyDiff = %.32f",AccumulatedEnergyDiff );
	msg(STATUS, "counted %i energy increases",errorcounter4);
	//msg(STATUS,"errorcounter = %i should be the same as number of coll particles = %i", (errorcounter),NparticleColl);
	msg(STATUS,"%i Electron collisions actually performed and last index was %i", errorcounter, debugindex);
	msg(STATUS,"Done colliding Electrons");
	//free?
}

void mccCollideIon_debug(const dictionary *ini, Population *pop, double *Pmax,
	double *maxfreqIon, const gsl_rng *rng, double timeStep, double nt){

	// DEPRECATED! Historic artifact

	msg(STATUS,"colliding Ions");

	//int nSpecies = pop->nSpecies;
	int nDims = pop->nDims;
	double *vel = pop->vel;
	double *mass = pop->mass;
	double NvelThermal = iniGetDouble(ini,"collisions:thermalVelocityNeutrals");
	//double timeStep = iniGetDouble(ini,"time:timeStep");
	//double stepSize = iniGetDouble(ini,"grid:stepSize");
	//double velTh = (timeStep/stepSize)*NvelThermal; //neutrals
	//free(velThermal);

	double R, R1, Rp, Rq;

	double angleChi = 0; //scattering angle in x, y
	double anglePhi = 0; //scattering angle in j
	double angleTheta = 0;
	double A = 0; //scaling Factor used for precomputing (speedup)
	long int q = 0;
	long int last_q = 0;
	//double fraction = 0.7; //defines fraction of particles colliding w el/ch-ex
	double vxMW, vyMW, vzMW;
	long int errorcounter = 0;
	long int errorcounter1 = 0;
	long int errorcounter2 = 0;
	long int errorcounter3 = 0;
	long int errorcounter4 = 0;
	long int debugindex = 0;
	double old_EnergyDiff = 0;
	double Ekin = 0;
	double EkinAfter = 0;
	double EnergyDiff = 1;
	double AccumulatedEnergyDiff = 0;

	long int iStart = pop->iStart[1];			// *nDims??
	long int iStop = pop->iStop[1];      // make shure theese are actually ions!
	long int NparticleColl = (*Pmax)*(pop->iStop[1]-pop->iStart[1]); // 1 dim ?, Number of particles too collide
	long int mccStepSize = floor((double)((iStop-iStart))\
	/ (double)(NparticleColl));
	//if ((mccStepSize-1)*(NparticleColl)/nDims > (iStop-iStart)){ //errorcheck, remove
	//	msg(WARNING,"particle collisions out of bounds in mccCollideIon");
	//	msg(WARNING,"%i is bigger than array = %i",(mccStepSize-1)*(NparticleColl),(iStop-iStart) );
	//}

	msg(STATUS,"colliding %i of %i ions", NparticleColl, (iStop-iStart));
	msg(STATUS,"Particles per box to pick one from = %i", (mccStepSize));

	long int mccStop = iStart + mccStepSize*NparticleColl; //enshure non out of bounds
	// but also that last index probably never collides...
	if (mccStop > iStop){ //errorcheck, remove
		msg(WARNING,"particle collisions out of bounds in mccCollideIon");
		msg(WARNING,"mccStop = %i is bigger then iStop = %i",mccStop,iStop);
	}
	for(long int i=iStart;i<mccStop;i+=mccStepSize){  //check this statement #segfault long int p=pStart;p<pStart+Pmax*(pStop-pStart);p++)
		errorcounter3 += 1;
		if (errorcounter3 > NparticleColl){ //errorcheck, remove
			msg(ERROR,"errorcounter = %i should be the same or less as the \
			number of coll	particles = %i", (errorcounter),NparticleColl/nDims);
		//	msg(WARNING,"particle collisions out of bounds in mccCollideIon");
			//errorcounter = 0;
		}
		if (i > iStop){
			msg(STATUS,"iStop = %i and index=%i", iStop, i);
		}
		if (i < iStart){
			msg(STATUS,"iStart = %i and index=%i", iStart, i);
		}
		Rp = gsl_rng_uniform_pos(rng); //decides type of coll.
		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
		Rq = gsl_rng_uniform_pos(rng);
		q = (i + (Rq*mccStepSize))*nDims; // particle q collides
		//msg(STATUS,"R=%g, p = %i, q = %i", R,p,q );
		if(q>iStop*nDims){
			msg(ERROR,"index out of range, q>iStop in collideIon");
		}
		vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
		vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
		vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
		//msg(STATUS,"neutral velx = %f vely = %f velz = %f",vxMW,vyMW,vzMW );

		//transfer to CM-frame
		double vxTran = vel[q]-(vel[q]-vxMW)/2.0;
		double vyTran = vel[q+1]-(vel[q+1]-vyMW)/2.0;
		double vzTran = vel[q+2]- (vel[q+2]-vzMW)/2.0;

		double MyCollFreq1 = mccGetMyCollFreq(ini,mccSigmaIonElastic, mass[1],vxTran,vyTran,vzTran,timeStep,nt); //prob of coll for particle i
		double MyCollFreq2 = mccGetMyCollFreq(ini,mccSigmaCEX, mass[1],vxTran,vyTran,vzTran,timeStep,nt); //duplicate untill real cross sections are implemented

		//msg(STATUS,"is Rp = %f < MyCollFreq/maxfreqElectron = %f", Rp, ((MyCollFreq1+MyCollFreq2)/ *maxfreqIon));
		if (Rp<( (MyCollFreq1+MyCollFreq2)/ *maxfreqIon)){ // MyCollFreq1+MyCollFreq2 defines total coll freq.
			//Rp > frq ? or Rp < frq ? this collides faster or slower ions more often
			errorcounter2 += 1;
			debugindex = i;
			if (( (MyCollFreq1+MyCollFreq2)/ *maxfreqIon)>1.0000000001){
				//put in sanity later, need real cross sects
				msg(WARNING,"(MyCollFreq1+MyCollFreq2)/ *maxfreqIon > 1");
				msg(WARNING,"(MyCollFreq1= %f MyCollFreq2 = %f *maxfreqIon =%f",MyCollFreq1,MyCollFreq2,*maxfreqIon);
			}
			if(Rp < MyCollFreq1/ *maxfreqIon){ // if test slow, but enshures randomness...
				// elastic:

				//msg(STATUS,"elastic");


				//msg(ERROR,"Elastic collision"); // temporary test
				//Ekin = 0.5*(vxTran*vxTran + vyTran*vyTran + vzTran*vzTran)*mass[1];
				Ekin = 0.5*(vel[q]*vel[q] + vel[q+1]*vel[q+1] + vel[q+2]*vel[q+2])*mass[1]; // lab frame
				R1 = gsl_rng_uniform_pos(rng);

				//The scattering happens in center of mass so we use THETA not chi
				double argument = (acos(1-2*R)/2.0);

				if(sqrt(argument*argument)>1.0){
					double newargument = sqrt(argument*argument)/argument;
					msg(WARNING,"old argument = %.32f new arg = %.64f", argument, newargument);
					argument = newargument;
				}
				angleChi =  acos(argument); // gives nan value if abs(argument) > 1

				//for debugging
				if(sqrt(argument*argument)>1.0){
					msg(ERROR,"argument of cos() is greater than 1. argument = %32f", argument);
				}
				if(isnan(angleChi)){
					msg(STATUS, "abs(argument) = %32f", sqrt(argument*argument));
					msg(ERROR,"angleChi == nan!!!! new velocity is cos(nan)  argument of acos() is %.32f!!", argument);
				}

				//angleChi = acos(sqrt(1-R)); // for M_ion = M_neutral
				anglePhi = 2*PI*R1; // set different R?
				//angleTheta = 2*angleChi; //NOPE! not the same!
				angleTheta = acos(vel[q]); //C_M frame
				double angleTheta_lab = acos(sqrt(1-R));
				EnergyDiff = mccEnergyDiffIonElastic(Ekin, angleTheta_lab, mass[1], mass[1]);//Acctually Ion and neutral mass

				//if(sin(angleTheta) == 0.0){
				//	msg(ERROR,"float division by zero in collideion", argument);
				//}
				A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);

				if(isinf(A)){
					msg(ERROR,"A is inf in Collide ion !!");
				}
				if(isnan(A)){
					msg(ERROR,"A is nan in Collide Ion !!");
				}
				vel[q] = vxTran*cos(angleChi)+A*(vyTran*vyTran + vzTran*vzTran) \
				+ vxMW; //Vx
				vel[q+1] = vyTran*cos(angleChi)+A*vzTran-A*vxTran*vyTran + vyMW; //Vy
				vel[q+2] = vzTran*cos(angleChi)-A*vyTran-A*vxTran*vzTran + vzMW; //Vz
				if(sqrt(vel[q]*vel[q] + vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2])>sqrt((vxTran+vxMW)*(vxTran+vxMW)+(vyTran+vyMW)*(vyTran+vyMW)+(vzTran+vzMW)*(vzTran+vxMW))){
					//msg(STATUS,"argument of acos() = %f", argument);
					//msg(STATUS,"acos() = %f",angleChi );
					msg(STATUS,"A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta) = %f",A );
					//msg(STATUS,"vx = %f, vy = %f, vz = %f",vel[q],vel[q+1],vel[q+2]);
					errorcounter4 += 1;
				}
				//if(sqrt(A*A)<0.00000001){
				//	msg(STATUS,"argument of acos() = %f", argument);
				//	msg(STATUS,"acos() = %f",angleChi );
				//	msg(STATUS,"A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta) = %f",A );
				//}
				//EkinAfter = 0.5*((vel[q]-vxMW)*(vel[q]-vxMW) + (vel[q+1]-vyMW)*(vel[q+1]-vyMW) + (vel[q+2]-vzMW)*(vel[q+2])-vyMW)*mass[1]; //CM frame
				EkinAfter = 0.5*((vel[q])*(vel[q]) + (vel[q+1])*(vel[q+1]) + (vel[q+2])*(vel[q+2]))*mass[1]; // lab frame
				AccumulatedEnergyDiff += (Ekin-EkinAfter);
				if(old_EnergyDiff > (Ekin-EkinAfter)*(Ekin-EkinAfter) - EnergyDiff*EnergyDiff ){
					old_EnergyDiff = EnergyDiff;
				}
				///msg(STATUS," ");
				///msg(STATUS, "---------- energydiff = %.32f", (Ekin-EkinAfter) );
				///msg(STATUS, "analytical energydiff = %.32f",EnergyDiff );
				///msg(STATUS, "deviation = %.32f",EnergyDiff-(Ekin-EkinAfter) );
				//msg(STATUS," ");
				errorcounter1 += 1;
			}else{
				// Charge exchange:
				// flip ion and neutral. Neutral is new ion.
				//msg(STATUS,"ch-ex");
				errorcounter += 1;
				vel[q] = vxMW; //Vx
				vel[q+1] = vyMW; //Vy
				vel[q+2] = vzMW; //Vz
			}
		}
		last_q = q;
	}
	// Special handling of last box to let every particle have posibillity to collide

	Rp = gsl_rng_uniform_pos(rng); //decides type of coll.
	R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
	Rq = gsl_rng_uniform_pos(rng);
	q = ( (last_q) + (Rq*(iStop*nDims-last_q)) ); // particle q collides
	msg(STATUS,"LAst collision q = %i and iStop*nDims = %i, previous last q = %i", q,iStop*nDims,last_q );
	if(q>iStop*nDims){
		msg(ERROR,"index out of range, q>iStop in collideIon last box");
	}
	vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
	vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
	vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
	//msg(STATUS,"neutral velx = %f vely = %f velz = %f",vxMW,vyMW,vzMW );
	double vxTran = vel[q]-vxMW;   //simple transfer, should be picked from maxwellian
	double vyTran = vel[q+1]-vyMW;  //vel-velMaxwellian
	double vzTran = vel[q+2]-vzMW;  // this will for now break conservation laws???

	double MyCollFreq1 = mccGetMyCollFreq(ini,mccSigmaIonElastic, mass[1],vxTran,vyTran,vzTran,timeStep,nt); //prob of coll for particle i
	double MyCollFreq2 = mccGetMyCollFreq(ini,mccSigmaCEX, mass[1],vxTran,vyTran,vzTran,timeStep,nt); //duplicate untill real cross sections are implemented



	if (Rp<( (MyCollFreq1+MyCollFreq2)/ *maxfreqIon)){ // MyCollFreq1+MyCollFreq2 defines total coll freq.
		errorcounter2 += 1;
		debugindex = q;
		if (( (MyCollFreq1+MyCollFreq2)/ *maxfreqIon)>1.0000000001){
			//put in sanity later, need real cross sects
			msg(WARNING,"(MyCollFreq1+MyCollFreq2)/ *maxfreqIon > 1");
			msg(WARNING,"(MyCollFreq1= %f MyCollFreq2 = %f *maxfreqIon =%f",MyCollFreq1,MyCollFreq2,*maxfreqIon);
		}
		if(Rp < MyCollFreq1/ *maxfreqIon){ // if test slow, but enshures randomness...
			// elastic:

			//msg(STATUS,"elastic");


			//msg(ERROR,"Elastic collision"); // temporary test
			//Ekin = 0.5*(vxTran*vxTran + vyTran*vyTran + vzTran*vzTran)*mass[1];
			Ekin = 0.5*(vel[q]*vel[q] + vel[q+1]*vel[q+1] + vel[q+2]*vel[q+2])*mass[1]; // lab frame
			R1 = gsl_rng_uniform_pos(rng);

			//The scattering happens in center of mass so we use THETA not chi
			double argument = (acos(1-2*R)/2.0);

			if(sqrt(argument*argument)>1.0){
				double newargument = sqrt(argument*argument)/argument;
				msg(WARNING,"old argument = %.32f new arg = %.64f", argument, newargument);
				argument = newargument;
			}
			angleChi =  acos(argument); // gives nan value if abs(argument) > 1

			//for debugging
			if(sqrt(argument*argument)>1.0){
				msg(ERROR,"argument of cos() is greater than 1. argument = %32f", argument);
			}
			if(isnan(angleChi)){
				msg(STATUS, "abs(argument) = %32f", sqrt(argument*argument));
				msg(ERROR,"angleChi == nan!!!! new velocity is cos(nan)  argument of acos() is %.32f!!", argument);
			}

			//angleChi = acos(sqrt(1-R)); // for M_ion = M_neutral
			anglePhi = 2*PI*R1; // set different R?
			//angleTheta = 2*angleChi; //NOPE! not the same!
			angleTheta = acos(vel[q]); //C_M frame
			double angleTheta_lab = acos(sqrt(1-R));
			EnergyDiff = mccEnergyDiffIonElastic(Ekin, angleTheta_lab, mass[1], mass[1]);//Acctually Ion and neutral mass

			//if(sin(angleTheta) == 0.0){
			//	msg(ERROR,"float division by zero in collideion", argument);
			//}
			A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);

			if(isinf(A)){
				msg(ERROR,"A is inf in Collide ion !!");
			}
			if(isnan(A)){
				msg(ERROR,"A is nan in Collide Ion !!");
			}
			vel[q] = vxTran*cos(angleChi)+A*(vyTran*vyTran + vzTran*vzTran) \
			+ vxMW; //Vx
			vel[q+1] = vyTran*cos(angleChi)+A*vzTran-A*vxTran*vyTran + vyMW; //Vy
			vel[q+2] = vzTran*cos(angleChi)-A*vyTran-A*vxTran*vzTran + vzMW; //Vz
			if(sqrt(vel[q]*vel[q] + vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2])>sqrt((vxTran+vxMW)*(vxTran+vxMW)+(vyTran+vyMW)*(vyTran+vyMW)+(vzTran+vzMW)*(vzTran+vxMW))){
				msg(STATUS,"argument of acos() = %f", argument);
				msg(STATUS,"acos() = %f",angleChi );
				msg(STATUS,"A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta) = %f",A );
				msg(STATUS,"vx = %f, vy = %f, vz = %f",vel[q],vel[q+1],vel[q+2]);
				errorcounter4 += 1;
			}
			//if(sqrt(A*A)<0.00000001){
			//	msg(STATUS,"argument of acos() = %f", argument);
			//	msg(STATUS,"acos() = %f",angleChi );
			//	msg(STATUS,"A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta) = %f",A );
			//}
			//EkinAfter = 0.5*((vel[q]-vxMW)*(vel[q]-vxMW) + (vel[q+1]-vyMW)*(vel[q+1]-vyMW) + (vel[q+2]-vzMW)*(vel[q+2])-vyMW)*mass[1]; //CM frame
			EkinAfter = 0.5*((vel[q])*(vel[q]) + (vel[q+1])*(vel[q+1]) + (vel[q+2])*(vel[q+2]))*mass[1]; // lab frame
			AccumulatedEnergyDiff += (Ekin-EkinAfter);
			if(old_EnergyDiff > (Ekin-EkinAfter)*(Ekin-EkinAfter) - EnergyDiff*EnergyDiff ){
				old_EnergyDiff = EnergyDiff;
			}
			///msg(STATUS," ");
			///msg(STATUS, "---------- energydiff = %.32f", (Ekin-EkinAfter) );
			///msg(STATUS, "analytical energydiff = %.32f",EnergyDiff );
			///msg(STATUS, "deviation = %.32f",EnergyDiff-(Ekin-EkinAfter) );
			//msg(STATUS," ");
			errorcounter1 += 1;
		}else{
			// Charge exchange:
			// flip ion and neutral. Neutral is new ion.
			//msg(STATUS,"ch-ex");
			errorcounter += 1;
			vel[q] = vxMW; //Vx
			vel[q+1] = vyMW; //Vy
			vel[q+2] = vzMW; //Vz
		}
	}



	//debug printout
	msg(STATUS, "LargestEnergyError = %.32f",old_EnergyDiff );
	msg(STATUS, "AccumulatedEnergyDiff = %.32f",AccumulatedEnergyDiff );
	msg(STATUS, "counted %i energy increases",errorcounter4);
	msg(STATUS,"%i ion collisions actually performed and last index was %i", errorcounter2, debugindex-iStart);
	msg(STATUS,"%i ion collisions as ch-ex and %i as elastic, the sum should be %i", errorcounter, errorcounter1,errorcounter2);

	//msg(STATUS,"errorcounter = %i should be the same as number of coll particles = %i", (errorcounter),NparticleColl);

	//free?
	msg(STATUS,"Done colliding Ions");
}

void mccCollideElectronStatic(const dictionary *ini, Population *pop,
	double *Pmax,double *maxfreqElectron, const gsl_rng *rng,
	double nt, double mccSigmaElectronElastic, MpiInfo *mpiInfo){

	// uses static CROSS-Sections, collfreq is proportional to v
	//msg(STATUS,"colliding Electrons");

	int nDims = pop->nDims;
	double *vel = pop->vel;
	double *mass = pop->mass;

	double R = gsl_rng_uniform_pos(rng);
	double Rp = gsl_rng_uniform(rng);

	double angleChi = 0; //scattering angle in x, y
	double anglePhi = 0; //scattering angle in j
	double angleTheta = 0;
	double A = 0; //scaling Factor used for precomputing (speedup)
	long int q = 0;
	double vx, vy, vz;
	long int last_i = 0;

	long int errorcounter = 0;

	double Ekin = 0;
	long int iStart = pop->iStart[0];		// *nDims??
	long int iStop = pop->iStop[0];      // make shure theese are actually electrons!
	long int NparticleColl = (*Pmax)*(pop->iStop[0]-pop->iStart[0]); // 1 dim ?, Number of particles too collide
	long int mccStepSize = floor((double)((iStop-iStart))\
	/ (double)(NparticleColl));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "colliding %i of %i electrons\n", (NparticleColl), ((iStop-iStart)));
	}
	//msg(STATUS,"colliding %i of %i electrons", NparticleColl, (iStop-iStart));

	//long int mccStop = iStart + mccStepSize*NparticleColl; //enshure non out of bounds

	//check energies
	//double ekin = 0; //test value
	// double ekin_anal = 0;
	// double ekinafter =0;
	// double ekindiff =0;

	for(long int i=iStart;i<iStop;i+=mccStepSize){  //check this statement #segfault long int p=pStart;p<pStart+Pmax*(pStop-pStart);p++)

		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
		Rp = gsl_rng_uniform_pos(rng); // separate rand num. for prob.
		q = ((i + floor(R*mccStepSize))*nDims);

		vx = vel[q];
		vy = vel[q+1];
		vz = vel[q+2];
		// need n,n+1,n+2 for x,y,j ?

		double MyCollFreq = mccGetMyCollFreqStatic(mccSigmaElectronElastic,  \
			vx,vy,vz,nt); //prob of coll for particle i

		if (Rp<(MyCollFreq/ *maxfreqElectron)){
		// Pmax gives the max possible colls. but we will "never" reach this
		// number, so we need to test for each particles probability.
			errorcounter += 1;

			R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?

			Ekin = 0.5*(vx*vx + vy*vy + vz*vz)*mass[0]; // values stored in population for speed??

			double argument = (2+Ekin-2*pow((1+Ekin),R))/(Ekin); // This one has the posibility to be greater than 1!!

			if(sqrt(argument*argument)>1.0){
				double newargument = sqrt(argument*argument)/argument;
				fMsg(ini, "collision", "WARNING: old argument = %.32f new arg = %.64f, R = %.32f \n", argument, newargument,R);
				//msg(WARNING,"old argument = %.32f new arg = %.64f, R = %.32f", argument, newargument,R);
				argument = newargument;
			}
			angleChi =  acos(argument); // gives nan value if abs(argument) > 1
			anglePhi = 2*PI*R; // set different R?
			angleTheta = acos(vx);

			A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);

			vel[q] = vx*cos(angleChi)+A*(vy*vy + vz*vz); //Vx
			vel[q+1] = vy*cos(angleChi)+A*vz-A*vx*vy; //Vy
			vel[q+2] = vz*cos(angleChi)-A*vy-A*vx*vz; //Vz
			//check energies
			// ekin_anal = mccEnergyDiffElectronElastic(Ekin, angleChi, mass[0], mass[1]);
			// ekinafter = 0.5*(vel[q]*vel[q]+vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2])*mass[0] ;
			// ekindiff = (Ekin-ekinafter);
			// msg(STATUS,"analytic energydiff = %.8f, computed energydiff = %.8f",ekin_anal, ekindiff );

		}
		last_i = i;
	}
	// Special handling of last box to let every particle have posibillity to collide

	R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
	Rp = gsl_rng_uniform_pos(rng); // separate rand num. for prob.
	q = ( (last_i) + floor(R*(((iStop)-last_i))) )*nDims; // particle q collides
	// msg(STATUS,"          electrons ");
	// msg(STATUS,"istop = %i, mccstop = %i total pop size = %i",(iStop),mccStop,(iStop-iStart) );
	// msg(STATUS,"mccstep = %i ,last_i = %i, last_q = %i,prev last_q = %i, last box = %i", mccStepSize,last_i,q,last_q,(iStop-last_q));
	// msg(STATUS,"           ");
	vx = vel[q];
	vy = vel[q+1];
	vz = vel[q+2];
	// need n,n+1,n+2 for x,y,j ?

	double MyCollFreq = mccGetMyCollFreqStatic(mccSigmaElectronElastic,  \
		vx,vy,vz,nt); //prob of coll for particle i

	if (Rp<(MyCollFreq/ *maxfreqElectron)){
		errorcounter += 1;

		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?

		Ekin = 0.5*(vx*vx + vy*vy + vz*vz)*mass[0]; // values stored in population for speed??

		angleChi =  acos((2+Ekin-2*pow((1+Ekin),R))/(Ekin)); // gives nan value if abs(argument) > 1
		anglePhi = 2*PI*R; // set different R?
		angleTheta = acos(vx);

		A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);

		vel[q] = vx*cos(angleChi)+A*(vy*vy + vz*vz); //Vx
		vel[q+1] = vy*cos(angleChi)+A*vz-A*vx*vy; //Vy
		vel[q+2] = vz*cos(angleChi)-A*vy-A*vx*vz; //Vz
	}
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "counted  %i Electron collisions on one MPI node \n",  errorcounter);
	}
	msg(STATUS,"counted  %i Electron collisions on one MPI node", errorcounter);
}

void mccCollideIonStatic(const dictionary *ini, Population *pop, double *Pmax,
	double *maxfreqIon, const gsl_rng *rng, double nt,
	double NvelThermal,double mccSigmaCEX,
	double mccSigmaIonElastic, MpiInfo *mpiInfo){

	// uses static CROSS-Sections, collfreq is proportional to v

	//int nSpecies = pop->nSpecies;
	int nDims = pop->nDims;
	double *vel = pop->vel;
	//double *mass = pop->mass;
	//double velTh = NvelThermal;//(timeStep/stepSize)*NvelThermal; //neutrals
	//msg(STATUS,"neutral thermal vel used in Ion is %f", velTh);
	double R, R1, Rp, Rq;


	double angleChi = 0; //scattering angle in x, y
	double anglePhi = 0; //scattering angle in j
	double angleTheta = 0;
	double A = 0; //scaling Factor used for precomputing (speedup)
	long int q = 0;

	long int errorcounter = 0; // count collisions
	long int errorcounter1 = 0; // count cex collisions

	double vxMW, vyMW, vzMW;

	long int last_i;

	long int iStart = pop->iStart[1];			// *nDims??
	long int iStop = pop->iStop[1];      // make shure theese are actually ions!
	long int NparticleColl = (*Pmax)*(pop->iStop[1]-pop->iStart[1]); // 1 dim ?, Number of particles too collide
	long int mccStepSize = (floor((double)((iStop-iStart))\
	/ (double)(NparticleColl)));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "colliding %i of %i ions \n", (NparticleColl), ((iStop-iStart)));
	}
	///msg(STATUS,"colliding %i of %i ions", NparticleColl, (iStop-iStart));
	//msg(STATUS,"Particles per box to pick one from = %i", (mccStepSize));

	//long int mccStop = iStart + mccStepSize*NparticleColl; //enshure non out of bounds

	//check energies
	// double ekin = 0; //test value
	// double ekin_anal = 0;
	// double ekinafter =0;
	// double ekindiff =0;
	for(long int i=iStart;i<iStop;i+=mccStepSize){  //check this statement #segfault long int p=pStart;p<pStart+Pmax*(pStop-pStart);p++)

		Rp = gsl_rng_uniform_pos(rng); //decides type of coll.
		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
		Rq = gsl_rng_uniform_pos(rng);
		q = (i + floor((Rq*(mccStepSize))) )*nDims ; // particle (q = i*nDims) collides
		//msg(STATUS,"i=%i,  q = %i", i,q );
		// if(q>=iStop*nDims){
		// 	msg(ERROR,"main q = %i out of bounds in Ions, istop*nDims = %i",q,iStop*nDims);
		// }
		vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
		vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
		vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);

		//ekin = (vel[q]*vel[q]+vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2])*pop->mass[1];

		//store
		double vx = vel[q];
		double vy = vel[q];
		double vz = vel[q];

		//transfer to CM-frame
		double vxTran = vx-(vx-vxMW)/2.0;
		double vyTran = vy-(vy-vyMW)/2.0;
		double vzTran = vz- (vz-vzMW)/2.0;

		double MyCollFreq1 = mccGetMyCollFreqStatic(mccSigmaIonElastic,vxTran,
			vyTran,vzTran, nt); //duplicate untill real cross sections are implemented
		double MyCollFreq2 = mccGetMyCollFreqStatic(mccSigmaCEX,vxTran,vyTran,
			vzTran, nt); //prob of coll for particle i
		//msg(STATUS,"is Rp = %f < MyCollFreq/maxfreqElectron = %f", Rp, ((MyCollFreq1+MyCollFreq2)/ *maxfreqIon));
		if (Rp<( (MyCollFreq1+MyCollFreq2)/ *maxfreqIon)){ // MyCollFreq1+MyCollFreq2 defines total coll freq.
			//msg(STATUS,"collided");
			if(Rp < MyCollFreq1/ *maxfreqIon){ // if test slow, but enshures randomness...
				// elastic:
				//msg(STATUS,"elastic");
				errorcounter += 1;

				R1 = gsl_rng_uniform_pos(rng);
				//The scattering happens in center of mass so we use THETA not chi
				angleChi =  (acos(1-2*R)/2.0);

				anglePhi = 2*PI*R1; // set different R?
				angleTheta = acos(vel[q]); //C_M frame
				A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);
				vel[q] = vxTran*cos(angleChi)+A*(vyTran*vyTran + vzTran*vzTran) \
				+ (vx-vxMW)/2.0; //Vx
				vel[q+1] = vyTran*cos(angleChi)+A*vzTran-A*vxTran*vyTran + (vy-vyMW)/2.0; //Vy
				vel[q+2] = vzTran*cos(angleChi)-A*vyTran-A*vxTran*vzTran + (vz-vzMW)/2.0; //Vz
				//check energies
				// ekin_anal += mccEnergyDiffIonElastic(ekin, (angleChi), pop->mass[1], pop->mass[1]);
				// ekinafter = (vel[q]*vel[q]+vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2])*pop->mass[1] ;
				// ekindiff += (ekin-ekinafter);
				// msg(STATUS,"analytic energydiff = %.8f, computed energydiff = %.8f",ekin_anal, ekindiff );
			}else{
				errorcounter += 1;
				errorcounter1 += 1;
				// Charge exchange:
				// flip ion and neutral. Neutral is new ion.
				//msg(STATUS,"ch-ex");
				vel[q] = vxMW; //Vx
				vel[q+1] = vyMW; //Vy
				vel[q+2] = vzMW; //Vz

			}
		}
		last_i = i;
	}


	// Special handling of last box to let every particle have posibillity to collide

	Rp = gsl_rng_uniform_pos(rng); //decides type of coll.
	R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
	Rq = gsl_rng_uniform_pos(rng);
	q = ( (last_i) + floor(Rq*(((iStop)-last_i))) )*nDims; // particle q collides
	// if(q>=iStop*nDims || q<last_q){
	// 	msg(ERROR,"q = %i out of bounds in Ions, istop*nDims = %i, last q = %i",q,iStop*nDims,last_q);
	// }
	// msg(STATUS,"istop = %i, total pop size = %i",(iStop),(iStop-iStart) );
	// msg(STATUS,"mccstep = %i ,last_q = %i, prev last_i = %i, last box = %i", mccStepSize,q,last_i,(iStop-last_i));
	vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
	vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
	vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
	//msg(STATUS,"neutral velx = %f vely = %f velz = %f",vxMW,vyMW,vzMW );
	//store
	double vx = vel[q];
	double vy = vel[q];
	double vz = vel[q];

	//transfer to CM-frame
	double vxTran = vx-(vx-vxMW)/2.0;
	double vyTran = vy-(vy-vyMW)/2.0;
	double vzTran = vz- (vz-vzMW)/2.0;

	double MyCollFreq1 = mccGetMyCollFreqStatic(mccSigmaIonElastic,vxTran,vyTran,vzTran, nt); //duplicate untill real cross sections are implemented
	double MyCollFreq2 = mccGetMyCollFreqStatic(mccSigmaCEX,vxTran,vyTran,vzTran, nt); //prob of coll for particle i


	if (Rp<( (MyCollFreq1+MyCollFreq2)/ *maxfreqIon)){ // MyCollFreq1+MyCollFreq2 defines total coll freq.
		if(Rp < MyCollFreq1/ *maxfreqIon){ // if test slow, but enshures randomness...
			// elastic:
			//msg(STATUS,"elastic");
			errorcounter += 1;

			R1 = gsl_rng_uniform_pos(rng);
			//The scattering happens in center of mass so we use THETA not chi
			angleChi =  (acos(1-2*R)/2.0); //chi is THETA

			anglePhi = 2*PI*R1; // set different R?
			angleTheta = acos(vel[q]); //C_M frame
			A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);
			vel[q] = vxTran*cos(angleChi)+A*(vyTran*vyTran + vzTran*vzTran) \
			+ (vx-vxMW)/2.0; //Vx
			vel[q+1] = vyTran*cos(angleChi)+A*vzTran-A*vxTran*vyTran + (vy-vyMW)/2.0; //Vy
			vel[q+2] = vzTran*cos(angleChi)-A*vyTran-A*vxTran*vzTran + (vz-vzMW)/2.0; //Vz
		}else{
			// Charge exchange:
			// flip ion and neutral. Neutral is new ion.
			errorcounter += 1;
			errorcounter1 += 1;
			//msg(STATUS,"ch-ex");
			vel[q] = vxMW; //Vx
			vel[q+1] = vyMW; //Vy
			vel[q+2] = vzMW; //Vz

		}
	}
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "counted  %i ION collisions on one MPI node, %i as CEX \n", errorcounter,errorcounter1);
	}
	msg(STATUS,"counted  %i ION collisions on one MPI node, %i as CEX", errorcounter,errorcounter1);
}


void mccCollideElectronFunctional(const dictionary *ini, Population *pop,
	double *Pmax,double *maxfreqElectron, const gsl_rng *rng,
	double nt, MpiInfo *mpiInfo,double electron_a,double electron_b){

	// uses static CROSS-Sections, collfreq is proportional to v
	//msg(STATUS,"colliding Electrons");

	int nDims = pop->nDims;
	double *vel = pop->vel;
	double *mass = pop->mass;

	double R = gsl_rng_uniform_pos(rng);
	double Rp = gsl_rng_uniform(rng);

	double angleChi = 0; //scattering angle in x, y
	double anglePhi = 0; //scattering angle in j
	double angleTheta = 0;
	double A = 0; //scaling Factor used for precomputing (speedup)
	long int q = 0;
	double vx, vy, vz;
	long int last_i = 0;

	long int errorcounter = 0;

	double Ekin = 0;
	long int iStart = pop->iStart[0];		// *nDims??
	long int iStop = pop->iStop[0];      // make shure theese are actually electrons!
	long int NparticleColl = (*Pmax)*(pop->iStop[0]-pop->iStart[0]); // 1 dim ?, Number of particles too collide
	long int mccStepSize = floor((double)((iStop-iStart))\
	/ (double)(NparticleColl));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "colliding %i of %i electrons\n", (NparticleColl), ((iStop-iStart)));
	}
	//msg(STATUS,"colliding %i of %i electrons", NparticleColl, (iStop-iStart));

	long int mccStop = iStart + mccStepSize*NparticleColl; //enshure non out of bounds

	for(long int i=iStart;i<iStop;i+=mccStepSize){  //check this statement #segfault long int p=pStart;p<pStart+Pmax*(pStop-pStart);p++)

		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
		Rp = gsl_rng_uniform_pos(rng); // separate rand num. for prob.
		q = ((i + floor(R*mccStepSize))*nDims);

		vx = vel[q];
		vy = vel[q+1];
		vz = vel[q+2];
		// need n,n+1,n+2 for x,y,j ?

		double MyCollFreq = mccGetMyCollFreqFunctional(mccSigmaElectronElasticFunctional,  \
			vx,vy,vz,nt,electron_a,electron_b); //prob of coll for particle i

		if (Rp<(MyCollFreq/ *maxfreqElectron)){
		// Pmax gives the max possible colls. but we will "never" reach this
		// number, so we need to test for each particles probability.
			errorcounter += 1;

			R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?

			Ekin = 0.5*(vx*vx + vy*vy + vz*vz)*mass[0]; // values stored in population for speed??

			double argument = (2+Ekin-2*pow((1+Ekin),R))/(Ekin); // This one has the posibility to be greater than 1!!

			if(sqrt(argument*argument)>1.0){
				double newargument = sqrt(argument*argument)/argument;
				fMsg(ini, "collision", "WARNING: old argument = %.32f new arg = %.64f, R = %.32f \n", argument, newargument,R);
				//msg(WARNING,"old argument = %.32f new arg = %.64f, R = %.32f", argument, newargument,R);
				argument = newargument;
			}
			angleChi =  acos(argument); // gives nan value if abs(argument) > 1

			anglePhi = 2*PI*R; // set different R?
			angleTheta = acos(vx);

			A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);

			vel[q] = vx*cos(angleChi)+A*(vy*vy + vz*vz); //Vx
			vel[q+1] = vy*cos(angleChi)+A*vz-A*vx*vy; //Vy
			vel[q+2] = vz*cos(angleChi)-A*vy-A*vx*vz; //Vz
		}
		last_i = i;
	}
	// Special handling of last box to let every particle have posibillity to collide

	R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
	Rp = gsl_rng_uniform_pos(rng); // separate rand num. for prob.
	q = ( (last_i) + floor(R*(((iStop)-last_i))) )*nDims; // particle q collides

	vx = vel[q];
	vy = vel[q+1];
	vz = vel[q+2];
	// need n,n+1,n+2 for x,y,j ?

	double MyCollFreq = mccGetMyCollFreqFunctional(mccSigmaElectronElasticFunctional,  \
		vx,vy,vz,nt,electron_a,electron_b); //prob of coll for particle i

	if (Rp<(MyCollFreq/ *maxfreqElectron)){
		errorcounter += 1;

		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?

		Ekin = 0.5*(vx*vx + vy*vy + vz*vz)*mass[0]; // values stored in population for speed??

		angleChi =  acos((2+Ekin-2*pow((1+Ekin),R))/(Ekin)); // gives nan value if abs(argument) > 1
		anglePhi = 2*PI*R; // set different R?
		angleTheta = acos(vx);

		A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);

		vel[q] = vx*cos(angleChi)+A*(vy*vy + vz*vz); //Vx
		vel[q+1] = vy*cos(angleChi)+A*vz-A*vx*vy; //Vy
		vel[q+2] = vz*cos(angleChi)-A*vy-A*vx*vz; //Vz
	}
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "counted  %i Electron collisions on one MPI node \n",  errorcounter);
	}
	msg(STATUS,"counted  %i Electron collisions on one MPI node", errorcounter);
}

void mccCollideIonFunctional(const dictionary *ini, Population *pop, double *Pmax,
	double *maxfreqIon, const gsl_rng *rng, double nt,
	double NvelThermal, MpiInfo *mpiInfo,double CEX_a,double CEX_b,
	double ion_elastic_a,double ion_elastic_b){

	// uses static CROSS-Sections, collfreq is proportional to v

	//int nSpecies = pop->nSpecies;
	int nDims = pop->nDims;
	double *vel = pop->vel;
	//double *mass = pop->mass;
	//double velTh = NvelThermal;//(timeStep/stepSize)*NvelThermal; //neutrals
	//msg(STATUS,"neutral thermal vel used in Ion is %f", velTh);
	double R, R1, Rp, Rq;

	double angleChi = 0; //scattering angle in x, y
	double anglePhi = 0; //scattering angle in j
	double angleTheta = 0;
	double A = 0; //scaling Factor used for precomputing (speedup)
	long int q = 0;
	long int last_i = 0;

	long int errorcounter = 0; // count collisions
	long int errorcounter1 = 0; // count cex collisions

	double vxMW, vyMW, vzMW;

	long int iStart = pop->iStart[1];			// *nDims??
	long int iStop = pop->iStop[1];      // make shure theese are actually ions!
	long int NparticleColl = (*Pmax)*(pop->iStop[1]-pop->iStart[1]); // 1 dim ?, Number of particles too collide
	long int mccStepSize = floor((double)((iStop-iStart))\
	/ (double)(NparticleColl));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "colliding %i of %i ions \n", (NparticleColl), ((iStop-iStart)));
	}
	///msg(STATUS,"colliding %i of %i ions", NparticleColl, (iStop-iStart));
	//msg(STATUS,"Particles per box to pick one from = %i", (mccStepSize));

	//long int mccStop = iStart + mccStepSize*NparticleColl; //enshure non out of bounds

	for(long int i=iStart;i<iStop;i+=mccStepSize){  //check this statement #segfault long int p=pStart;p<pStart+Pmax*(pStop-pStart);p++)

		Rp = gsl_rng_uniform_pos(rng); //decides type of coll.
		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
		Rq = gsl_rng_uniform_pos(rng);
		q = (i + floor(Rq*mccStepSize))*nDims; // particle q collides
		//msg(STATUS,"R=%g, p = %i, q = %i", R,p,q );

		vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
		vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
		vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);

		//store
		double vx = vel[q];
		double vy = vel[q];
		double vz = vel[q];

		//transfer to CM-frame
		double vxTran = vx-(vx-vxMW)/2.0;
		double vyTran = vy-(vy-vyMW)/2.0;
		double vzTran = vz- (vz-vzMW)/2.0;

		double MyCollFreq1 = mccGetMyCollFreqFunctional(mccSigmaIonElasticFunctional,vxTran,vyTran,vzTran,nt,ion_elastic_a,ion_elastic_b); //prob of coll for particle i
		double MyCollFreq2 = mccGetMyCollFreqFunctional(mccSigmaCEXFunctional,vxTran,vyTran,vzTran,nt,CEX_a,CEX_b); //duplicate untill real cross sections are implemented


		//msg(STATUS,"is Rp = %f < MyCollFreq/maxfreqElectron = %f", Rp, ((MyCollFreq1+MyCollFreq2)/ *maxfreqIon));
		if (Rp<( (MyCollFreq1+MyCollFreq2)/ *maxfreqIon)){ // MyCollFreq1+MyCollFreq2 defines total coll freq.
			if(Rp < MyCollFreq1/ *maxfreqIon){ // if test slow, but enshures randomness...
				// elastic:
				//msg(STATUS,"elastic");
				errorcounter += 1;

				R1 = gsl_rng_uniform_pos(rng);
				//The scattering happens in center of mass so we use THETA not chi
				angleChi =  (acos(1-2*R)/2.0);

				anglePhi = 2*PI*R1; // set different R?
				angleTheta = acos(vel[q]); //C_M frame
				A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);
				vel[q] = vxTran*cos(angleChi)+A*(vyTran*vyTran + vzTran*vzTran) \
				+ (vx-vxMW)/2.0; //Vx
				vel[q+1] = vyTran*cos(angleChi)+A*vzTran-A*vxTran*vyTran + (vy-vyMW)/2.0; //Vy
				vel[q+2] = vzTran*cos(angleChi)-A*vyTran-A*vxTran*vzTran + (vz-vzMW)/2.0; //Vz
			}else{
				errorcounter += 1;
				errorcounter1 += 1;
				// Charge exchange:
				// flip ion and neutral. Neutral is new ion.
				//msg(STATUS,"ch-ex");
				vel[q] = vxMW; //Vx
				vel[q+1] = vyMW; //Vy
				vel[q+2] = vzMW; //Vz

			}
		}
		last_i = i;
	}
	// Special handling of last box to let every particle have posibillity to collide

	Rp = gsl_rng_uniform_pos(rng); //decides type of coll.
	R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
	Rq = gsl_rng_uniform_pos(rng);
	q = ( (last_i) + floor(Rq*(((iStop)-last_i))) )*nDims; // particle q collides

	vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
	vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
	vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
	//msg(STATUS,"neutral velx = %f vely = %f velz = %f",vxMW,vyMW,vzMW );
	//store
	double vx = vel[q];
	double vy = vel[q];
	double vz = vel[q];

	//transfer to CM-frame
	double vxTran = vx-(vx-vxMW)/2.0;
	double vyTran = vy-(vy-vyMW)/2.0;
	double vzTran = vz- (vz-vzMW)/2.0;

	double MyCollFreq1 = mccGetMyCollFreqFunctional(mccSigmaIonElasticFunctional,vxTran,vyTran,vzTran,nt,ion_elastic_a,ion_elastic_b); //prob of coll for particle i
	double MyCollFreq2 = mccGetMyCollFreqFunctional(mccSigmaCEXFunctional,vxTran,vyTran,vzTran,nt,CEX_a,CEX_b); //duplicate untill real cross sections are implemented


	if (Rp<( (MyCollFreq1+MyCollFreq2)/ *maxfreqIon)){ // MyCollFreq1+MyCollFreq2 defines total coll freq.
		if(Rp < MyCollFreq1/ *maxfreqIon){ // if test slow, but enshures randomness...
			// elastic:
			//msg(STATUS,"elastic");
			errorcounter += 1;

			R1 = gsl_rng_uniform_pos(rng);
			//The scattering happens in center of mass so we use THETA not chi
			angleChi =  (acos(1-2*R)/2.0); //chi is THETA

			anglePhi = 2*PI*R1; // set different R?
			angleTheta = acos(vel[q]); //C_M frame
			A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);
			vel[q] = vxTran*cos(angleChi)+A*(vyTran*vyTran + vzTran*vzTran) \
			+ (vx-vxMW)/2.0; //Vx
			vel[q+1] = vyTran*cos(angleChi)+A*vzTran-A*vxTran*vyTran + (vy-vyMW)/2.0; //Vy
			vel[q+2] = vzTran*cos(angleChi)-A*vyTran-A*vxTran*vzTran + (vz-vzMW)/2.0; //Vz
		}else{
			// Charge exchange:
			// flip ion and neutral. Neutral is new ion.
			errorcounter += 1;
			errorcounter1 += 1;
			//msg(STATUS,"ch-ex");
			vel[q] = vxMW; //Vx
			vel[q+1] = vyMW; //Vy
			vel[q+2] = vzMW; //Vz

		}
	}
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "counted  %i ION collisions on one MPI node, %i as CEX \n", errorcounter,errorcounter1);
	}
	msg(STATUS,"counted  %i ION collisions on one MPI node, %i as CEX", errorcounter,errorcounter1);
}



void mccCollideElectronConstantFrq(const dictionary *ini, Population *pop,
	double *Pmax,double *maxfreqElectron, const gsl_rng *rng,double NvelThermal,
	double nt, MpiInfo *mpiInfo){

	//  collfreq is constant
	//msg(STATUS,"colliding Electrons");

	int nDims = pop->nDims;
	double *vel = pop->vel;
	double *mass = pop->mass;

	double R = gsl_rng_uniform_pos(rng);

	double angleChi = 0; //scattering angle in x, y
	double anglePhi = 0; //scattering angle in j
	double angleTheta = 0;
	double A = 0; //scaling Factor used for precomputing (speedup)
	long int q = 0;
	double vx, vy, vz;
	double vxMW, vyMW, vzMW;
	double last_i = 0;

	double ekin_anal = 0;
	double ekinafter = 0;
	double ekindiff =0;

	long int errorcounter = 0;

	double Ekin = 0;
	long int iStart = pop->iStart[0];		// *nDims??
	long int iStop = pop->iStop[0];      // make shure theese are actually electrons!
	long int NparticleColl = (*Pmax)*(pop->iStop[0]-pop->iStart[0]); // 1 dim ?, Number of particles too collide
	long int mccStepSize = floor((double)((iStop-iStart))\
	/ (double)(NparticleColl));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "colliding %i of %i electrons\n", (NparticleColl), ((iStop-iStart)));
	}
	//msg(STATUS,"colliding %i of %i electrons", NparticleColl, (iStop-iStart));

	//long int mccStop = iStart + mccStepSize*NparticleColl; //enshure non out of bounds

	for(long int i=iStart;i<iStop;i+=mccStepSize){  //check this statement #segfault long int p=pStart;p<pStart+Pmax*(pStop-pStart);p++)

		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
		q = ((i + floor(R*mccStepSize))*nDims);

		vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
		vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
		vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);

		//transfer to CM-frame
		double vxTran = vel[q]-(vel[q]-vxMW)/2.0;
		double vyTran = vel[q+1]-(vel[q+1]-vyMW)/2.0;
		double vzTran = vel[q+2]- (vel[q+2]-vzMW)/2.0;

		vx = vel[q];
		vy = vel[q+1];
		vz = vel[q+2];
		// need n,n+1,n+2 for x,y,j ?


		// Pmax gives the max possible colls. but we will "never" reach this
		// number, so we need to test for each particles probability.
		errorcounter += 1;

		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?

		Ekin = 0.5*(vx*vx + vy*vy + vz*vz)*mass[0]; // values stored in population for speed??

		double argument = (2+Ekin-2*pow((1+Ekin),R))/(Ekin); // This one has the posibility to be greater than 1!!

		if(sqrt(argument*argument)>1.0){
			double newargument = sqrt(argument*argument)/argument;
			fMsg(ini, "collision", "WARNING: old argument = %.32f new arg = %.64f, R = %.32f \n", argument, newargument,R);
			//msg(WARNING,"old argument = %.32f new arg = %.64f, R = %.32f", argument, newargument,R);
			argument = newargument;
		}
		angleChi =  acos(argument); // gives nan value if abs(argument) > 1

		anglePhi = 2*PI*R; // set different R?
		angleTheta = acos(vx);

		A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);

		vel[q] = vx*cos(angleChi)+A*(vy*vy + vz*vz); //Vx
		vel[q+1] = vy*cos(angleChi)+A*vz-A*vx*vy; //Vy
		vel[q+2] = vz*cos(angleChi)-A*vy-A*vx*vz; //Vz

		//check energies
		ekin_anal += mccEnergyDiffElectronElastic(Ekin, angleChi, mass[0], mass[1]);
		ekinafter = 0.5*(vel[q]*vel[q]+vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2])*mass[0] ;
		ekindiff += (Ekin-ekinafter);
		//msg(STATUS,"analytic energydiff = %.8f, computed energydiff = %.8f",ekin_anal, ekindiff );


		last_i = i;
	}

	// Special handling of last box to let every particle have posibillity to collide

	R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
	q = ( (last_i) + floor(R*(((iStop)-last_i))) )*nDims; // particle q collides

	vx = vel[q];
	vy = vel[q+1];
	vz = vel[q+2];
	// need n,n+1,n+2 for x,y,j ?
	errorcounter += 1;
	R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?

	Ekin = 0.5*(vx*vx + vy*vy + vz*vz)*mass[0]; // values stored in population for speed??

	angleChi =  acos((2+Ekin-2*pow((1+Ekin),R))/(Ekin)); // gives nan value if abs(argument) > 1
	anglePhi = 2*PI*R; // set different R?
	angleTheta = acos(vx);

	A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);

	vel[q] = vx*cos(angleChi)+A*(vy*vy + vz*vz); //Vx
	vel[q+1] = vy*cos(angleChi)+A*vz-A*vx*vy; //Vy
	vel[q+2] = vz*cos(angleChi)-A*vy-A*vx*vz; //Vz

	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "counted  %i Electron collisions on one MPI node \n",  errorcounter);
	}
	msg(STATUS,"counted  %i Electron collisions on one MPI node", errorcounter);
}

void mccCollideIonConstantFrq(const dictionary *ini, Population *pop,
	double *Pmax, const gsl_rng *rng,
	double NvelThermal,double *collFrqIonElastic,
	double *collFrqIonCEX, MpiInfo *mpiInfo){

	// uses constant collfreq fixed number of colls per dt

	//int nSpecies = pop->nSpecies;
	int nDims = pop->nDims;
	double *vel = pop->vel;
	//double velTh = NvelThermal;//(timeStep/stepSize)*NvelThermal; //neutrals
	//msg(STATUS,"neutral thermal vel used in Ion is %f", velTh);
	double R, R1, Rp, Rq;

	double angleChi = 0; //scattering angle in x, y
	double anglePhi = 0; //scattering angle in j
	double angleTheta = 0;
	double A = 0; //scaling Factor used for precomputing (speedup)
	double B = 0;
	long int q = 0;
	long int last_i = 0;

	long int errorcounter = 0; // count collisions
	long int errorcounter1 = 0; // count cex collisions

	double vxMW, vyMW, vzMW;

	double MyCollFreq1 = *collFrqIonElastic;
	double MyCollFreq2 = *collFrqIonCEX;
	double maxfreqIon = MyCollFreq1+MyCollFreq2;

	long int iStart = pop->iStart[1];			// *nDims??
	long int iStop = pop->iStop[1];      // make shure theese are actually ions!
	long int NparticleColl = (*Pmax)*(pop->iStop[1]-pop->iStart[1]); // 1 dim ?, Number of particles too collide
	long int mccStepSize = floor((double)((iStop-iStart))\
	/ (double)(NparticleColl));
	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "colliding %i of %i ions \n", (NparticleColl), ((iStop-iStart)));
	}
	///msg(STATUS,"colliding %i of %i ions", NparticleColl, (iStop-iStart));
	//msg(STATUS,"Particles per box to pick one from = %i", (mccStepSize));

	//long int mccStop = iStart + mccStepSize*NparticleColl; //enshure non out of bounds

	//check energies
	double ekin = 0; //test value
	double ekin_anal = 0;
	double ekinafter =0;
	double ekindiff =0;

	for(long int i=iStart;i<iStop;i+=mccStepSize){  //check this statement #segfault long int p=pStart;p<pStart+Pmax*(pStop-pStart);p++)

		Rp = gsl_rng_uniform_pos(rng); //decides type of coll.
		R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
		Rq = gsl_rng_uniform_pos(rng);
		q = (i + floor(Rq*mccStepSize))*nDims; // particle q collides
		//msg(STATUS,"mass = %f, i = %i, q = %i",pop->mass[1],i*nDims,q );

		vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
		vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
		vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);

		//store
		// unit velocities
		double velchange = 0.0;// change of energy incident particle
		double velsquare = sqrt(vel[q]*vel[q]+vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2]);
		double vx = vel[q]/velsquare;
		double vy = vel[q+1]/velsquare;
		double vz = vel[q+2]/velsquare;
		ekin = 0.5*(velsquare*velsquare)*pop->mass[1] ;




		//transfer to CM-frame
		double vxTran = vx-vxMW;
		double vyTran = vy-vyMW;
		double vzTran = vz-vzMW;


		//msg(STATUS,"is Rp = %f < MyCollFreq/maxfreqElectron = %f", Rp, ((MyCollFreq1+MyCollFreq2)/ *maxfreqIon));
		if(Rp < MyCollFreq1/ maxfreqIon){ // if test slow, but enshures randomness...
			// elastic:
			//msg(STATUS,"elastic");
			errorcounter += 1;

			R1 = gsl_rng_uniform_pos(rng);
			//The scattering happens in center of mass so we use THETA not chi
			angleChi =  sqrt(acos(1-R));
			velchange = sqrt(velsquare*velsquare*(cos(angleChi)*cos(angleChi)));
			anglePhi = 2*PI*R1; // set different R?
			angleTheta = acos(vx); //C_M frame
			if (angleTheta > 3.15){
				msg(ERROR, "angleTheta = %f",angleTheta);
			}
			A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);
			B = (sin(angleChi)*cos(anglePhi))/sin(angleTheta);
			vel[q] = (vx*cos(angleChi)+B*(vy*vy + vz*vz) )*velchange; //Vx
			vel[q+1] = (vy*cos(angleChi)+A*vzTran-B*vx*vy)*velchange; //Vy
			vel[q+2] = (vz*cos(angleChi)-A*vy-B*vx*vz)*velchange; //Vz
			double temppp = sqrt((vx*cos(angleChi)+B*(vy*vy + vz*vz) )*(vx*cos(angleChi)+B*(vy*vy + vz*vz) )+ (vy*cos(angleChi)+A*vzTran-B*vx*vy)*(vy*cos(angleChi)+A*vzTran-B*vx*vy) +(vz*cos(angleChi)-A*vy-B*vx*vz)*(vz*cos(angleChi)-A*vy-B*vx*vz));
			//msg(STATUS,"size of change x =%f, y = %f, z = %f, squared = %f, ",(vx*cos(angleChi)+B*(vy*vy + vz*vz) ), (vy*cos(angleChi)+A*vzTran-B*vx*vy),(vz*cos(angleChi)-A*vy-B*vx*vz) ,temppp);
			//check energies
			ekin_anal += mccEnergyDiffIonElastic(ekin, (2.0*angleChi), pop->mass[1], pop->mass[1]);
			ekinafter = 0.5*(vel[q]*vel[q]+vel[q+1]*vel[q+1]+vel[q+2]*vel[q+2])*pop->mass[1] ;
			ekindiff += (ekin-ekinafter);
			msg(STATUS," analytic energydiff = %.8f, computed energydiff = %.32f",ekin_anal, ekindiff );

		}else{
			errorcounter += 1;
			errorcounter1 += 1;
			// Charge exchange:
			// flip ion and neutral. Neutral is new ion.
			//msg(STATUS,"ch-ex");
			vel[q] = vxMW; //Vx
			vel[q+1] = vyMW; //Vy
			vel[q+2] = vzMW; //Vz
		}
		last_i = i;
	}


	// Special handling of last box to let every particle have posibillity to collide

	Rp = gsl_rng_uniform_pos(rng); //decides type of coll.
	R = gsl_rng_uniform_pos(rng); // New random number per particle. maybe print?
	Rq = gsl_rng_uniform_pos(rng);
	q = ( (last_i) + floor(Rq*(((iStop)-last_i))) )*nDims; // particle q collides

	// msg(STATUS,"istop = %i, total pop size = %i",(iStop),(iStop-iStart) );
	// msg(STATUS,"mccstep = %i ,last_q = %i, prev last_i = %i, last box = %i", mccStepSize,q,last_i,(iStop-last_i));

	vxMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal); //maxwellian dist?
	vyMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
	vzMW = gsl_ran_gaussian_ziggurat(rng,NvelThermal);
	//msg(STATUS,"neutral velx = %f vely = %f velz = %f",vxMW,vyMW,vzMW );

	//store
	double vx = vel[q];
	double vy = vel[q];
	double vz = vel[q];

	//transfer to CM-frame
	double vxTran = vx-(vx-vxMW)/2.0;
	double vyTran = vy-(vy-vyMW)/2.0;
	double vzTran = vz- (vz-vzMW)/2.0;
	//
	if(Rp < MyCollFreq1/ maxfreqIon){ // if test slow, but enshures randomness...
		// elastic:
		//msg(STATUS,"elastic");
		errorcounter += 1;

		R1 = gsl_rng_uniform_pos(rng);
		//The scattering happens in center of mass so we use THETA not chi
		angleChi =  (acos(1-2*R)/2.0);

		anglePhi = 2*PI*R1; // set different R?
		angleTheta = acos(vel[q]); //C_M frame
		A = (sin(angleChi)*sin(anglePhi))/sin(angleTheta);
		vel[q] = vxTran*cos(angleChi)+A*(vyTran*vyTran + vzTran*vzTran) \
		+ (vx-vxMW)/2.0; //Vx
		vel[q+1] = vyTran*cos(angleChi)+A*vzTran-A*vxTran*vyTran + (vy-vyMW)/2.0; //Vy
		vel[q+2] = vzTran*cos(angleChi)-A*vyTran-A*vxTran*vzTran + (vz-vzMW)/2.0; //Vz
	}else{
		// Charge exchange:
		// flip ion and neutral. Neutral is new ion.
		errorcounter += 1;
		errorcounter1 += 1;
		//msg(STATUS,"ch-ex");
		vel[q] = vxMW; //Vx
		vel[q+1] = vyMW; //Vy
		vel[q+2] = vzMW; //Vz
	}

	if(mpiInfo->mpiRank==0){
		fMsg(ini, "collision", "counted  %i ION collisions on one MPI node, %i as CEX \n", errorcounter,errorcounter1);
	}
	msg(STATUS,"counted  %i ION collisions on one MPI node, %i as CEX", errorcounter,errorcounter1);
}



/*************************************************
*		Inline functions
************************************************/



/*************************************************
*		DEFINITIONS
************************************************/



/*************************************************
* 		ALLOCATIONS
* 		DESTRUCTORS
************************************************/


/*******************************************************
*			VARIOUS COMPUTATIONS (RESIDUAL)
******************************************************/



/*************************************************
*		RUNS
************************************************/

funPtr mccTestMode_set(dictionary *ini){
	//test sanity here!
	mccSanity(ini,"mccTestMode",2);
	return mccTestMode;
}



void mccTestMode(dictionary *ini){

	msg(STATUS, "start mcc Test Mode");

	/*
	* SELECT METHODS
	*/
	void (*acc)()   = select(ini,"methods:acc",	puAcc3D1_set,
	puAcc3D1KE_set,
	puAccND1_set,
	puAccND1KE_set,
	puAccND0_set,
	puAccND0KE_set,
	puBoris3D1_set,
	puBoris3D1KE_set,
	puBoris3D1KETEST_set);

	void (*distr)() = select(ini,"methods:distr",	puDistr3D1split_set,
													puDistr3D1_set,
													puDistrND1_set,
													puDistrND0_set);

	void (*extractEmigrants)() = select(ini,"methods:migrate",	puExtractEmigrants3D_set,
	puExtractEmigrantsND_set);

	void (*solverInterface)()	= select(ini,	"methods:poisson",
												mgSolver_set
												//sSolver_set
												);


	//
	void (*solve)() = NULL;
	void *(*solverAlloc)() = NULL;
	void (*solverFree)() = NULL;
	solverInterface(&solve, &solverAlloc, &solverFree);


	/*
	* INITIALIZE PINC VARIABLES
	*
	*done in "main" for "regular mode"
	*/
	Units *units=uAlloc(ini);
	uNormalize(ini, units);
	// normalize mcc input variables
	//must be done after uNormalize, and before defining mcc vars
	mccNormalize(units,ini);

	MpiInfo *mpiInfo = gAllocMpi(ini);
	Population *pop = pAlloc(ini);
	Grid *phi = gAlloc(ini, SCALAR);
	Grid *E   = gAlloc(ini, VECTOR);
	Grid *rho = gAlloc(ini, SCALAR);
	Grid *rho_e = gAlloc(ini, SCALAR);
	Grid *rho_i = gAlloc(ini, SCALAR);
	void *solver = solverAlloc(ini, rho, phi);


	/*
	* mcc specific variables
	*/



	double PmaxElectron = 0;
	double maxfreqElectron = 0;
	double PmaxIon = 0;
	double maxfreqIon = 0;
	int nSpecies = pop->nSpecies;
	double nt = iniGetDouble(ini,"collisions:numberDensityNeutrals"); //constant for now
	double mccTimestep = iniGetDouble(ini,"time:timeStep");
	//double mccStepSize = iniGetDouble(ini,"grid:stepSize");
	//double frequency = iniGetDouble(ini,"collisions:collisionFrequency"); // for static Pmax...
	double NvelThermal = iniGetDouble(ini,"collisions:thermalVelocityNeutrals");
	double StaticSigmaCEX = iniGetDouble(ini,"collisions:sigmaCEX");
	double StaticSigmaIonElastic = iniGetDouble(ini,"collisions:sigmaIonElastic");
	double StaticSigmaElectronElastic = iniGetDouble(ini,"collisions:sigmaElectronElastic");
	//if using constant collision frquency
	double collFrqElectronElastic = iniGetDouble(ini,"collisions:collFrqElectronElastic");
	double collFrqIonElastic = iniGetDouble(ini,"collisions:collFrqIonElastic");
	double collFrqCEX = iniGetDouble(ini,"collisions:collFrqCEX");

	double CEX_a = iniGetDouble(ini,"collisions:CEX_a");
	double CEX_b = iniGetDouble(ini,"collisions:CEX_b");
	double ion_elastic_a = iniGetDouble(ini,"collisions:ion_elastic_a");
	double ion_elastic_b = iniGetDouble(ini,"collisions:ion_elastic_b");
	double electron_a = iniGetDouble(ini,"collisions:electron_a");
	double electron_b = iniGetDouble(ini,"collisions:electron_b");

	// using Boris algo
	double *S = (double*)malloc((3)*(nSpecies)*sizeof(double));
	double *T = (double*)malloc((3)*(nSpecies)*sizeof(double));

	// Creating a neighbourhood in the rho to handle migrants
	gCreateNeighborhood(ini, mpiInfo, rho);

	// Setting Boundary slices
	gSetBndSlices(phi, mpiInfo);

	//Set mgSolve
	//MgAlgo mgAlgo = getMgAlgo(ini);

	// Random number seeds
	gsl_rng *rngSync = gsl_rng_alloc(gsl_rng_mt19937);
	gsl_rng *rng = gsl_rng_alloc(gsl_rng_mt19937);
	gsl_rng_set(rng,mpiInfo->mpiRank+1); // Seed needs to be >=1

	// /*
	// * PREPARE FILES FOR WRITING
	// */
	// int rank = phi->rank;
	// double *denorm = malloc((rank-1)*sizeof(*denorm));
	// double *dimen = malloc((rank-1)*sizeof(*dimen));
	//
	// for(int d = 1; d < rank;d++) denorm[d-1] = 1.;
	// for(int d = 1; d < rank;d++) dimen[d-1] = 1.;

	/*
	 * PREPARE FILES FOR WRITING
	 */
	pOpenH5(ini, pop, units, "pop");
	gOpenH5(ini, rho, mpiInfo, units, units->chargeDensity, "rho");
	gOpenH5(ini, rho_e, mpiInfo, units, units->chargeDensity, "rho_e");
	gOpenH5(ini, rho_i, mpiInfo, units, units->chargeDensity, "rho_i");
	gOpenH5(ini, phi, mpiInfo, units, units->potential, "phi");
	gOpenH5(ini, E,   mpiInfo, units, units->eField, "E");
  // oOpenH5(ini, obj, mpiInfo, units, 1, "test");
  // oReadH5(obj, mpiInfo);

	hid_t history = xyOpenH5(ini,"history");
	pCreateEnergyDatasets(history,pop);

	// Add more time series to history if you want
	// xyCreateDataset(history,"/group/group/dataset");

	// free(denorm);
	// free(dimen);

	/*
	* INITIAL CONDITIONS
	*/

	//msg(STATUS, "constants = %f, %f", velThermal[0], velThermal[1]);
	// Initalize particles
	pPosLattice(ini, pop, mpiInfo);
	//pPosUniform(ini, pop, mpiInfo, rngSync);
	//pVelZero(pop);
	//pVelConstant(ini, pop, velThermal[0], velThermal[1]); //constant values for vel.
	pVelMaxwell(ini, pop, rng);
	double maxVel = iniGetDouble(ini,"population:maxVel");

	// Perturb particles
	// pPosPerturb(ini, pop, mpiInfo);

	// Migrate those out-of-bounds due to perturbation
	extractEmigrants(pop, mpiInfo);
	puMigrate(pop, mpiInfo, rho);

	/*
	* compute initial half-step
	*/

	// Get initial charge density
	distr(pop, rho,rho_e,rho_i); //two species
	gHaloOp(addSlice, rho, mpiInfo, FROMHALO);
	gHaloOp(addSlice, rho_e, mpiInfo, FROMHALO);
	gHaloOp(addSlice, rho_i, mpiInfo, FROMHALO);

	// Get initial E-field
	//solve(mgAlgo, mgRho, mgPhi, mgRes, mpiInfo);
	solve(solver, rho, phi, mpiInfo);
	gFinDiff1st(phi, E);
	gHaloOp(setSlice, E, mpiInfo, TOHALO);
	gMul(E, -1.);

	// add External E
	puAddEext(ini, pop, E);

	// Advance velocities half a step
	gMul(E, 0.5);
	puGet3DRotationParameters(ini, T, S, 0.5);
	acc(pop, E, T, S);
	gMul(E, 2.0);
	puGet3DRotationParameters(ini, T, S, 1.0);

	//Write initial h5 files
	//gWriteH5(E, mpiInfo, 0.0);
	gWriteH5(rho, mpiInfo, 0.0);
	gWriteH5(rho_e, mpiInfo, 0.0);
	gWriteH5(rho_i, mpiInfo, 0.0);
	gWriteH5(phi, mpiInfo, 0.0);
	//pWriteH5(pop, mpiInfo, 0.0, 0.5);
	pWriteEnergy(history,pop,0.0);


	//msg(STATUS, "Pmax for Electrons is %f",PmaxElectron);
	//msg(STATUS, "Pmax for Ions is %f",PmaxIon);

	/*
	* TIME LOOP
	*/

	Timer *t = tAlloc(mpiInfo->mpiRank);

	// n should start at 1 since that's the timestep we have after the first
	// iteration (i.e. when storing H5-files).
	int nTimeSteps = iniGetInt(ini,"time:nTimeSteps");
	for(int n = 1; n <= nTimeSteps; n++){
		if(mpiInfo->mpiRank==0){
			fMsg(ini, "collision", "\n Computing time-step %i \n", n);
		}
		msg(STATUS,"Computing time-step %i of %i",n,nTimeSteps);
		//MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary

		// Check that no particle moves beyond a cell (mostly for debugging)
		//pVelAssertMax(pop,maxVel);

		tStart(t);


		// Move particles
		//msg(STATUS, "moving particles");
		puMove(pop);
		// oRayTrace(pop, obj);


		// Migrate particles (periodic boundaries)
		//msg(STATUS, "extracting emigrants");
		extractEmigrants(pop, mpiInfo);
		//msg(STATUS, "Migrating particles");
		puMigrate(pop, mpiInfo, rho);

		/*
		*   Collisions
		*/

		//mccCollideElectron_debug(ini,pop, &PmaxElectron, &maxfreqElectron, rng, mccTimestep, nt,DebyeLength); //race conditions?????
		//mccCollideIon_debug(ini, pop, &PmaxIon, &maxfreqIon, rng, mccTimestep, nt,DebyeLength);


		// mccGetPmaxElectronStatic(ini,StaticSigmaElectronElastic, mccTimestep, nt,
		// 	&PmaxElectron, &maxfreqElectron,pop,mpiInfo);
		// mccGetPmaxIonStatic(ini,StaticSigmaCEX, StaticSigmaIonElastic, mccTimestep,
		// 	nt, &PmaxIon, &maxfreqIon, pop,mpiInfo);
		//
		// mccCollideIonStatic(ini, pop, &PmaxIon, &maxfreqIon, rng,
		// 	nt,NvelThermal,StaticSigmaCEX,StaticSigmaIonElastic,
		// 	mpiInfo);
		// mccCollideElectronStatic(ini, pop, &PmaxElectron,
		// 	&maxfreqElectron, rng, nt,StaticSigmaElectronElastic,
		// 	mpiInfo);

		mccGetPmaxElectronConstantFrq(ini, &PmaxElectron,
			&collFrqElectronElastic,pop,mpiInfo);
		mccGetPmaxIonConstantFrq(ini, &PmaxIon, &collFrqIonElastic,
			&collFrqCEX, pop,mpiInfo);


		mccCollideElectronConstantFrq(ini, pop,&PmaxElectron,&maxfreqElectron,
			rng,NvelThermal,nt,mpiInfo);
		mccCollideIonConstantFrq(ini, pop,&PmaxIon, rng,NvelThermal,
			&collFrqIonElastic,&collFrqCEX, mpiInfo);

		// mccGetPmaxElectronFunctional(ini,
		// 		mccTimestep,nt,&PmaxElectron, &maxfreqElectron,pop,
		// 		mpiInfo,electron_a, electron_b);
		// mccGetPmaxIonFunctional(ini,
		// 		mccTimestep, nt, &PmaxIon, &maxfreqIon, pop,
		// 		mpiInfo,CEX_a,CEX_b, ion_elastic_a, ion_elastic_b);

		// mccCollideElectronFunctional(ini, pop,
		// 	&PmaxElectron, &maxfreqElectron, rng, nt, mpiInfo,
		// 	electron_a, electron_b);
		// mccCollideIonFunctional(ini, pop, &PmaxIon, &maxfreqIon,
		// 	rng, nt, NvelThermal, mpiInfo,CEX_a,CEX_b, ion_elastic_a,
		// 	ion_elastic_b);

		/*
		*   Collisions
		*/

		//MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary

		//MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary
		// Check that no particle resides out-of-bounds (just for debugging)
		//msg(STATUS, "checking particles out of bounds");
		pPosAssertInLocalFrame(pop, rho);

		//MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary
		//-----------------------------
		// Compute charge density
		//msg(STATUS, "computing charge density");
		distr(pop, rho,rho_e,rho_i); //two species

		//MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary

		//msg(STATUS, "MPI exchanging density");
		gHaloOp(addSlice, rho, mpiInfo, FROMHALO);
		gHaloOp(addSlice, rho_e, mpiInfo, FROMHALO);
		gHaloOp(addSlice, rho_i, mpiInfo, FROMHALO);

		//---------------------------
		//msg(STATUS, "gAssertNeutralGrid");
		gAssertNeutralGrid(rho, mpiInfo);

		// Compute electric potential phi
		//msg(STATUS, "Compute electric potential phi");
		//solve(mgAlgo, mgRho, mgPhi, mgRes, mpiInfo);
		solve(solver, rho, phi, mpiInfo);
		//msg(STATUS, "gAssertNeutralGrid");
		gAssertNeutralGrid(phi, mpiInfo);

		// Compute E-field
		//msg(STATUS, "compute E field");
		gFinDiff1st(phi, E);
		//msg(STATUS, "MPI exchanging E");
		gHaloOp(setSlice, E, mpiInfo, TOHALO);
		//msg(STATUS, "gMul( E, -1)");
		gMul(E, -1.);

		//msg(STATUS, "gAssertNeutralGrid");
		gAssertNeutralGrid(E, mpiInfo);
		// Apply external E
		// gAddTo(Ext);
		//gZero(E); //temporary test
		puAddEext(ini, pop, E);

		// Accelerate particle and compute kinetic energy for step n
		//msg(STATUS, "Accelerate particles");

		acc(pop, E, T, S);
		tStop(t);

		// Sum energy for all species
		//msg(STATUS, "sum kinetic energy");
		pSumKinEnergy(pop); // kin_energy per species to pop
		//msg(STATUS, "compute pot energy");
		// Compute potential energy for step n
		gPotEnergy(rho,phi,pop);
		//if( n> 40000 && n< 59500 && n%100 == 0){
		//	msg(STATUS, "writing over a given timestep to file");
			// Example of writing another dataset to history.xy.h5
			// xyWrite(history,"/group/group/dataset",(double)n,value,MPI_SUM);
			//Write h5 files
			//gWriteH5(E, mpiInfo, (double) n);
			//gWriteH5(rho, mpiInfo, (double) n);
		//	gWriteH5(rho, mpiInfo, (double) n);
		//	gWriteH5(rho_e, mpiInfo, (double) n);
		//	gWriteH5(rho_i, mpiInfo, (double) n);
		//	gWriteH5(phi, mpiInfo, (double) n);
			//pWriteH5(pop, mpiInfo, (double) n, (double)n+0.5);
		//}
		if( n>= 100000 && n%100 == 0){
			// Example of writing another dataset to history.xy.h5
			// xyWrite(history,"/group/group/dataset",(double)n,value,MPI_SUM);
			//Write h5 files
			//gWriteH5(E, mpiInfo, (double) n);
			gWriteH5(rho, mpiInfo, (double) n);
			gWriteH5(rho_e, mpiInfo, (double) n);
			gWriteH5(rho_i, mpiInfo, (double) n);
			gWriteH5(phi, mpiInfo, (double) n);
			//pWriteH5(pop, mpiInfo, (double) n, (double)n+0.5);
		}
		if( n< 100000 && n%10000 == 0){

			// Example of writing another dataset to history.xy.h5
			// xyWrite(history,"/group/group/dataset",(double)n,value,MPI_SUM);
			//Write h5 files
			//gWriteH5(E, mpiInfo, (double) n);
			//gWriteH5(rho, mpiInfo, (double) n);
			gWriteH5(rho, mpiInfo, (double) n);
			gWriteH5(rho_e, mpiInfo, (double) n);
			gWriteH5(rho_i, mpiInfo, (double) n);
			gWriteH5(phi, mpiInfo, (double) n);
			//pWriteH5(pop, mpiInfo, (double) n, (double)n+0.5);
		}

		if(n>nTimeSteps-1){
			msg(STATUS, "writing over a given timestep to file");
			pWriteH5(pop, mpiInfo, (double) n, (double)n+0.5);
			//gWriteH5(rho, mpiInfo, (double) n);
			//gWriteH5(rho_e, mpiInfo, (double) n);
			//gWriteH5(rho_i, mpiInfo, (double) n);
			//gWriteH5(phi, mpiInfo, (double) n);
			gWriteH5(E, mpiInfo, (double) n);
		}
		//if(n%20000 == 0){
			//pWriteH5(pop, mpiInfo, (double) n, (double)n+0.5);
			//gWriteH5(phi, mpiInfo, (double) n);
			//gWriteH5(rho, mpiInfo, (double) n);
			//gWriteH5(E, mpiInfo, (double) n);
		//}
//		if(n%1000==0 && n<48000){
//			msg(STATUS, "writing every 1000 timestep to file");
//			// Example of writing another dataset to history.xy.h5
//			// xyWrite(history,"/group/group/dataset",(double)n,value,MPI_SUM);
//			//Write h5 files
//			//gWriteH5(E, mpiInfo, (double) n);
//			//gWriteH5(rho, mpiInfo, (double) n);
//			gWriteH5(phi, mpiInfo, (double) n);
//			//pWriteH5(pop, mpiInfo, (double) n, (double)n+0.5);
//		}
		//gWriteH5(phi, mpiInfo, (double) n);
		pWriteEnergy(history,pop,(double)n);

		//free(nt);
		//free(mccTimestep);
		//free(frequency);
		//free(velThermal);
		//msg(STATUS, "   -    ");


	}
	//msg(STATUS, "Test returned %d", errorvar);

	if(mpiInfo->mpiRank==0) tMsg(t->total, "Time spent: ");
	MPI_Barrier(MPI_COMM_WORLD);

	/*
	* FINALIZE PINC VARIABLES
	*/
	gFreeMpi(mpiInfo);

	// Close h5 files
	pCloseH5(pop);
	gCloseH5(rho);
	gCloseH5(rho_e);
	gCloseH5(rho_i);
	gCloseH5(phi);
	gCloseH5(E);
	// oCloseH5(obj);
	xyCloseH5(history);

	// Free memory
	solverFree(solver);
	gFree(rho);
	gFree(rho_e);
	gFree(rho_i);
	gFree(phi);
	gFree(E);
	pFree(pop);
	free(S);
	free(T);
	// oFree(obj);

	gsl_rng_free(rngSync);
	gsl_rng_free(rng);

}
//
// funPtr mccTestMode2_set(dictionary *ini){
// 	//test sanity here!
// 	mccSanity(ini,"mccTestMode",2);
// 	return mccTestMode2;
// }
//
//
// void mccTestMode2(dictionary *ini){
//
//
// 	/*
// 	 * SELECT METHODS
// 	 */
// 	void (*acc)()   = select(ini,"methods:acc",	puAcc3D1_set,
// 												puAcc3D1KE_set,
// 												puAccND1_set,
// 												puAccND1KE_set,
// 												puAccND0_set,
// 												puAccND0KE_set,
// 												puBoris3D1_set,
// 												puBoris3D1KE_set,
// 												puBoris3D1KETEST_set);
//
// 	void (*distr)() = select(ini,"methods:distr",	puDistr3D1_set,
// 													puDistr3D1split_set,
// 													puDistrND1_set,
// 													puDistrND0_set);
//
//
// 	void (*extractEmigrants)() = select(ini,"methods:migrate",	puExtractEmigrants3D_set,
// 																puExtractEmigrantsND_set);
//
// 	//
// 	void (*solverInterface)()	= select(ini,	"methods:poisson",
// 												mgSolver_set
// 												//sSolver_set
// 												);
//
//
// 	//
// 	void (*solve)() = NULL;
// 	void *(*solverAlloc)() = NULL;
// 	void (*solverFree)() = NULL;
// 	solverInterface(&solve, &solverAlloc, &solverFree);
//
// 	/*
// 	 * INITIALIZE PINC VARIABLES
// 	 */
//
//
// 	MpiInfo *mpiInfo = gAllocMpi(ini);
// 	Population *pop = pAlloc(ini);
// 	Grid *E   = gAlloc(ini, VECTOR);
// 	Grid *rho = gAlloc(ini, SCALAR);
// 	Grid *res = gAlloc(ini, SCALAR);
// 	Grid *phi = gAlloc(ini, SCALAR);
// 	void *solver = solverAlloc(ini, rho, phi);
// 	/*
// 	* mcc specific variables
// 	*/
// 	// make struct???
// 	double PmaxElectron = 0;
// 	double maxfreqElectron = 0;
// 	double PmaxIon = 0;
// 	double maxfreqIon = 0;
// 	double *mass = pop->mass;
// 	int nSpecies = pop->nSpecies;
// 	double nt = iniGetDouble(ini,"collisions:numberDensityNeutrals"); //constant for now
// 	double mccTimestep = iniGetDouble(ini,"time:timeStep");
// 	//double frequency = iniGetDouble(ini,"collisions:collisionFrequency"); // for static Pmax...
// 	double *velThermal = iniGetDoubleArr(ini,"population:thermalVelocity",nSpecies);
//
//
// 	// using Boris algo
// 	//int nSpecies = pop->nSpecies;
// 	double *S = (double*)malloc((3)*(nSpecies)*sizeof(*S));
// 	double *T = (double*)malloc((3)*(nSpecies)*sizeof(*T));
//
// 	// Creating a neighbourhood in the rho to handle migrants
// 	gCreateNeighborhood(ini, mpiInfo, rho);
//
// 	// Setting Boundary slices
// 	gSetBndSlices(phi, mpiInfo);
//
// 	//Set mgSolve
// 	//MgAlgo mgAlgo = getMgAlgo(ini);
//
// 	// Random number seeds
// 	gsl_rng *rngSync = gsl_rng_alloc(gsl_rng_mt19937);
// 	gsl_rng *rng = gsl_rng_alloc(gsl_rng_mt19937);
// 	gsl_rng_set(rng,mpiInfo->mpiRank+1); // Seed needs to be >=1
//
//
// 	/*
// 	 * PREPARE FILES FOR WRITING
// 	 */
// 	int rank = phi->rank;
// 	double *denorm = malloc((rank-1)*sizeof(*denorm));
// 	double *dimen = malloc((rank-1)*sizeof(*dimen));
//
// 	for(int d = 1; d < rank;d++) denorm[d-1] = 1.;
// 	for(int d = 1; d < rank;d++) dimen[d-1] = 1.;
//
// 	pOpenH5(ini, pop, "pop");
// 	//gOpenH5(ini, rho, mpiInfo, denorm, dimen, "rho");
// 	gOpenH5(ini, phi, mpiInfo, denorm, dimen, "phi");
// 	//gOpenH5(ini, E,   mpiInfo, denorm, dimen, "E");
//   // oOpenH5(ini, obj, mpiInfo, denorm, dimen, "test");
//   // oReadH5(obj, mpiInfo);
//
// 	hid_t history = xyOpenH5(ini,"history");
// 	pCreateEnergyDatasets(history,pop);
//
// 	// Add more time series to history if you want
// 	// xyCreateDataset(history,"/group/group/dataset");
//
// 	free(denorm);
// 	free(dimen);
//
// 	/*
// 	 * INITIAL CONDITIONS
// 	 */
//
// 	// Initalize particles
// 	// pPosUniform(ini, pop, mpiInfo, rngSync);
// 	//pPosLattice(ini, pop, mpiInfo);
// 	//pVelZero(pop);
// 	// pVelMaxwell(ini, pop, rng);
// 	double maxVel = iniGetDouble(ini,"population:maxVel");
//
// 	// Manually initialize a single particle
// 	if(mpiInfo->mpiRank==0){
// 		double pos[3] = {8., 8., 8.};
// 		double vel[3] = {0.02, 0., 1.};
// 		pNew(pop, 0, pos, vel);
// 		double pos1[3] = {17., 17., 16.};
// 		double vel1[3] = {0.1, 0., 0.1};
// 		pNew(pop, 1, pos1, vel1); //second particle
// 	}
//
// 	// Perturb particles
// 	//pPosPerturb(ini, pop, mpiInfo);
//
// 	// Migrate those out-of-bounds due to perturbation
// 	extractEmigrants(pop, mpiInfo);
// 	puMigrate(pop, mpiInfo, rho);
//
// 	/*
// 	 * INITIALIZATION (E.g. half-step)
// 	 */
//
// 	// Get initial charge density
// 	distr(pop, rho);
// 	gHaloOp(addSlice, rho, mpiInfo, FROMHALO);
//
// 	// Get initial E-field
// 	solve(solver, rho, phi, mpiInfo);
// 	//solve(mgAlgo, mgRho, mgPhi, mgRes, mpiInfo);
// 	gFinDiff1st(phi, E);
// 	gHaloOp(setSlice, E, mpiInfo, TOHALO);
// 	gMul(E, -1.);
//
//
// 	// Advance velocities half a step
//
//
// 	gMul(E, 0.5);
// 	puGet3DRotationParameters(ini, T, S, 0.5);
// 	// adScale(T, 3*nSpecies, 0.5);
// 	// adScale(S, 3*nSpecies, 0.5);
//
// 	gZero(E);
// 	puAddEext(ini, pop, E);
// 	acc(pop, E, T, S);
//
// 	gMul(E, 2.0);
// 	puGet3DRotationParameters(ini, T, S, 1.0);
// 	// adScale(T, 3*nSpecies, 2.0);
// 	// adScale(S, 3*nSpecies, 2.0);
//
// 	/*
// 	 * TIME LOOP
// 	 */
//
// 	Timer *t = tAlloc(rank);
//
// 	double x_min = 20;
// 	double x_max = 0;
// 	double y_min = 20;
// 	double y_max = 0;
//
// 	// n should start at 1 since that's the timestep we have after the first
// 	// iteration (i.e. when storing H5-files).
// 	PmaxElectron = 1.0;
// 	PmaxIon = 1.0; // first timestep
// 	int nTimeSteps = iniGetInt(ini,"time:nTimeSteps");
// 	for(int n = 1; n <= nTimeSteps; n++){
//
// 		msg(STATUS,"Computing time-step %i",n);
// 		MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary
//
// 		// Check that no particle moves beyond a cell (mostly for debugging)
// 		pVelAssertMax(pop,maxVel);
//
// 		tStart(t);
//
// 		// Move particles
// 		//adPrint(pop->pos, 3);
// 		//x_min = pop->pos[0]<x_min ? pop->pos[0] : x_min;
// 		//x_max = pop->pos[0]>x_max ? pop->pos[0] : x_max;
// 		//y_min = pop->pos[1]<y_min ? pop->pos[1] : y_min;
// 		//y_max = pop->pos[1]>y_max ? pop->pos[1] : y_max;
// 		puMove(pop);
// 		// oRayTrace(pop, obj);
//
// 		// Migrate particles (periodic boundaries)
// 		extractEmigrants(pop, mpiInfo);
// 		puMigrate(pop, mpiInfo, rho);
//
// 		mccGetPmaxElectron(ini,mass[0], velThermal[0], mccTimestep, nt,
// 			&PmaxElectron, &maxfreqElectron, pop,mpiInfo);
// 		mccGetPmaxIon(ini,mass[1], velThermal[1], mccTimestep, nt, &PmaxIon,
// 			&maxfreqIon, pop,mpiInfo);
//
// 		PmaxElectron = 0.0;
// 		PmaxIon = 0.0;
//
// 		if(n%4==0 ){
// 			PmaxIon = 1.0; // CEX coll
//
// 		}
//
//
// 		//MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary
// 		//mccCollideElectron(ini,pop, &PmaxElectron, &maxfreqElectron, rng, mccTimestep, nt,DebyeLength); //race conditions?????
// 		//MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary
// 		mccCollideIon_debug(ini, pop, &PmaxIon, &maxfreqIon, rng, mccTimestep, nt);
// 		//MPI_Barrier(MPI_COMM_WORLD);	// Temporary, shouldn't be necessary
//
//
//
// 		// Check that no particle resides out-of-bounds (just for debugging)
// 		pPosAssertInLocalFrame(pop, rho);
// 		//msg(STATUS, "HEEEEEEEEEEERRRRRRRRRRREEEEEEEEEEEE");
// 		// Compute charge density
// 		distr(pop, rho);
// 		gHaloOp(addSlice, rho, mpiInfo, FROMHALO);
//
// 		// gAssertNeutralGrid(rho, mpiInfo);
//
// 		// Compute electric potential phi
// 		solve(solver, rho, phi, mpiInfo);
// 		//solve(mgAlgo, mgRho, mgPhi, mgRes, mpiInfo);
//
// 		gAssertNeutralGrid(phi, mpiInfo);
//
// 		// Compute E-field
// 		gFinDiff1st(phi, E);
// 		gHaloOp(setSlice, E, mpiInfo, TOHALO);
// 		gMul(E, -1.);
//
// 		gAssertNeutralGrid(E, mpiInfo);
// 		// Apply external E
// 		// gAddTo(Ext);
// 		//puAddEext(ini, pop, E);
//
// 		// Accelerate particle and compute kinetic energy for step n
//
// 		gZero(E); // Turning off E-field for testing
// 		puAddEext(ini, pop, E);
// 		acc(pop, E, T, S);
//
// 		tStop(t);
//
// 		// Sum energy for all species
// 		pSumKinEnergy(pop);
//
// 		// Compute potential energy for step n
// 		gPotEnergy(rho,phi,pop);
//
// 		// Example of writing another dataset to history.xy.h5
// 		// xyWrite(history,"/group/group/dataset",(double)n,value,MPI_SUM);
//
// 		//Write h5 files
// 		//gWriteH5(E, mpiInfo, (double) n);
// 		//gWriteH5(rho, mpiInfo, (double) n);
// 		gWriteH5(phi, mpiInfo, (double) n);
// 		pWriteH5(pop, mpiInfo, (double) n, (double)n+0.5);
// 		//pWriteEnergy(history,pop,(double)n);
//
// 	}
//
// 	msg(STATUS, "x in [%f, %f]", x_min, x_max);
// 	msg(STATUS, "y in [%f, %f]", y_min, y_max);
//
// 	if(mpiInfo->mpiRank==0) tMsg(t->total, "Time spent: ");
//
// 	/*
// 	 * FINALIZE PINC VARIABLES
// 	 */
// 	free(S);
// 	free(T);
//
// 	gFreeMpi(mpiInfo);
//
// 	// Close h5 files
// 	pCloseH5(pop);
// 	//gCloseH5(rho);
// 	gCloseH5(phi);
// 	//gCloseH5(E);
// 	// oCloseH5(obj);
// 	xyCloseH5(history);
//
// 	// Free memory
// 	solverFree(solver);
// 	gFree(rho);
// 	gFree(phi);
// 	gFree(E);
// 	pFree(pop);
// 	// oFree(obj);
//
// 	gsl_rng_free(rngSync);
// 	gsl_rng_free(rng);
//
// }
