#include "SpeciesV.h"

#include <cmath>
#include <ctime>
#include <cstdlib>

#include <iostream>

#include <omp.h>

// IDRIS
#include <cstring>
// IDRIS
#include "PusherFactory.h"
#include "IonizationFactory.h"
#include "PartBoundCond.h"
#include "PartWall.h"
#include "BoundaryConditionType.h"

#include "ElectroMagn.h"
#include "Interpolator.h"
#include "InterpolatorFactory.h"
#include "Profile.h"

#include "Projector.h"
#include "ProjectorFactory.h"

#include "SimWindow.h"
#include "Patch.h"

// #include "Field.h"
#include "Field1D.h"
#include "Field2D.h"
#include "Field3D.h"
#include "Tools.h"

#include "DiagnosticTrack.h"

using namespace std;


// ---------------------------------------------------------------------------------------------------------------------
// Constructor for Species
// input: simulation parameters & Species index
// ---------------------------------------------------------------------------------------------------------------------
SpeciesV::SpeciesV( Params &params, Patch *patch ) :
    Species( params, patch )
{
    initCluster( params );
    npack_ = 0 ;
    packsize_ = 0;

    for (unsigned int idim=0; idim < params.nDim_field; idim++){
        distance[idim] = &Species::cartesian_distance;
    }
    if (params.geometry == "AMcylindrical"){
        distance[1] = &Species::radial_distance;
    }

#ifdef _OMPTASKS   

    first_cell_of_bin.resize(Nbins);
    last_cell_of_bin.resize(Nbins);

    int bin_ncells_transverse = 1;
    if ( nDim_field >= 2 ) { bin_ncells_transverse *= ( f_dim1-2*oversize[1] ) ; } 
    if ( nDim_field == 3 ) { bin_ncells_transverse *= ( f_dim2-2*oversize[2] ) ; }

    for (int ibin = 0; ibin < Nbins; ibin++){

        if (ibin == 0){
            first_cell_of_bin[ibin]     = 0;
            last_cell_of_bin[ibin]      = (clrw+1)*bin_ncells_transverse-1;
        } else {
            first_cell_of_bin[ibin]     = last_cell_of_bin[ibin-1]+1;
            last_cell_of_bin[ibin]      = first_cell_of_bin[ibin]+clrw*bin_ncells_transverse-1;
        }
      
    }            

#endif

}//END SpeciesV creator

// ---------------------------------------------------------------------------------------------------------------------
// Destructor for Species
// ---------------------------------------------------------------------------------------------------------------------
SpeciesV::~SpeciesV()
{
}


void SpeciesV::initCluster( Params &params )
{
    int ncells = 1;
    for( unsigned int iDim=0 ; iDim<nDim_field ; iDim++ ) {
        ncells *= ( params.n_space[iDim]+1 );
    }
    particles->last_index.resize( ncells, 0 );
    particles->first_index.resize( ncells, 0 );
    count.resize( ncells, 0 );

    //Size in each dimension of the buffers on which each bin are projected
    //In 1D the particles of a given bin can be projected on 6 different nodes at the second order (oversize = 2)
    
    Nbins = (params.n_space[0]/clrw); // Nbins is not equal to first_index.size() for SpeciesV
    
    //Size in each dimension of the buffers on which each bin are projected
    //In 1D the particles of a given bin can be projected on 6 different nodes at the second order (oversize = 2)

    //Primal dimension of fields.
    f_dim0 =  params.n_space[0] + 2 * oversize[0] +1;
    f_dim1 =  params.n_space[1] + 2 * oversize[1] +1;
    f_dim2 =  params.n_space[2] + 2 * oversize[2] +1;

    //Dual dimension of fields.
    f_dim0_d =  params.n_space[0] + 2 * oversize[0] +2;
    f_dim1_d =  params.n_space[1] + 2 * oversize[1] +2;
    f_dim2_d =  params.n_space[2] + 2 * oversize[2] +2;

    b_dim.resize( params.nDim_field, 1 );
    if( nDim_particle == 1 ) {
        b_dim[0] = ( 1 + clrw ) + 2 * oversize[0];
        b_dim[1] =  1;
        f_dim1 = 1;
        f_dim2 = 1;
        f_dim1_d = 1;
        f_dim2_d = 1;
    }
    if( nDim_particle == 2 ) {
        b_dim[0] = ( 1 + clrw ) + 2 * oversize[0]; // There is a primal number of bins.
        b_dim[1] =  f_dim1;
        f_dim2 = 1;
        f_dim2_d = 1;
    }
    if( nDim_particle == 3 ) {
        b_dim[0] = ( 1 + clrw ) + 2 * oversize[0]; // There is a primal number of bins.
        b_dim[1] = f_dim1;
        b_dim[2] = f_dim2;
    }

#ifdef _OMPTASKS
    nrj_lost_per_bin                       = new double[Nbins];
    nrj_radiation_per_bin                  = new double[Nbins];      
    if (params.geometry != "AMcylindrical" ){
        //! buffers for currents and charge
        b_Jx.resize(Nbins);
        b_Jy.resize(Nbins);
        b_Jz.resize(Nbins);
        b_rho.resize(Nbins);

        size_proj_buffer_rho = b_dim[0]*b_dim[1]*f_dim2;
        size_proj_buffer_Jx  = b_dim[0]*b_dim[1]*f_dim2;
        size_proj_buffer_Jy  = b_dim[0]*f_dim1_d*f_dim2;
        size_proj_buffer_Jz  = b_dim[0]*b_dim[1]*f_dim2_d;

        for( unsigned int ibin = 0 ; ibin < Nbins ; ibin++ ) {
            // allocate current-buffers, then put to zero their content
            b_Jx[ibin]  = new double[size_proj_buffer_Jx ];
            b_Jy[ibin]  = new double[size_proj_buffer_Jy ];
            b_Jz[ibin]  = new double[size_proj_buffer_Jz ];
            b_rho[ibin] = new double[size_proj_buffer_rho];
        }
    } else { // AM geometry
        //! buffers for currents and charge
        size_proj_buffer_rhoAM = b_dim[0]*b_dim[1]    * params.nmodes; // used for rhoAM
        size_proj_buffer_Jl    = b_dim[0]*b_dim[1]    * params.nmodes; // used for Jl
        size_proj_buffer_Jr    = b_dim[0]*f_dim1_d    * params.nmodes; // used for Jr
        size_proj_buffer_Jt    = b_dim[0]*b_dim[1]    * params.nmodes; // used for Jt

        //! buffers for currents and charge
        b_Jl.resize(Nbins);
        b_Jr.resize(Nbins);
        b_Jt.resize(Nbins);
        b_rhoAM.resize(Nbins);

        for( unsigned int ibin = 0 ; ibin < Nbins ; ibin++ ) {
            // allocate current-buffers, then put to zero their content
            b_Jl[ibin]    = new std::complex<double>[size_proj_buffer_Jl   ];
            b_Jr[ibin]    = new std::complex<double>[size_proj_buffer_Jr   ];
            b_Jt[ibin]    = new std::complex<double>[size_proj_buffer_Jt   ];
            b_rhoAM[ibin] = new std::complex<double>[size_proj_buffer_rhoAM];
        }
    } // end condition on geometry
#endif

    //Initialize specMPI
    MPI_buffer_.allocate( nDim_field );

    //ener_tot = 0.;
    nrj_bc_lost = 0.;
    nrj_mw_lost = 0.;
    new_particles_energy_ = 0.;
    nrj_radiation = 0.;

}//END initCluster


