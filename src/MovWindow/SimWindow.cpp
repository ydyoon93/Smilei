
#include "SimWindow.h"
#include "Params.h"
#include "Species.h"
#include "ElectroMagn.h"
#include "Interpolator.h"
#include "Projector.h"
#include "SmileiMPI.h"
#include "VectorPatch.h"
#include "Hilbert_functions.h"
#include "PatchesFactory.h"
#include <iostream>
#include <omp.h>
#include <fstream>

using namespace std;

SimWindow::SimWindow(Params& params)
{
    nspace_win_x_ = params.nspace_win_x;
    cell_length_x_   = params.cell_length[0];
    x_moved = 0.;      //The window has not moved at t=0. Warning: not true anymore for restarts.
    n_moved = 0;      //The window has not moved at t=0. Warning: not true anymore for restarts.
    velocity_x_ = params.velocity_x; 
    int nthds(1);
    #ifdef _OPENMP
        nthds = omp_get_max_threads();
    #endif
    patch_to_be_created.resize(nthds);

    delay_ = params.delay;
    MESSAGE(1,"Moving window is active:");
    MESSAGE(2,"nspace_win_x_ : " << nspace_win_x_);
    MESSAGE(2,"cell_length_x_ : " << cell_length_x_);
    MESSAGE(2,"velocity_x_ : " << velocity_x_);
    MESSAGE(2,"delay_ : " << delay_);
    
}

SimWindow::~SimWindow()
{
}