void SpeciesV::dynamics( double time_dual, unsigned int ispec,
                         ElectroMagn *EMfields, Params &params, bool diag_flag,
                         PartWalls *partWalls,
                         Patch *patch, SmileiMPI *smpi,
                         RadiationTables &RadiationTables,
                         MultiphotonBreitWheelerTables &MultiphotonBreitWheelerTables,
                         vector<Diagnostic *> &localDiags )
{
    int ithread;
#ifdef _OPENMP
    ithread = omp_get_thread_num();
#else
    ithread = 0;
#endif

#ifdef  __DETAILED_TIMERS
    double timer;
#endif

    if( npack_==0 ) {
        npack_    = 1;
        packsize_ = ( f_dim1-2*oversize[1] );

        //if ( ( (long int)particles->last_index.back() < (long int)60000 ) || (Radiate) || (Ionize) || (Multiphoton_Breit_Wheeler_process) )
        packsize_ *= ( f_dim0-2*oversize[0] );
        //else
        //    npack_ *= (f_dim0-2*oversize[0]);

        if( nDim_field == 3 ) {
            packsize_ *= ( f_dim2-2*oversize[2] );
        }
    }

    unsigned int iPart;

    int tid( 0 );
    std::vector<double> nrj_lost_per_thd( 1, 0. );

    // -------------------------------
    // calculate the particle dynamics
    // -------------------------------
    if( time_dual>time_frozen_ || Ionize ) { // moving particle
    
        smpi->dynamics_resize( ithread, nDim_field, particles->last_index.back(), params.geometry=="AMcylindrical" );

        //Point to local thread dedicated buffers
        //Still needed for ionization
        vector<double> *Epart = &( smpi->dynamics_Epart[ithread] );

        //Prepare for sorting
        for( unsigned int i=0; i<count.size(); i++ ) {
            count[i] = 0;
        }

        for( unsigned int ipack = 0 ; ipack < npack_ ; ipack++ ) {

            int nparts_in_pack = particles->last_index[( ipack+1 ) * packsize_-1 ];
            smpi->dynamics_resize( ithread, nDim_field, nparts_in_pack );

#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif

            // Interpolate the fields at the particle position
            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ )
                Interp->fieldsWrapper( EMfields, *particles, smpi, &( particles->first_index[ipack*packsize_+scell] ),
                                       &( particles->last_index[ipack*packsize_+scell] ),
                                       ithread, particles->first_index[ipack*packsize_] );

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[0] += MPI_Wtime() - timer;
#endif

            // Ionization
            if( Ionize ) {
#ifdef  __DETAILED_TIMERS
                timer = MPI_Wtime();
#endif
                for( unsigned int scell = 0 ; scell < particles->first_index.size() ; scell++ ) {
                    ( *Ionize )( particles, particles->first_index[scell], particles->last_index[scell], Epart, patch, Proj );
                }
#ifdef  __DETAILED_TIMERS
                patch->patch_timers_[4] += MPI_Wtime() - timer;
#endif
            }
            
            if ( time_dual <= time_frozen_ ) continue;

            // Radiation losses
            if( Radiate ) {
#ifdef  __DETAILED_TIMERS
                timer = MPI_Wtime();
#endif

                for( unsigned int scell = 0 ; scell < particles->first_index.size() ; scell++ ) {

                    ( *Radiate )( *particles, this->photon_species_, smpi,
                                  RadiationTables, nrj_radiation,
                                  particles->first_index[scell], particles->last_index[scell], ithread );

                    // // Update scalar variable for diagnostics
                    // nrj_radiation += Radiate->getRadiatedEnergy();
                    //
                    // // Update the quantum parameter chi
                    // Radiate->computeParticlesChi( *particles,
                    //                               smpi,
                    //                               first_index[scell],
                    //                               last_index[scell],
                    //                               ithread );
                }
#ifdef  __DETAILED_TIMERS
                patch->patch_timers_[5] += MPI_Wtime() - timer;
#endif
            }

            // Multiphoton Breit-Wheeler
            if( Multiphoton_Breit_Wheeler_process ) {
#ifdef  __DETAILED_TIMERS
                timer = MPI_Wtime();
#endif
                for( unsigned int scell = 0 ; scell < particles->first_index.size() ; scell++ ) {

                    // Pair generation process
                    ( *Multiphoton_Breit_Wheeler_process )( *particles,
                                                            smpi,
                                                            MultiphotonBreitWheelerTables,
                                                            particles->first_index[scell], particles->last_index[scell], ithread );

                    // Update scalar variable for diagnostics
                    // We reuse nrj_radiation for the pairs
                    nrj_radiation += Multiphoton_Breit_Wheeler_process->getPairEnergy();

                    // Update the photon quantum parameter chi of all photons
                    Multiphoton_Breit_Wheeler_process->compute_thread_chiph( *particles,
                            smpi,
                            particles->first_index[scell],
                            particles->last_index[scell],
                            ithread );

                    // Suppression of the decayed photons into pairs
                    Multiphoton_Breit_Wheeler_process->decayed_photon_cleaning(
                        *particles, smpi, scell, particles->first_index.size(), &particles->first_index[0], &particles->last_index[0], ithread );
                        
                }
#ifdef  __DETAILED_TIMERS
                patch->patch_timers_[6] += MPI_Wtime() - timer;
#endif
            }

#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif

            // Push the particles and the photons
            ( *Push )( *particles, smpi, particles->first_index[ipack*packsize_],
                       particles->last_index[ipack*packsize_+packsize_-1],
                       ithread, particles->first_index[ipack*packsize_] );

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[1] += MPI_Wtime() - timer;
            timer = MPI_Wtime();
#endif

            unsigned int length[3];
            length[0]=0;
            length[1]=params.n_space[1]+1;
            length[2]=params.n_space[2]+1;

            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                double ener_iPart( 0. );
                // Apply wall and boundary conditions
                if( mass_>0 ) {
                    for( unsigned int iwall=0; iwall<partWalls->size(); iwall++ ) {
                        ( *partWalls )[iwall]->apply( *particles, smpi, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], this, ithread, ener_iPart );
                        nrj_lost_per_thd[tid] += mass_ * ener_iPart;
                    }

                    // Boundary Condition may be physical or due to domain decomposition
                    // apply returns 0 if iPart is not in the local domain anymore

                    partBoundCond->apply( *particles, smpi, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], this, ithread, ener_iPart );
                    nrj_lost_per_thd[tid] += mass_ * ener_iPart;

                    for( iPart=particles->first_index[ipack*packsize_+scell] ; ( int )iPart<particles->last_index[ipack*packsize_+scell]; iPart++ ) {
                        if ( particles->cell_keys[iPart] != -1 ) {
                            //Compute cell_keys of remaining particles
                            for( unsigned int i = 0 ; i<nDim_field; i++ ) {
                                particles->cell_keys[iPart] *= this->length_[i];
                                particles->cell_keys[iPart] += round( ((this)->*(distance[i]))(particles, i, iPart) * dx_inv_[i] );
                            }
                            //First reduction of the count sort algorithm. Lost particles are not included.
                            count[particles->cell_keys[iPart]] ++;
                        }
                    }

                } else if( mass_==0 ) {

                    for( unsigned int iwall=0; iwall<partWalls->size(); iwall++ ) {
                        ( *partWalls )[iwall]->apply( *particles, smpi, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], this, ithread, ener_iPart );
                        nrj_lost_per_thd[tid] += ener_iPart;
                    }

                    // Boundary Condition may be physical or due to domain decomposition
                    // apply returns 0 if iPart is not in the local domain anymore

                    partBoundCond->apply( *particles, smpi, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], this, ithread, ener_iPart );
                    nrj_lost_per_thd[tid] += ener_iPart;

                    for( iPart=particles->first_index[scell] ; ( int )iPart<particles->last_index[scell]; iPart++ ) {
                        if ( particles->cell_keys[iPart] != -1 ) {
                            //Compute cell_keys of remaining particles
                            for( unsigned int i = 0 ; i<nDim_field; i++ ) {
                                particles->cell_keys[iPart] *= length[i];
                                particles->cell_keys[iPart] += round( ((this)->*(distance[i]))(particles, i, iPart) * dx_inv_[i] );
                            }
                            count[particles->cell_keys[iPart]] ++;
                        }
                    }
                }
            }
            //START EXCHANGE PARTICLES OF THE CURRENT BIN ?

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[3] += MPI_Wtime() - timer;
#endif

            // Project currents if not a Test species and charges as well if a diag is needed.
            // Do not project if a photon
            if( ( !particles->is_test ) && ( mass_ > 0 ) )
#ifdef  __DETAILED_TIMERS
                timer = MPI_Wtime();
#endif

            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ )
                Proj->currentsAndDensityWrapper(
                    EMfields, *particles, smpi, particles->first_index[ipack*packsize_+scell],
                    particles->last_index[ipack*packsize_+scell],
                    ithread,
                    diag_flag, params.is_spectral,
                    ispec, ipack*packsize_+scell, particles->first_index[ipack*packsize_]
                );

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[2] += MPI_Wtime() - timer;
#endif

            for( unsigned int ithd=0 ; ithd<nrj_lost_per_thd.size() ; ithd++ ) {
                nrj_bc_lost += nrj_lost_per_thd[tid];
            }
        } // End loop on packs
    } //End if moving or ionized particles

    if(time_dual <= time_frozen_ && diag_flag &&( !particles->is_test ) ) { //immobile particle (at the moment only project density)

        if( params.geometry != "AMcylindrical" ) {
            double *b_rho = EMfields->rho_s[ispec] ? &( *EMfields->rho_s[ispec] )( 0 ) : &( *EMfields->rho_ )( 0 ) ;
            for( unsigned int scell = 0 ; scell < particles->first_index.size() ; scell ++ ) { //Loop for projection on buffer_proj
                for( iPart=particles->first_index[scell] ; ( int )iPart<particles->last_index[scell]; iPart++ ) {
                    Proj->basic( b_rho, ( *particles ), iPart, 0 );
                } //End loop on particles
            }//End loop on scells

        } else { // AM case
            ElectroMagnAM *emAM = static_cast<ElectroMagnAM *>( EMfields );
            int n_species = patch->vecSpecies.size();
            for( unsigned int imode = 0; imode<params.nmodes; imode++ ) {
                int ifield = imode*n_species+ispec;
                complex<double> *b_rho = emAM->rho_AM_s[ifield] ? &( *emAM->rho_AM_s[ifield] )( 0 ) : &( *emAM->rho_AM_[imode] )( 0 ) ;
                for( unsigned int scell = 0 ; scell < particles->first_index.size() ; scell ++ ) { //Loop for projection on buffer_proj
                    for( int iPart=particles->first_index[scell] ; iPart<particles->last_index[scell]; iPart++ ) {
                        Proj->basicForComplex( b_rho, ( *particles ), iPart, 0, imode );
                    }
                }
            }
        }


    } // End projection for frozen particles

}//END dynamics