void SimWindow::operate(VectorPatch& vecPatches, SmileiMPI* smpi, Params& params)
{

        x_moved += cell_length_x_*params.n_space[0];
        n_moved += params.n_space[0];

    // Store current number of patch on current MPI process
    // Don't move during this process
    int nPatches( vecPatches.size() );
    int nSpecies  ( vecPatches(0)->vecSpecies.size() );
    int nmessage = 14+2*nSpecies;
    vector<int> nbrOfPartsSend(nSpecies,0);
    vector<int> nbrOfPartsRecv(nSpecies,0);

    // Delete western patch
    double energy_field_lost(0.);
    vector<double> energy_part_lost( vecPatches(0)->vecSpecies.size(), 0. );


    vecPatches.closeAllDiags(smpi);
    
    // Shift the patches, new patches will be created directly with their good patchid
    for (unsigned int ipatch = 0 ; ipatch < nPatches ; ipatch++) {
	vecPatches(ipatch)->neighbor_[0][1] = vecPatches(ipatch)->hindex;
        vecPatches(ipatch)->hindex = vecPatches(ipatch)->neighbor_[0][0];
    }
    // Init new patches (really new and received)
    for (unsigned int ipatch = 0 ; ipatch < nPatches ; ipatch++) {

        if ( vecPatches(ipatch)->MPI_me_ != vecPatches(ipatch)->MPI_neighbor_[0][1] ) {
            int patchid = vecPatches(ipatch)->neighbor_[0][1];
            Patch* newPatch = PatchesFactory::clone(vecPatches(0),params, smpi, patchid, n_moved );
            vecPatches.patches_.push_back( newPatch );
        }
    }

    for ( int ipatch = nPatches-1 ; ipatch >= 0 ; ipatch--) {

        // Patch à supprimer
        if ( vecPatches(ipatch)->isWestern() ) {

	    // Compute energy lost 
	    energy_field_lost += vecPatches(ipatch)->EMfields->computeNRJ();
	    for ( int ispec=0 ; ispec<vecPatches(0)->vecSpecies.size() ; ispec++ )
		energy_part_lost[ispec] += vecPatches(ipatch)->vecSpecies[ispec]->computeNRJ();

            delete  vecPatches.patches_[ipatch];
            vecPatches.patches_[ipatch] = NULL;
	    vecPatches.patches_.erase( vecPatches.patches_.begin() + ipatch );

        }
    }

    // Sync / Patches done for these diags -> Store in patch master 
    vecPatches(0)->EMfields->storeNRJlost( energy_field_lost );
    for ( int ispec=0 ; ispec<vecPatches(0)->vecSpecies.size() ; ispec++ )
	vecPatches(0)->vecSpecies[ispec]->storeNRJlost( energy_part_lost[ispec] );

    nPatches = vecPatches.size();

    // Sort patch by hindex (to avoid deadlock)
    //bool stop;
    int jpatch(nPatches-1);
    do {
        for ( int ipatch = 0 ; ipatch<jpatch ; ipatch++  ) {
            if ( vecPatches(ipatch)->hindex > vecPatches(jpatch)->hindex ) {
                Patch* tmp = vecPatches(ipatch);
                vecPatches.patches_[ipatch] = vecPatches.patches_[jpatch];
        	vecPatches.patches_[jpatch] = tmp;
            }
        }
        jpatch--;
    } while(jpatch>=0);

            // Patch à envoyer
            for (unsigned int ipatch = 0 ; ipatch < nPatches ; ipatch++) {
                //if my MPI left neighbor is not me AND I'm not a newly created patch, send me !
                if ( vecPatches(ipatch)->MPI_me_ != vecPatches(ipatch)->MPI_neighbor_[0][0] && vecPatches(ipatch)->hindex == vecPatches(ipatch)->neighbor_[0][0] ) {
                    smpi->isend( vecPatches(ipatch), vecPatches(ipatch)->MPI_neighbor_[0][0], vecPatches(ipatch)->hindex*nmessage );
		    //cout << vecPatches(ipatch)->MPI_me_ << " send : " << vecPatches(ipatch)->vecSpecies[0]->getNbrOfParticles() << " & " << vecPatches(ipatch)->vecSpecies[1]->getNbrOfParticles() << endl;
                }
            }
            // Patch à recevoir
            for (unsigned int ipatch = 0 ; ipatch < nPatches ; ipatch++) {
                //if my MPI right neighbor is not me AND my MPI right neighbor exists AND I am a newly created patch, I receive !
                if ( ( vecPatches(ipatch)->MPI_me_ != vecPatches(ipatch)->MPI_neighbor_[0][1] ) && ( vecPatches(ipatch)->MPI_neighbor_[0][1] != MPI_PROC_NULL )  && (vecPatches(ipatch)->neighbor_[0][0] != vecPatches(ipatch)->hindex) ){
                    smpi->recv( vecPatches(ipatch), vecPatches(ipatch)->MPI_neighbor_[0][1], vecPatches(ipatch)->hindex*nmessage, params );
		    //cout << vecPatches(ipatch)->MPI_me_ << " recv : " << vecPatches(ipatch)->vecSpecies[0]->getNbrOfParticles() << " & " << vecPatches(ipatch)->vecSpecies[1]->getNbrOfParticles() << endl;
                }
            }

    //Wait for all send to be completed by the receivers too.
    //MPI_Barrier(MPI_COMM_WORLD);
    smpi->barrier();

    // Suppress after exchange to not distrub patch position during exchange
    for ( int ipatch = nPatches-1 ; ipatch >= 0 ; ipatch--) {
        if ( vecPatches(ipatch)->MPI_me_ != vecPatches(ipatch)->MPI_neighbor_[0][0] && vecPatches(ipatch)->hindex == vecPatches(ipatch)->neighbor_[0][0] ) {

            delete vecPatches.patches_[ipatch];
            vecPatches.patches_[ipatch] = NULL;
	    vecPatches.patches_.erase( vecPatches.patches_.begin() + ipatch );

        }

    }
    nPatches = vecPatches.size();


    // Finish shifting the patches, new patches will be created directly with their good patches
    for (unsigned int ipatch = 0 ; ipatch < nPatches ; ipatch++) {
	if (vecPatches(ipatch)->neighbor_[0][0] != vecPatches(ipatch)->hindex) continue;
	    
	//For now also need to update neighbor_, corner_neighbor and their MPI counterparts even if these will be obsolete eventually.
	vecPatches(ipatch)->corner_neighbor_[1][0]= vecPatches(ipatch)->neighbor_[1][0];
	vecPatches(ipatch)->neighbor_[1][0]=        vecPatches(ipatch)->corner_neighbor_[0][0];


	vecPatches(ipatch)->corner_neighbor_[1][1]= vecPatches(ipatch)->neighbor_[1][1];
	vecPatches(ipatch)->neighbor_[1][1]=        vecPatches(ipatch)->corner_neighbor_[0][1];


	//Compute missing part of the new neighborhood tables.
	vecPatches(ipatch)->Pcoordinates[0]--;

	int xcall = vecPatches(ipatch)->Pcoordinates[0]-1;
	int ycall = vecPatches(ipatch)->Pcoordinates[1]-1;
	if (params.bc_em_type_x[0]=="periodic" && xcall < 0) xcall += (1<<params.mi[0]);
	if (params.bc_em_type_y[0]=="periodic" && ycall <0) ycall += (1<<params.mi[1]);
	vecPatches(ipatch)->corner_neighbor_[0][0] = generalhilbertindex(params.mi[0] , params.mi[1], xcall, ycall);
	ycall = vecPatches(ipatch)->Pcoordinates[1];
	vecPatches(ipatch)->neighbor_[0][0] = generalhilbertindex(params.mi[0] , params.mi[1], xcall, vecPatches(ipatch)->Pcoordinates[1]);
	ycall = vecPatches(ipatch)->Pcoordinates[1]+1;
	if (params.bc_em_type_y[0]=="periodic" && ycall >= 1<<params.mi[1]) ycall -= (1<<params.mi[1]);
	vecPatches(ipatch)->corner_neighbor_[0][1] = generalhilbertindex(params.mi[0] , params.mi[1], xcall, ycall);
	
    }

    for (int ipatch=0 ; ipatch<nPatches ; ipatch++ ) {
        vecPatches(ipatch)->updateMPIenv(smpi);
    }

    for (int ipatch=0 ; ipatch<nPatches ; ipatch++ )
	vecPatches(ipatch)->EMfields->laserDisabled();


    // 
    vecPatches.openAllDiags(params,smpi);
    
    vecPatches.set_refHindex() ;
    vecPatches.update_field_list() ;

    return;

}



bool SimWindow::isMoving(double time_dual)
{
    return ( (nspace_win_x_) && ((time_dual - delay_)*velocity_x_ > x_moved) );
}

void SimWindow::setOperators(VectorPatch& vecPatches)
{

    for (unsigned int ipatch = 0 ; ipatch < vecPatches.size() ; ipatch++) {

	vecPatches(ipatch)->updateMvWinLimits( x_moved, n_moved );

	for (unsigned int ispec=0 ; ispec<vecPatches(ipatch)->vecSpecies.size(); ispec++) {
	    vecPatches(ipatch)->vecSpecies[ispec]->updateMvWinLimits(x_moved);
	}

	vecPatches(ipatch)->Interp->setMvWinLimits( vecPatches(ipatch)->getCellStartingGlobalIndex(0) );
	vecPatches(ipatch)->Proj->setMvWinLimits  ( vecPatches(ipatch)->getCellStartingGlobalIndex(0) );

    }


}