void SpeciesV::dynamicsTasks( double time_dual, unsigned int ispec,
                         ElectroMagn *EMfields, Params &params, bool diag_flag,
                         PartWalls *partWalls,
                         Patch *patch, SmileiMPI *smpi,
                         RadiationTables &RadiationTables,
                         MultiphotonBreitWheelerTables &MultiphotonBreitWheelerTables,
                         vector<Diagnostic *> &localDiags, int buffer_id )
{
  

#ifdef  __DETAILED_TIMERS
    double timer;
    int ithread;
#endif
    int bin_size0 = b_dim[0];
    for( unsigned int ibin = 0 ; ibin < Nbins ; ibin++ ) {
        nrj_lost_per_bin[ibin] = 0.;
        nrj_radiation_per_bin[ibin] = 0.;
    }
    // Init tags for the task dependencies of the particle operations
    int *bin_has_interpolated                   = new int[Nbins+1]; // the last element is used to manage the Multiphoton Breit Wheeler dependency
    int *bin_has_ionized                        = new int[Nbins];
    int *bin_has_radiated                       = new int[Nbins];
    int *bin_has_done_Multiphoton_Breit_Wheeler = new int[Nbins];
    int *bin_has_pushed                         = new int[Nbins];
    int *bin_has_done_particles_BC              = new int[Nbins];
    int *bin_has_projected                      = new int[Nbins];

    int *bin_can_radiate = new int[Nbins];;
    int *bin_can_push = new int[Nbins];;

    if (Radiate){ // if Radiation True ... 
        if (!Ionize) { 
            // ... radiate only after ionization if present ...
            bin_can_radiate = bin_has_interpolated;
        } else { 
            // ... radiate directly after interpolation if ionization is not present ...
            bin_can_radiate = bin_has_ionized;
        }
        // ... and push only after radiation 
        bin_can_push = bin_has_radiated;
    } else { // if Radiation False ...
        if (Ionize){ 
            // ... push after ionization if present
            bin_can_push = bin_has_ionized;
        } else { 
            // ... push directly after interpolation if ionization is not present
            // A Species with mass = 0 cannot Ionize or Radiate, thus this this is the used dependency array.
            // Remember that the element ibin = Nbins of bin_has_interpolated 
            // is used to manage the pusher dependency on the photon cleaning
            bin_can_push = bin_has_interpolated;       
        }
    }

    int Ncells = ( f_dim0-2*oversize[0] );
    if( nDim_field >= 2 ) {
        Ncells *= ( f_dim1-2*oversize[1] );
    }
    if( nDim_field == 3 ) {
        Ncells *= ( f_dim2-2*oversize[2] );
    }

    #pragma omp taskgroup
    {
    // -------------------------------
    // calculate the particle dynamics
    // -------------------------------
    if( time_dual>time_frozen_  || Ionize ) { // if moving particle or it can be ionized

        //Prepare for sorting
        for( unsigned int i=0; i<count.size(); i++ ) {
            count[i] = 0;
        }

        // resize the dynamics buffers to treat all the particles in this Patch ipatch and Species ispec
        int nparts = particles->last_index[Ncells-1 ];
        smpi->dynamics_resize( buffer_id, nDim_field, nparts, params.geometry=="AMcylindrical" );

        for( unsigned int ibin = 0 ; ibin < Nbins ; ibin++ ){ 
#ifdef  __DETAILED_TIMERS
            #pragma omp task default(shared) firstprivate(ibin) depend(out:bin_has_interpolated[ibin]) private(ithread,timer)
#else
            #pragma omp task default(shared) firstprivate(ibin) depend(out:bin_has_interpolated[ibin])
#endif
            {

            if ( params.geometry != "AMcylindrical" ){
                // Reset densities sub-buffers - each of these buffers stores a grid density on the ibin physical space
                // This must be done before Projection and before Ionization (because of the ionization currents)
                for (unsigned int i = 0; i < size_proj_buffer_Jx; i++)  b_Jx[ibin][i]    = 0.0;
                for (unsigned int i = 0; i < size_proj_buffer_Jy; i++)  b_Jy[ibin][i]    = 0.0;
                for (unsigned int i = 0; i < size_proj_buffer_Jz; i++)  b_Jz[ibin][i]    = 0.0;
                for (unsigned int i = 0; i < size_proj_buffer_rho; i++) b_rho[ibin][i]   = 0.0;
            } else {
                // Reset densities sub-buffers - each of these buffers stores a grid density on the ibin physical space
                // This must be done before Projection and before Ionization (because of the ionization currents)
                for (unsigned int i = 0; i < size_proj_buffer_Jl; i++)  b_Jl[ibin][i]    = 0.0;
                for (unsigned int i = 0; i < size_proj_buffer_Jr; i++)  b_Jr[ibin][i]    = 0.0;
                for (unsigned int i = 0; i < size_proj_buffer_Jt; i++)  b_Jt[ibin][i]    = 0.0;
                for (unsigned int i = 0; i < size_proj_buffer_rhoAM; i++) b_rhoAM[ibin][i] = 0.0;
            }

#ifdef  __DETAILED_TIMERS
            ithread = omp_get_thread_num();
            timer = MPI_Wtime();
#endif

            // Interpolate the fields at the particle position
            for( unsigned int scell = first_cell_of_bin[ibin] ; scell < last_cell_of_bin[ibin] ; scell++ ){
                Interp->fieldsWrapper( EMfields, *particles, smpi, &( particles->first_index[scell] ),
                                       &( particles->last_index[scell] ),
                                       buffer_id, particles->first_index[0] );
            } // end cell loop for Interpolator

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[0*patch->thread_number_ + ithread] += MPI_Wtime() - timer;
#endif
            } //end task Interpolator

        } // end ibin loop for Interpolator

    } // end if moving particle or ionize

    if( time_dual>time_frozen_ ){ // if moving particle push
    
        for( unsigned int ibin = 0 ; ibin < Nbins ; ibin++ ) {
#ifdef  __DETAILED_TIMERS
            #pragma omp task default(shared) firstprivate(ibin) depend(in:bin_can_push[ibin],bin_can_push[Nbins]) depend(out:bin_has_pushed[ibin]) private(ithread,timer)
#else
            #pragma omp task default(shared) firstprivate(ibin) depend(in:bin_can_push[ibin],bin_can_push[Nbins]) depend(out:bin_has_pushed[ibin])
#endif
            {
#ifdef  __DETAILED_TIMERS
            ithread = omp_get_thread_num();
            timer = MPI_Wtime();
#endif
            for( unsigned int scell = first_cell_of_bin[ibin] ; scell < last_cell_of_bin[ibin] ; scell++ ){
                // Push the particles and the photons
                ( *Push )( *particles, smpi, particles->first_index[scell],
                           particles->last_index[scell],
                           buffer_id, particles->first_index[0] );
            } // end cell loop for Push
            
#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[1*patch->thread_number_ + ithread] += MPI_Wtime() - timer;
#endif
            } // end task for Push on ibin
        } // end ibin loop for Push
    } // end if moving particle, radiate and push

    if( time_dual>time_frozen_){ // do not apply particles BC nor projection on frozen particles     

        for( unsigned int ibin = 0 ; ibin < Nbins ; ibin++ ) {
#ifdef  __DETAILED_TIMERS
            #pragma omp task default(shared) firstprivate(ibin) private(ithread,timer) depend(in:bin_has_pushed[ibin]) depend(out:bin_has_done_particles_BC[ibin])
#else
            #pragma omp task default(shared) firstprivate(ibin) depend(in:bin_has_pushed[ibin]) depend(out:bin_has_done_particles_BC[ibin])
#endif
            {
            double ener_iPart( 0. );

#ifdef  __DETAILED_TIMERS
            ithread = omp_get_thread_num();
            timer = MPI_Wtime();
#endif
            // Variables to compute cell_keys for the sorting
            unsigned int length[3];
            length[0]=0;
            length[1]=params.n_space[1]+1;
            length[2]=params.n_space[2]+1;

            for( unsigned int scell = first_cell_of_bin[ibin] ; scell < last_cell_of_bin[ibin] ; scell++ ) {
            
                // Apply wall and boundary conditions
                if( mass_>0 ) {
                    for( unsigned int iwall=0; iwall<partWalls->size(); iwall++ ) {
                        ( *partWalls )[iwall]->apply( *particles, smpi, particles->first_index[scell], particles->last_index[scell], this, buffer_id, ener_iPart );
                        nrj_lost_per_bin[ibin] += mass_ * ener_iPart;
                    }
                
                    // Boundary Condition may be physical or due to domain decomposition
                    // apply returns 0 if iPart is not in the local domain anymore
                    partBoundCond->apply( *particles, smpi, particles->first_index[scell], particles->last_index[scell], this, buffer_id, ener_iPart );
                    nrj_lost_per_bin[ibin] += mass_ * ener_iPart;
                
                } else if( mass_==0 ) {
                
                    for( unsigned int iwall=0; iwall<partWalls->size(); iwall++ ) {
                        ( *partWalls )[iwall]->apply( *particles, smpi, particles->first_index[scell], particles->last_index[scell], this, buffer_id, ener_iPart );
                        nrj_lost_per_bin[ibin] += ener_iPart;
                    }
                
                    // Boundary Condition may be physical or due to domain decomposition
                    // apply returns 0 if iPart is not in the local domain anymore
                    partBoundCond->apply( *particles, smpi, particles->first_index[scell], particles->last_index[scell], this, buffer_id, ener_iPart );
                    nrj_lost_per_bin[ibin] += ener_iPart;
                
                } // end mass = 0

                // Compute cell_keys for sorting
                for( unsigned int iPart=particles->first_index[scell] ; ( int )iPart<particles->last_index[scell]; iPart++ ) {
                    if ( particles->cell_keys[iPart] != -1 ) {
                        //Compute cell_keys of remaining particles
                        for( unsigned int i = 0 ; i<nDim_field; i++ ) {
                            particles->cell_keys[iPart] *= this->length_[i];
                            particles->cell_keys[iPart] += round( ((this)->*(distance[i]))(particles, i, iPart) * dx_inv_[i] );
                        }
                    }
                } // end iPart loop to compute cell_keys
              } // end scell loop

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[3*patch->thread_number_ + ithread] += MPI_Wtime() - timer;
#endif
            } // end task for particles BC and cell_keys on ibin
        } // end ibin loop for particles BC

        for( unsigned int ibin = 0 ; ibin < Nbins ; ibin++ ) {
#ifdef  __DETAILED_TIMERS
            #pragma omp task default(shared) firstprivate(ibin,bin_size0) private(ithread,timer) depend(in:bin_has_done_particles_BC[ibin]) depend(out:bin_has_projected[ibin])
#else
            #pragma omp task default(shared) firstprivate(ibin,bin_size0) depend(in:bin_has_done_particles_BC[ibin]) depend(out:bin_has_projected[ibin])
#endif
            {
                
#ifdef  __DETAILED_TIMERS
            ithread = omp_get_thread_num();
            timer = MPI_Wtime();
#endif
                
            // Project currents if not a Test species and charges as well if a diag is needed.
            // Do not project if a photon
            if( ( !particles->is_test ) && ( mass_ > 0 ) ) {
                if (params.geometry != "AMcylindrical"){
                    for( unsigned int scell = first_cell_of_bin[ibin] ; scell < last_cell_of_bin[ibin] ; scell++ ) {
                        Proj->currentsAndDensityWrapperOnBuffers(
                            b_Jx[ibin], b_Jy[ibin], b_Jz[ibin], b_rho[ibin], 
                            ibin*clrw, *particles, smpi, 
                            particles->first_index[scell], particles->last_index[scell], 
                            buffer_id, diag_flag, params.is_spectral, ispec, scell );
                    } // end scell loop
                } else {
                    // for( unsigned int scell = first_cell_of_bin[ibin] ; scell < last_cell_of_bin[ibin] ; scell++ ) {
                    // ProjectorAM2Order *ProjAM = static_cast<ProjectorAM2Order *>(Proj);
                    // ProjAM->currentsAndDensityWrapperOnAMBuffers( EMfields, b_Jl[ibin], b_Jr[ibin], b_Jt[ibin], b_rhoAM[ibin], 
                    //                                             ibin*clrw, bin_size0, *particles, smpi, 
                    //                                             particles->first_index[scell], particles->last_index[scell], 
                    //                                             buffer_id, diag_flag);
                    // } // end scell loop
                } // end if AM
            } // end condition on test and mass

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[2*patch->thread_number_ + ithread] += MPI_Wtime() - timer;
#endif
            }//end task for Proj of ibin
        }// end ibin loop for Proj    

#pragma omp taskwait


        // Compute count arrays for the sorting
        // For the moment this operation is made on all the particles of the patch to avoid data races
        // #pragma omp task depend(in:bin_has_done_particles_BC[0:(Nbins-1)])
        {
        for( unsigned int scell = 0 ; scell < Ncells ; scell++ ) {
            for( unsigned int iPart=particles->first_index[scell] ; ( int )iPart<particles->last_index[scell]; iPart++ ) {
                if ( particles->cell_keys[iPart] != -1 ) {
                    //First reduction of the count sort algorithm. Lost particles are not included.
                    count[particles->cell_keys[iPart]] ++;
                }
            } // end iPart loop
        } // end cells loop
        } // end task to compute count array 



    } // end if moving particle

//     {
//     // reduction of lost energy
//     for( unsigned int ibin=0 ; ithd<Nbins ; ibin++ ) {
//         nrj_bc_lost += nrj_lost_per_bin[ibin];
//     }
//     // reduction of radiated energy
//     if( Radiate || Multiphoton_Breit_Wheeler_process ) {
// #ifdef  __DETAILED_TIMERS
//                 timer = MPI_Wtime();
//                 ithread = omp_get_thread_num();
// #endif
// 
//         for( unsigned int ibin=0 ; ibin < Nbins ; ibin++ ) {
//             nrj_radiation += nrj_radiation_per_bin[ibin];
//         }
// #ifdef  __DETAILED_TIMERS
//         patch->patch_timers_[5*patch->thread_number_ + ithread] += MPI_Wtime() - timer;
// #endif
//     } // end if Radiate
//     } // end task for lost/radiated energy reduction    

    } // end taskgroup



} // END dynamicsTasks

// ---------------------------------------------------------------------------------------------------------------------
// For all particles of the species
//   - increment the charge (projection)
//   - used at initialisation for Poisson (and diags if required, not for now dynamics )
// ---------------------------------------------------------------------------------------------------------------------
void SpeciesV::computeCharge( unsigned int ispec, ElectroMagn *EMfields, bool old /*=false*/ )
{
    // -------------------------------
    // calculate the particle charge
    // -------------------------------
    if( ( !particles->is_test ) ) {
        if( !dynamic_cast<ElectroMagnAM *>( EMfields ) ) {
            double *b_rho=&( *EMfields->rho_ )( 0 );
            for( unsigned int iPart=particles->first_index[0] ; ( int )iPart<particles->last_index[particles->last_index.size()-1]; iPart++ ) {
                Proj->basic( b_rho, ( *particles ), iPart, 0 );
            }
        } else {
            ElectroMagnAM *emAM = static_cast<ElectroMagnAM *>( EMfields );
            unsigned int Nmode = emAM->rho_AM_.size();
            for( unsigned int imode=0; imode<Nmode; imode++ ) {
                complex<double> *b_rho = old ? &( *emAM->rho_old_AM_[imode] )( 0 ) : &( *emAM->rho_AM_[imode] )( 0 );
                for( unsigned int iPart=particles->first_index[0] ; ( int )iPart<particles->last_index[particles->last_index.size()-1]; iPart++ ) {
                    Proj->basicForComplex( b_rho, ( *particles ), iPart, 0, imode );
                }
             }
       }
   }

}//END computeCharge


// ---------------------------------------------------------------------------------------------------------------------
// Sort particles
// ---------------------------------------------------------------------------------------------------------------------
void SpeciesV::sortParticles( Params &params, Patch *patch )
{
    unsigned int npart, ncell;
    int ip_dest, cell_target;
    unsigned int length[3];
    vector<int> buf_cell_keys[3][2];
    std::vector<unsigned int> cycle;
    unsigned int ip_src;

    length[0]=0;
    length[1]=params.n_space[1]+1;
    length[2]=params.n_space[2]+1;

    //Number of dual cells
    ncell = ( params.n_space[0]+1 );
    for( unsigned int i=1; i < nDim_field; i++ ) {
        ncell *= length[i];
    }

    //Number of particles before exchange
    npart = particles->size();

    //Loop over just arrived particles to compute their cell keys and contribution to count
    for( unsigned int idim=0; idim < nDim_field ; idim++ ) {
        for( unsigned int ineighbor=0 ; ineighbor < 2 ; ineighbor++ ) {
            buf_cell_keys[idim][ineighbor].resize( MPI_buffer_.part_index_recv_sz[idim][ineighbor] );
            #pragma omp simd
            for( unsigned int ip=0; ip < MPI_buffer_.part_index_recv_sz[idim][ineighbor]; ip++ ) {
                for( unsigned int ipos=0; ipos < nDim_field ; ipos++ ) {
                    double X = ((this)->*(distance[ipos]))(&MPI_buffer_.partRecv[idim][ineighbor], ipos, ip);
                    int IX = round( X * dx_inv_[ipos] );
                    buf_cell_keys[idim][ineighbor][ip] = buf_cell_keys[idim][ineighbor][ip] * length[ipos] + IX;
                }
            }
            //Can we vectorize this reduction ?
            for( unsigned int ip=0; ip < MPI_buffer_.part_index_recv_sz[idim][ineighbor]; ip++ ) {
                count[buf_cell_keys[idim][ineighbor][ip]] ++;
            }
        }
    }

    // second loop convert the count array in cumulative sum
    particles->first_index[0]=0;
    for( unsigned int ic=1; ic < ncell; ic++ ) {
        particles->first_index[ic] = particles->first_index[ic-1] + count[ic-1];
        particles->last_index[ic-1]= particles->first_index[ic];
    }

    //New total number of particles is stored as last element of particles->last_index
    particles->last_index[ncell-1] = particles->last_index[ncell-2] + count.back() ;

    //Now proceed to the cycle sort

    if( MPI_buffer_.partRecv[0][0].size() == 0 ) {
        MPI_buffer_.partRecv[0][0].initialize( 0, *particles );    //Is this correct ?
    }

    // Resize the particle vector
    if( ( unsigned int )particles->last_index.back() > npart ) {
        particles->resize( particles->last_index.back(), nDim_particle, params.keep_position_old );
        particles->cell_keys.resize( particles->last_index.back(), -1 ); // Merge this in particles.resize(..) ?
    }

    //Copy all particles from MPI buffers back to the writable particles via cycle sort pass.
    for( unsigned int idim=0; idim < nDim_field ; idim++ ) {
        for( unsigned int ineighbor=0 ; ineighbor < 2 ; ineighbor++ ) {
            for( unsigned int ip=0; ip < MPI_buffer_.part_index_recv_sz[idim][ineighbor]; ip++ ) {
                cycle.resize( 1 );
                cell_target = buf_cell_keys[idim][ineighbor][ip];
                ip_dest = particles->first_index[cell_target];
                while( particles->cell_keys[ip_dest] == cell_target ) {
                    ip_dest++;
                }
                particles->first_index[cell_target] = ip_dest + 1 ;
                cycle[0] = ip_dest;
                cell_target = particles->cell_keys[ip_dest];
                //As long as the particle is not erased, we can build up the cycle.
                while( cell_target != -1 ) {
                    ip_dest = particles->first_index[cell_target];
                    while( particles->cell_keys[ip_dest] == cell_target ) {
                        ip_dest++;
                    }
                    particles->first_index[cell_target] = ip_dest + 1 ;
                    cycle.push_back( ip_dest );
                    cell_target = particles->cell_keys[ip_dest];
                }
                //Last target_cell is -1, the particle must be erased:
                particles->translateParticles( cycle );
                //Eventually copy particle from the MPI buffer into the particle vector.
                MPI_buffer_.partRecv[idim][ineighbor].overwriteParticle( ip, *particles, cycle[0] );
            }
        }
    }

    // -------------------------------------------------------------------------------------
    // Checkpoint for debugging
    
    // for( unsigned int scell = 0 ; scell < particles->first_index.size(); scell++ ) {
    //     for (unsigned int ip = particles->first_index[scell] ; ip  < particles->last_index[scell] ; ip ++) {
    //
    //         double xmin = patch->getDomainLocalMin(0);
    //         double xmax = patch->getDomainLocalMax(0);
    //         double ymin = patch->getDomainLocalMin(1);
    //         double ymax = patch->getDomainLocalMax(1);
    //
    //         double x = particles->position(0,ip);
    //         double y = particles->position(1,ip);
    //         double mx = particles->momentum(0,ip);
    //         double my = particles->momentum(1,ip);
    //         double mz = particles->momentum(2,ip);
    //         double v = sqrt(mx*mx+my*my+mz*mz)/sqrt(1+mx*mx+my*my+mz*mz);
    //         //if (particles->cell_keys[ip] < 0) {
    //         std::cerr
    //         << " Np: " << particles->last_index[scell] - particles->first_index[scell]
    //         << " Cell keys size: " << particles->cell_keys.size()
    //         << " ip: "<< ip
    //         << " cell_keys: " << particles->cell_keys[ip]
    //         << ", x: " << xmin
    //         << " < " << x
    //         << " < " << xmax
    //         << ", y: " << ymin
    //         << " < " << y
    //         << " < " << ymax
    //         << ", mx: " << mx
    //         << ", my: " << my
    //         << ", mz: " << mz
    //         << setprecision(10)
    //         << ", v: " << v
    //         << std::endl;
    //
    //         if (x <= xmin
    //             || x >= xmax
    //             || y <= ymin
    //             || y >= ymax
    //             || v >= 1) {
    //             ERROR("error")
    //         }
    //         //}
    //     }
    // }

    //Copy valid particles siting over particles->last_index.back() back into the real particles array (happens when more particles are lost than received)
    for( unsigned int ip=( unsigned int )particles->last_index.back(); ip < npart; ip++ ) {
        cell_target = particles->cell_keys[ip];
        
        // double xmin = patch->getDomainLocalMin(0);
        // double xmax = patch->getDomainLocalMax(0);
        // double ymin = patch->getDomainLocalMin(1);
        // double ymax = patch->getDomainLocalMax(1);
        // //
        // double x = particles->position(0,ip);
        // double y = particles->position(1,ip);
        // double w = particles->weight(ip);
        // double mx = particles->momentum(0,ip);
        // double my = particles->momentum(1,ip);
        // double mz = particles->momentum(2,ip);
        // double v = sqrt(mx*mx+my*my+mz*mz)/sqrt(1+mx*mx+my*my+mz*mz);
        // //if (particles->cell_keys[ip] < 0) {
        // std::cerr << cell_target << " " << particles->first_index[cell_target] <<  std::endl;
        // std::cerr
        // << " Cell keys size: " << particles->cell_keys.size()
        // << " ip: "<< ip
        // << " cell_keys: " << particles->cell_keys[ip]
        // << ", x: " << xmin
        // << " < " << x
        // << " < " << xmax
        // << ", y: " << ymin
        // << " < " << y
        // << " < " << ymax
        // << ", mx: " << mx
        // << ", my: " << my
        // << ", mz: " << mz
        // << setprecision(10)
        // << ", v: " << v
        // << std::endl;
        
        if( cell_target == -1 ) {
            continue;
        }
        cycle.resize( 0 );
        cycle.push_back( ip );
        
        //As long as the particle is not erased, we can build up the cycle.
        while( cell_target != -1 ) {

            ip_dest = particles->first_index[cell_target];

            while( particles->cell_keys[ip_dest] == cell_target ) {
                ip_dest++;
            }
            particles->first_index[cell_target] = ip_dest + 1 ;
            cycle.push_back( ip_dest );
            cell_target = particles->cell_keys[ip_dest];
        }
        //Last target_cell is -1, the particle must be erased:
        particles->translateParticles( cycle );
    }

    // Resize the particle vector
    if( ( unsigned int )particles->last_index.back() < npart ) {
        particles->resize( particles->last_index.back(), nDim_particle, params.keep_position_old );
        particles->cell_keys.resize( particles->last_index.back() ); // Merge this in particles.resize(..) ?
    }


    //Loop over all cells
    for( int icell = 0 ; icell < ( int )ncell; icell++ ) {
        for( unsigned int ip=( unsigned int )particles->first_index[icell]; ip < ( unsigned int )particles->last_index[icell] ; ip++ ) {
            //update value of current cell 'icell' if necessary
            //if particle changes cell, build a cycle of exchange as long as possible. Treats all particles
            if( particles->cell_keys[ip] != icell ) {
                cycle.resize( 1 );
                cycle[0] = ip;
                ip_src = ip;
                //While the destination particle is not going out of the patch or back to the initial cell, keep building the cycle.
                while( particles->cell_keys[ip_src] != icell ) {
                    //Scan the next cell destination
                    ip_dest = particles->first_index[particles->cell_keys[ip_src]];
                    while( particles->cell_keys[ip_dest] == particles->cell_keys[ip_src] ) {
                        ip_dest++;
                    }
                    //In the destination cell, if a particle is going out of this cell, add it to the cycle.
                    particles->first_index[particles->cell_keys[ip_src]] = ip_dest + 1 ;
                    cycle.push_back( ip_dest );
                    ip_src = ip_dest; //Destination becomes source for the next iteration
                }
                //swap parts
                particles->swapParticles( cycle );
            }
        }
    } //end loop on cells
    // Restore particles->first_index initial value
    particles->first_index[0]=0;
    for( unsigned int ic=1; ic < ncell; ic++ ) {
        particles->first_index[ic] = particles->last_index[ic-1];
    }
}


void SpeciesV::computeParticleCellKeys( Params &params )
{
    //Compute part_cell_keys at patch creation. This operation is normally done in the pusher to avoid additional particles pass.

    unsigned int ip, npart;
    int IX;
    double X;

    npart = particles->size(); //Number of particles

    #pragma omp simd
    for( ip=0; ip < npart ; ip++ ) {
        // Counts the # of particles in each cell (or sub_cell) and store it in sparticles->last_index.
        for( unsigned int ipos=0; ipos < nDim_field ; ipos++ ) {
            X = ((this)->*(distance[ipos]))(particles, ipos, ip);
            IX = round( X * dx_inv_[ipos] );
            particles->cell_keys[ip] = particles->cell_keys[ip] * this->length_[ipos] + IX;
        }
    }
    for( ip=0; ip < npart ; ip++ ) {
        count[particles->cell_keys[ip]] ++ ;
    }

}

// -----------------------------------------------------------------------------
//! Compute cell_keys for the specified bin boundaries.
//! params object that contains the global parameters
//! istart first bin index
//! iend last bin index
// -----------------------------------------------------------------------------
void SpeciesV::compute_bin_cell_keys( Params &params, int istart, int iend )
{
    // Resize of cell_keys seems necessary here
    particles->cell_keys.resize( particles->size() );

    #pragma omp simd
    for( int ip=istart; ip < iend; ip++ ) {
        // Counts the # of particles in each cell (or sub_cell) and store it in sparticles->last_index.
        for( unsigned int ipos=0; ipos < nDim_field ; ipos++ ) {
            particles->cell_keys[ip] *= this->length_[ipos];
            particles->cell_keys[ip] += round( ((this)->*(distance[ipos]))(particles, ipos, ip) * dx_inv_[ipos] );
        }
    }
}

void SpeciesV::importParticles( Params &params, Patch *patch, Particles &source_particles, vector<Diagnostic *> &localDiags )
{

    unsigned int npart = source_particles.size(), ncells=particles->first_index.size();

    // If this species is tracked, set the particle IDs
    if( particles->tracked ) {
        dynamic_cast<DiagnosticTrack *>( localDiags[tracking_diagnostic] )->setIDs( source_particles );
    }

    unsigned int length[3];
    length[0]=0;
    length[1]=params.n_space[1]+1;
    length[2]=params.n_space[2]+1;

    // compute cell keys of new parts
    vector<int> src_cell_keys( npart, 0 );
    for ( unsigned int ip = 0 ; ip < npart ; ip++ ) {
        for( unsigned int ipos=0; ipos < nDim_field ; ipos++ ) {
            double X = ((this)->*(distance[ipos]))(&source_particles, ipos, ip);
            int IX = round( X * dx_inv_[ipos] );
            src_cell_keys[ip] = src_cell_keys[ip] * length[ipos] + IX;
        }
    }
    vector<int> src_count( ncells, 0 );
    for( unsigned int ip=0; ip < npart ; ip++ )
        src_count[src_cell_keys[ip]] ++;

    // sort new parts per cells
    int istart = 0;
    int istop  = src_count[0];

    for ( int icell = 0 ; icell < (int)ncells ; icell++ ) {
        if (src_count[icell]!=0) {
            for( int ip=istart; ip < istop ; ip++ ) {
                if ( src_cell_keys[ip] == icell )
                    continue;
                else { // rearrange particles
                    int ip_swap = istop;
                    while (( src_cell_keys[ip_swap] != icell ) && (ip_swap<(int)npart))
                        ip_swap++;
                    source_particles.swapParticle(ip, ip_swap);
                    int tmp = src_cell_keys[ip];
                    src_cell_keys[ip] = src_cell_keys[ip_swap];
                    src_cell_keys[ip_swap] = tmp;
                } // rearrange particles
            } // end loop on particles of a cell

            // inject in main data structure per cell
            source_particles.copyParticles( istart, src_count[icell],
                                        *particles,
                                        particles->first_index[icell] );
            particles->last_index[icell] += src_count[icell];
            for ( unsigned int idx=icell+1 ; idx<particles->last_index.size() ; idx++ ) {
                particles->first_index[idx] += src_count[icell];
                particles->last_index[idx]  += src_count[icell];
            }
            count[icell] += src_count[icell];

        }
        // update istart/istop fot the next cell
        istart += src_count[icell];
        if ( icell != (int)ncells-1  )
            istop  += src_count[icell+1];
        else
            istop = npart;

    } // End cell loop
    //source_particles.clear();

    // Set place for new particles in species->particles->cell_keys
    for (unsigned int ip=0;ip<npart ; ip++ )
        addSpaceForOneParticle();

    source_particles.clear();

}

// ---------------------------------------------------------------------------------------------------------------------
//! Particle merging cell by cell
// ---------------------------------------------------------------------------------------------------------------------
void SpeciesV::mergeParticles( double time_dual, unsigned int ispec,
                               Params &params,
                               Patch *patch, SmileiMPI *smpi,
                               std::vector<Diagnostic *> &localDiags )
{
//     int ithread;
// #ifdef _OPENMP
//     ithread = omp_get_thread_num();
// #else
//     ithread = 0;
// #endif


    // Only for moving particles
    if( time_dual>time_frozen_ ) {

        unsigned int scell ;
        // double weight_before = 0;
        // double weight_after = 0;
        // double energy_before = 0;
        // double energy_after = 0;
        std::vector <int> mask(particles->last_index.back(), 1);

        // Resize the cell_keys
        // particles->cell_keys.resize( particles->last_index.back(), 1 );
        // #pragma omp simd
        // for (unsigned int ip = 0; ip < (unsigned int)(particles->last_index.back()) ; ip++) {
        //         particles->cell_keys[ip] = 1;
        // }

        // for (unsigned int ip = 0; ip < (unsigned int)(particles->last_index.back()) ; ip++) {
        //         weight_before += particles->weight(ip);
        //         energy_before += sqrt(1 + pow(particles->momentum(0,ip),2) + pow(particles->momentum(1,ip),2) + pow(particles->momentum(2,ip),2));
        // }

        // For each cell, we apply independently the merging process
        for( scell = 0 ; scell < particles->first_index.size() ; scell++ ) {
            
            ( *Merge )( mass_, *particles, mask, smpi, particles->first_index[scell],
                        particles->last_index[scell], count[scell]);
                        
        }

        // We remove empty space in an optimized manner
        particles->eraseParticlesWithMask(0, particles->last_index.back(), mask);

        // Update of first and last cell indexes
        particles->first_index[0] = 0;
        particles->last_index[0] = count[0];
        for( scell = 1 ; scell < particles->first_index.size(); scell++ ) {
            particles->first_index[scell] = particles->last_index[scell-1];
            particles->last_index[scell] = particles->first_index[scell] + count[scell];
        }
        
        //particles->cell_keys.resize(particles->last_index.back());
        
        // -------------------------------------------------------------------------------------
        // Checkpoint for debugging
        
        // for (unsigned int ip = 0; ip < (unsigned int)(particles->last_index.back()) ; ip++) {
        //         weight_after += particles->weight(ip);
        //         energy_after += sqrt(1 + pow(particles->momentum(0,ip),2)
        //                      + pow(particles->momentum(1,ip),2)
        //                      + pow(particles->momentum(2,ip),2));
        // }
        //
        // if (weight_before != weight_after) {
        //     std::cerr
        //     << " Weight before: " << weight_before
        //     << " Weight after: " << weight_after
        //     << " Energy before: " << energy_before
        //     << " Energy after: " << energy_after
        //     << std::endl;
        // }
        // -------------------------------------------------------------------------------------
        
        // -------------------------------------------------------------------------------------
        // Checkpoint for debugging
        
        // for( scell = 0 ; scell < particles->first_index.size(); scell++ ) {
        //     for (unsigned int ip = particles->first_index[scell] ; ip  < particles->last_index[scell] ; ip ++) {
        //
        //         double xmin = patch->getDomainLocalMin(0);
        //         double xmax = patch->getDomainLocalMax(0);
        //         double ymin = patch->getDomainLocalMin(1);
        //         double ymax = patch->getDomainLocalMax(1);
        //
        //         double x = particles->position(0,ip);
        //         double y = particles->position(1,ip);
        //         double mx = particles->momentum(0,ip);
        //         double my = particles->momentum(1,ip);
        //         double mz = particles->momentum(2,ip);
        //         double v = sqrt(mx*mx+my*my+mz*mz)/sqrt(1+mx*mx+my*my+mz*mz);
        //         if (particles->cell_keys[ip] < 0 || particles->cell_keys[ip] > particles->last_index.back()) {
        //         std::cerr
        //         << " Npc: " << particles->last_index[scell] - particles->first_index[scell]
        //         << " Cell keys size: " << particles->cell_keys.size()
        //         << " ip: "<< ip
        //         << " cell_keys: " << particles->cell_keys[ip]
        //         << ", x: " << xmin
        //         << " < " << x
        //         << " < " << xmax
        //         << ", y: " << ymin
        //         << " < " << y
        //         << " < " << ymax
        //         << ", mx: " << mx
        //         << ", my: " << my
        //         << ", mz: " << mz
        //         << setprecision(10)
        //         << ", v: " << v
        //         << std::endl;
        //         ERROR("")
        //
        //         if (x <= xmin
        //             || x >= xmax
        //             || y <= ymin
        //             || y >= ymax
        //             || v >= 1) {
        //             ERROR("")
        //         }
        //         }
        //     }
        // }
        
        // -------------------------------------------------------------------------------------
        
    }
}


// ---------------------------------------------------------------------------------------------------------------------
// For all particles of the species reacting to laser envelope
//   - interpolate the fields at the particle position
//   - deposit susceptibility
//   - calculate the new momentum
// ---------------------------------------------------------------------------------------------------------------------
void SpeciesV::ponderomotiveUpdateSusceptibilityAndMomentum( double time_dual, unsigned int ispec,
        ElectroMagn *EMfields,
        Params &params, bool diag_flag,
        Patch *patch, SmileiMPI *smpi,
        std::vector<Diagnostic *> &localDiags )
{

////////////////////////////// new vectorized
    int ithread;
#ifdef _OPENMP
    ithread = omp_get_thread_num();
#else
    ithread = 0;
#endif

#ifdef  __DETAILED_TIMERS
    double timer;
#endif

    if( npack_==0 ) {
        npack_    = 1;
        packsize_ = ( f_dim1-2*oversize[1] );

        //if ( (long int)particles->last_index.back() < (long int)60000 || (Radiate) || (Ionize) || (Multiphoton_Breit_Wheeler_process) )
        packsize_ *= ( f_dim0-2*oversize[0] );
        //else
        //    npack_ *= (f_dim0-2*oversize[0]);

        if( nDim_particle == 3 ) {
            packsize_ *= ( f_dim2-2*oversize[2] );
        }
    }


    // -------------------------------
    // calculate the particle dynamics
    // -------------------------------
    if( time_dual>time_frozen_ ) { // advance particle momentum

        for( unsigned int ipack = 0 ; ipack < npack_ ; ipack++ ) {

            // ipack start @ particles->first_index [ ipack * packsize_ ]
            // ipack end   @ particles->last_index [ ipack * packsize_ + packsize_ - 1 ]
            //int nparts_in_pack = particles->last_index[ (ipack+1) * packsize_-1 ] - particles->first_index [ ipack * packsize_ ];
            int nparts_in_pack = particles->last_index[( ipack+1 ) * packsize_-1 ];
            smpi->dynamics_resize( ithread, nDim_field, nparts_in_pack );

#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif
            // Interpolate the fields at the particle position
            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                Interp->fieldsAndEnvelope( EMfields, *particles, smpi, &( particles->first_index[ipack*packsize_+scell] ), &( particles->last_index[ipack*packsize_+scell] ), ithread, particles->first_index[ipack*packsize_] );
            }
#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[7] += MPI_Wtime() - timer;
#endif

            // Ionization
            if( Ionize ) {
            
#ifdef  __DETAILED_TIMERS
                timer = MPI_Wtime();
#endif
                vector<double> *Epart = &( smpi->dynamics_Epart[ithread] );
                vector<double> *EnvEabs_part  = &( smpi->dynamics_EnvEabs_part[ithread] );
                vector<double> *EnvExabs_part = &( smpi->dynamics_EnvExabs_part[ithread] );
                vector<double> *Phipart = &( smpi->dynamics_PHIpart[ithread] );
                for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                    Interp->envelopeFieldForIonization( EMfields, *particles, smpi, &( particles->first_index[ipack*packsize_+scell] ), &( particles->last_index[ipack*packsize_+scell] ), ithread );
                    Ionize->envelopeIonization( particles, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], Epart, EnvEabs_part, EnvExabs_part, Phipart, patch, Proj );
                }
#ifdef  __DETAILED_TIMERS
                patch->patch_timers_[4] += MPI_Wtime() - timer;
#endif
            }



            // Project susceptibility, the source term of envelope equation
#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif
            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                Proj->susceptibility( EMfields, *particles, mass_, smpi, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], ithread, ipack*packsize_+scell, particles->first_index[ipack*packsize_] );
            }

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[8] += MPI_Wtime() - timer;
#endif

            // Push the particles
#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif
            ( *Push )( *particles, smpi, particles->first_index[ipack*packsize_], particles->last_index[ipack*packsize_+packsize_-1], ithread, particles->first_index[ipack*packsize_] );
#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[9] += MPI_Wtime() - timer;
#endif
        }

    } else { // immobile particle (at the moment only project density)

    }//END if time vs. time_frozen_

} // end ponderomotiveUpdateSusceptibilityAndMomentum

// ---------------------------------------------------------------------------------------------------------------------
// For all particles of the species reacting to laser envelope
//   - interpolate the fields at the particle position
//   - deposit susceptibility
// ---------------------------------------------------------------------------------------------------------------------
void SpeciesV::ponderomotiveProjectSusceptibility( double time_dual, unsigned int ispec,
        ElectroMagn *EMfields,
        Params &params, bool diag_flag,
        Patch *patch, SmileiMPI *smpi,
        std::vector<Diagnostic *> &localDiags )
{

////////////////////////////// new vectorized
    int ithread;
#ifdef _OPENMP
    ithread = omp_get_thread_num();
#else
    ithread = 0;
#endif

#ifdef  __DETAILED_TIMERS
    double timer;
#endif

    if( npack_==0 ) {
        npack_    = 1;
        packsize_ = ( f_dim1-2*oversize[1] );

        //if ( (long int)particles->last_index.back() < (long int)60000 || (Radiate) || (Ionize) || (Multiphoton_Breit_Wheeler_process) )
        packsize_ *= ( f_dim0-2*oversize[0] );
        //else
        //    npack_ *= (f_dim0-2*oversize[0]);

        if( nDim_particle == 3 ) {
            packsize_ *= ( f_dim2-2*oversize[2] );
        }
    }


    // -------------------------------
    // calculate the particle dynamics
    // -------------------------------
    if( time_dual>time_frozen_ ) { // advance particle momentum

        for( unsigned int ipack = 0 ; ipack < npack_ ; ipack++ ) {

            // ipack start @ particles->first_index [ ipack * packsize_ ]
            // ipack end   @ particles->last_index [ ipack * packsize_ + packsize_ - 1 ]
            //int nparts_in_pack = particles->last_index[ (ipack+1) * packsize_-1 ] - particles->first_index [ ipack * packsize_ ];
            int nparts_in_pack = particles->last_index[( ipack+1 ) * packsize_-1 ];
            smpi->dynamics_resize( ithread, nDim_field, nparts_in_pack );

#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif
            // Interpolate the fields at the particle position
            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                Interp->fieldsAndEnvelope( EMfields, *particles, smpi, &( particles->first_index[ipack*packsize_+scell] ), &( particles->last_index[ipack*packsize_+scell] ), ithread, particles->first_index[ipack*packsize_] );
            }
#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[4] += MPI_Wtime() - timer;
#endif

            // Project susceptibility, the source term of envelope equation
#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif
            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                Proj->susceptibility( EMfields, *particles, mass_, smpi, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], ithread, ipack*packsize_+scell, particles->first_index[ipack*packsize_] );
            }

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[8] += MPI_Wtime() - timer;
#endif

        }

    } else { // immobile particle (at the moment only project density)

    }//END if time vs. time_frozen_

} // end ponderomotiveProjectSusceptibility

// ---------------------------------------------------------------------------------------------------------------------
// For all particles of the species reacting to laser envelope
//   - interpolate the ponderomotive potential and its gradient at the particle position, for present and previous timestep
//   - calculate the new particle position
//   - particles BC
//   - project charge and current density
// ---------------------------------------------------------------------------------------------------------------------
void SpeciesV::ponderomotiveUpdatePositionAndCurrents( double time_dual, unsigned int ispec,
        ElectroMagn *EMfields,
        Params &params, bool diag_flag, PartWalls *partWalls,
        Patch *patch, SmileiMPI *smpi,
        std::vector<Diagnostic *> &localDiags )
{


    int ithread;
#ifdef _OPENMP
    ithread = omp_get_thread_num();
#else
    ithread = 0;
#endif

#ifdef  __DETAILED_TIMERS
    double timer;
#endif

    unsigned int iPart;

    int tid( 0 );
    std::vector<double> nrj_lost_per_thd( 1, 0. );

    // -------------------------------
    // calculate the particle dynamics
    // -------------------------------
    if( time_dual>time_frozen_ ) { // moving particle

        //Prepare for sorting
        for( unsigned int i=0; i<count.size(); i++ ) {
            count[i] = 0;
        }

        for( unsigned int ipack = 0 ; ipack < npack_ ; ipack++ ) {

            //int nparts_in_pack = particles->last_index[ (ipack+1) * packsize_-1 ] - particles->first_index [ ipack * packsize_ ];
            int nparts_in_pack = particles->last_index[( ipack+1 ) * packsize_-1 ];
            smpi->dynamics_resize( ithread, nDim_field, nparts_in_pack );

#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif
            // Interpolate the fields at the particle position
            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                Interp->timeCenteredEnvelope( EMfields, *particles, smpi, &( particles->first_index[ipack*packsize_+scell] ), &( particles->last_index[ipack*packsize_+scell] ), ithread, particles->first_index[ipack*packsize_] );
            }
#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[10] += MPI_Wtime() - timer;
#endif

#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif
            // Push only the particle position
            ( *Push_ponderomotive_position )( *particles, smpi, particles->first_index[ipack*packsize_], particles->last_index[ipack*packsize_+packsize_-1], ithread, particles->first_index[ipack*packsize_] );
#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[11] += MPI_Wtime() - timer;
            timer = MPI_Wtime();
#endif
            unsigned int length[3];
            length[0]=0;
            length[1]=params.n_space[1]+1;
            length[2]=params.n_space[2]+1;

            for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                double ener_iPart( 0. );
                // Apply wall and boundary conditions
                if( mass_>0 ) { // condition mass_>0
                    for( unsigned int iwall=0; iwall<partWalls->size(); iwall++ ) {
                        ( *partWalls )[iwall]->apply( *particles, smpi, particles->first_index[scell], particles->last_index[scell], this, ithread, ener_iPart );
                        nrj_lost_per_thd[tid] += mass_ * ener_iPart;
                    }

                    // Boundary Condition may be physical or due to domain decomposition
                    // apply returns 0 if iPart is not in the local domain anymore

                    partBoundCond->apply( *particles, smpi, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], this, ithread, ener_iPart );
                    nrj_lost_per_thd[tid] += mass_ * ener_iPart;

                    for( iPart=particles->first_index[ipack*packsize_+scell] ; ( int )iPart<particles->last_index[ipack*packsize_+scell]; iPart++ ) {
                        if ( particles->cell_keys[iPart] != -1 ) {
                            //First reduction of the count sort algorithm. Lost particles are not included.
                            for( int i = 0 ; i<( int )nDim_field; i++ ) {
                                particles->cell_keys[iPart] *= length[i];
                                particles->cell_keys[iPart] += round( ((this)->*(distance[i]))(particles, i, iPart) * dx_inv_[i] );
                            }
                            count[particles->cell_keys[iPart]] ++; //First reduction of the count sort algorithm. Lost particles are not included.
                        }
                    }
                } else if( mass_==0 ) { // condition mass_=0
                    ERROR( "Particles with zero mass cannot interact with envelope" );
                }
            }
            //START EXCHANGE PARTICLES OF THE CURRENT BIN ?
#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[3] += MPI_Wtime() - timer;
#endif

            // Project currents if not a Test species and charges as well if a diag is needed.
            // Do not project if a photon
#ifdef  __DETAILED_TIMERS
            timer = MPI_Wtime();
#endif
            if( ( !particles->is_test ) && ( mass_ > 0 ) )
                for( unsigned int scell = 0 ; scell < packsize_ ; scell++ ) {
                    Proj->currentsAndDensityWrapper( EMfields, *particles, smpi, particles->first_index[ipack*packsize_+scell], particles->last_index[ipack*packsize_+scell], ithread, diag_flag, params.is_spectral, ispec, ipack*packsize_+scell, particles->first_index[ipack*packsize_] );
                }

#ifdef  __DETAILED_TIMERS
            patch->patch_timers_[12] += MPI_Wtime() - timer;
#endif
        }

        for( unsigned int ithd=0 ; ithd<nrj_lost_per_thd.size() ; ithd++ ) {
            nrj_bc_lost += nrj_lost_per_thd[tid];
        }

    } else { // immobile particle (at the moment only project density)
        if( diag_flag &&( !particles->is_test ) ) {
            double *b_rho=nullptr;
            for( unsigned int scell = 0 ; scell < particles->first_index.size() ; scell ++ ) {

                if( nDim_field==2 ) {
                    b_rho = EMfields->rho_s[ispec] ? &( *EMfields->rho_s[ispec] )( 0 ) : &( *EMfields->rho_ )( 0 ) ;
                }
                if( nDim_field==3 ) {
                    b_rho = EMfields->rho_s[ispec] ? &( *EMfields->rho_s[ispec] )( 0 ) : &( *EMfields->rho_ )( 0 ) ;
                } else if( nDim_field==1 ) {
                    b_rho = EMfields->rho_s[ispec] ? &( *EMfields->rho_s[ispec] )( 0 ) : &( *EMfields->rho_ )( 0 ) ;
                }
                for( iPart=particles->first_index[scell] ; ( int )iPart<particles->last_index[scell]; iPart++ ) {
                    Proj->basic( b_rho, ( *particles ), iPart, 0 );
                } //End loop on particles
            }
        }
    }//END if time vs. time_frozen_

} // end ponderomotiveUpdatePositionAndCurrents
