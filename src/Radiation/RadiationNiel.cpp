// ----------------------------------------------------------------------------
//! \file RadiationNiel.cpp
//
//! \brief This file implements the class RadiationNiel.
//!        This class is for the semi-classical Fokker-Planck model of
//!        synchrotron-like radiation loss developped by Niel et al. as an
//!        extension of the classical Landau-Lifshitz model in the weak quantum
//!        regime.
//!        This model includew a quantum correction + stochastic diffusive
//!        operator.
//
//! \details See these references for more information.
//! F. Niel et al., 2017
//! L. D. Landau and E. M. Lifshitz, The classical theory of fields, 1947
// ----------------------------------------------------------------------------

#include "RadiationNiel.h"

// -----------------------------------------------------------------------------
//! Constructor for RadiationNLL
//! Inherited from Radiation
// -----------------------------------------------------------------------------
RadiationNiel::RadiationNiel(Params& params, Species * species)
      : Radiation(params, species)
{
}

// -----------------------------------------------------------------------------
//! Destructor for RadiationNiel
// -----------------------------------------------------------------------------
RadiationNiel::~RadiationNiel()
{
}

// -----------------------------------------------------------------------------
//! Overloading of the operator (): perform the corrected Landau-Lifshitz
//! classical radiation reaction + stochastic diffusive operator.
//! **Non-vectorized version**
//
//! \param particles   particle object containing the particle properties
//! \param smpi        MPI properties
//! \param RadiationTables Cross-section data tables and useful functions
//                     for nonlinear inverse Compton scattering
//! \param istart      Index of the first particle
//! \param iend        Index of the last particle
//! \param ithread     Thread index
// -----------------------------------------------------------------------------
/*void RadiationNiel::operator() (Particles &particles,
        SmileiMPI* smpi,
        RadiationTables &RadiationTables,
        int istart,
        int iend,
        int ithread)
{

    // _______________________________________________________________
    // Parameters
    std::vector<LocalFields> *Epart = &(smpi->dynamics_Epart[ithread]);
    std::vector<LocalFields> *Bpart = &(smpi->dynamics_Bpart[ithread]);
    //std::vector<double> *invgf = &(smpi->dynamics_invgf[ithread]);

    // Charge divided by the square of the mass
    double charge_over_mass2;

    // 1/mass^2
    double one_over_mass_2 = pow(one_over_mass_,2.);

    // Sqrt(dt), used intensively in these loops
    double sqrtdt = sqrt(dt);

    // Temporary quantum parameter
    double chipa;

    // Temporary Lorentz factor
    double gamma;

    // Temporary double parameter
    double temp;

    // Radiated energy
    double rad_energy;

    // Stochastic diffusive term fo Niel et al.
    double diffusion;

    // Momentum shortcut
    double* momentum[3];
    for ( int i = 0 ; i<3 ; i++ )
        momentum[i] =  &( particles.momentum(i,0) );

    // Charge shortcut
    short* charge = &( particles.charge(0) );

    // Weight shortcut
    double* weight = &( particles.weight(0) );

    // Optical depth for the Monte-Carlo process
    // double* chi = &( particles.chi(0));

    // Reinitialize the cumulative radiated energy for the current thread
    this->radiated_energy = 0.;

    // _______________________________________________________________
    // Computation

    //#pragma omp simd
    for (int ipart=istart ; ipart<iend; ipart++ ) {
        charge_over_mass2 = (double)(charge[ipart])*one_over_mass_2;

        // Gamma
        gamma = sqrt(1.0 + momentum[0][ipart]*momentum[0][ipart]
                             + momentum[1][ipart]*momentum[1][ipart]
                             + momentum[2][ipart]*momentum[2][ipart]);

        // Computation of the Lorentz invariant quantum parameter
        chipa = Radiation::compute_chipa(charge_over_mass2,
                     momentum[0][ipart],momentum[1][ipart],momentum[2][ipart],
                     gamma,
                     (*Epart)[ipart].x,(*Epart)[ipart].y,(*Epart)[ipart].z,
                     (*Bpart)[ipart].x,(*Bpart)[ipart].y,(*Bpart)[ipart].z);

         // Radiated energy during the time step
         rad_energy =
         RadiationTables.get_corrected_cont_rad_energy_Ridgers(chipa,dt);

        // Below chipa = 1E-3, radiation losses are negligible
        if (chipa > 1E-3)
        {

            // Diffusive stochastic component during dt
            diffusion = RadiationTables.get_Niel_stochastic_term(gamma,
                                                                 chipa,sqrtdt);
        }
        else
        {
            diffusion = 0;
        }

        // Effect on the momentum
        // Temporary factor
        temp = (rad_energy - diffusion)*gamma/(gamma*gamma-1.);

        // Update of the momentum
        momentum[0][ipart] -= temp*momentum[0][ipart];
        momentum[1][ipart] -= temp*momentum[1][ipart];
        momentum[2][ipart] -= temp*momentum[2][ipart];

        // Incrementation of the radiated energy cumulative parameter
        radiated_energy += weight[ipart]*(gamma - sqrt(1.0
                            + momentum[0][ipart]*momentum[0][ipart]
                            + momentum[1][ipart]*momentum[1][ipart]
                            + momentum[2][ipart]*momentum[2][ipart]));

    }

}*/

// -----------------------------------------------------------------------------
//! Overloading of the operator (): perform the corrected Landau-Lifshitz
//! classical radiation reaction + stochastic diffusive operator.
//! **Vectorized version** But needs to be improved
//
//! \param particles   particle object containing the particle properties
//! \param smpi        MPI properties
//! \param RadiationTables Cross-section data tables and useful functions
//                     for nonlinear inverse Compton scattering
//! \param istart      Index of the first particle
//! \param iend        Index of the last particle
//! \param ithread     Thread index
// -----------------------------------------------------------------------------
void RadiationNiel::operator() (
        Particles &particles,
        Species * photon_species,
        SmileiMPI* smpi,
        RadiationTables &RadiationTables,
        int istart,
        int iend,
        int ithread)
{

    // _______________________________________________________________
    // Parameters
    LocalFields *Epart = &(smpi->dynamics_Epart[ithread][istart]);
    LocalFields *Bpart = &(smpi->dynamics_Bpart[ithread][istart]);
    // Used to store gamma directly
    double *gamma = &(smpi->dynamics_invgf[ithread][istart]);

    // Charge divided by the square of the mass
    double charge_over_mass2;

    // 1/mass^2
    const double one_over_mass_2 = pow(one_over_mass_,2.);

    // Sqrt(dt), used intensively in these loops
    const double sqrtdt = sqrt(dt);

    // Number of particles
    const int nbparticles = iend-istart;

    // Temporary double parameter
    double temp;

    // Particle id
    int ipart;

    // Radiated energy
    double rad_energy;

    // Stochastic diffusive term fo Niel et al.
    double diffusion[nbparticles];

    // Random Number
    double random_numbers[nbparticles];

    // Momentum shortcut
    double* momentum[3];
    for ( int i = 0 ; i<3 ; i++ )
        momentum[i] =  &( particles.momentum(i,istart) );

    // Charge shortcut
    short* charge = &( particles.charge(istart) );

    // Weight shortcut
    double* weight = &( particles.weight(istart) );

    // Quantum parameter
    double * chipa = &( particles.chi(istart));

    // Reinitialize the cumulative radiated energy for the current thread
    this->radiated_energy = 0.;

    const double chipa_radiation_threshold = RadiationTables.get_chipa_radiation_threshold();
    const double factor_cla_rad_power = RadiationTables.get_factor_cla_rad_power();

    // _______________________________________________________________
    // Computation

    //double t0 = MPI_Wtime();

    #pragma omp simd
    for (ipart=0 ; ipart< nbparticles; ipart++ )
    {

        charge_over_mass2 = (double)(charge[ipart])*one_over_mass_2;

        // Gamma
        gamma[ipart] = sqrt(1.0 + momentum[0][ipart]*momentum[0][ipart]
                             + momentum[1][ipart]*momentum[1][ipart]
                             + momentum[2][ipart]*momentum[2][ipart]);

        // Computation of the Lorentz invariant quantum parameter
        chipa[ipart] = Radiation::compute_chipa(charge_over_mass2,
                     momentum[0][ipart],momentum[1][ipart],momentum[2][ipart],
                     gamma[ipart],
                     Epart[ipart].x,Epart[ipart].y,Epart[ipart].z,
                     Bpart[ipart].x,Bpart[ipart].y,Bpart[ipart].z);
    }

    //double t1 = MPI_Wtime();

    // Non-vectorized
    for (ipart=0 ; ipart < nbparticles; ipart++ )
    {

        // Below chipa = chipa_radiation_threshold, radiation losses are negligible
        if (chipa[ipart] > chipa_radiation_threshold)
        {

          // Pick a random number in the normal distribution of standard
          // deviation sqrt(dt) (variance dt)
          random_numbers[ipart] = Rand::normal(sqrtdt);
        }
    }

    //double t2 = MPI_Wtime();

    // _______________________________________________________________
    // Computation of the diffusion coefficients

    // Use of the external table
    /*for (i=0 ; i < nbparticles; i++ )
    {

        // Particle number
        ipart = istart + i;

        // Below chipa = chipa_radiation_threshold, radiation losses are negligible
        if (chipa[ipart] > RadiationTables.get_chipa_radiation_threshold())
        {

            // Diffusive stochastic component during dt
            diffusion[i] = RadiationTables.get_Niel_stochastic_term((*gamma)[ipart],
                                                        chipa[ipart],sqrtdt);
        }
        else
        {
            diffusion[i] = 0.;
        }
    }*/

    double h;

    // Use of the fit
    #pragma omp simd
    for (ipart=0 ; ipart < nbparticles; ipart++ )
    {

        // Below chipa = chipa_radiation_threshold, radiation losses are negligible
        if (chipa[ipart] > chipa_radiation_threshold)
        {

          //h = RadiationTables.get_h_Niel_from_fit(chipa[ipart]);
          h = RadiationTables.get_h_Niel_from_table(chipa[ipart]);

          /*std::random_device device;
          std::mt19937 gen(device());
          std::normal_distribution<double> normal_distribution(0., sqrt(dt));
          r = normal_distribution(gen);*/

          diffusion[ipart] = sqrt(factor_cla_rad_power*gamma[ipart]*h)*random_numbers[ipart];
        }
    }

    //double t3 = MPI_Wtime();

    // _______________________________________________________________
    // Update of the momentum

    #pragma omp simd
    for (ipart=0 ; ipart<nbparticles; ipart++ )
    {
        // Below chipa = chipa_radiation_threshold, radiation losses are negligible
        if (chipa[ipart] > chipa_radiation_threshold)
        {

            // Radiated energy during the time step
            rad_energy =
            RadiationTables.get_corrected_cont_rad_energy_Ridgers(chipa[ipart],dt);

            // Effect on the momentum
            // Temporary factor
            temp = (rad_energy - diffusion[ipart])
                 * gamma[ipart]/(gamma[ipart]*gamma[ipart]-1.);

            // Update of the momentum
            momentum[0][ipart] -= temp*momentum[0][ipart];
            momentum[1][ipart] -= temp*momentum[1][ipart];
            momentum[2][ipart] -= temp*momentum[2][ipart];

        }
    }

    //double t4 = MPI_Wtime();

    // _______________________________________________________________
    // Computation of the thread radiated energy

    double radiated_energy_loc = 0;

    #pragma omp simd reduction(+:radiated_energy_loc)
    for (int ipart=0 ; ipart<nbparticles; ipart++ )
    {
        radiated_energy_loc += weight[ipart]*(gamma[ipart] - sqrt(1.0
                            + momentum[0][ipart]*momentum[0][ipart]
                            + momentum[1][ipart]*momentum[1][ipart]
                            + momentum[2][ipart]*momentum[2][ipart]));
    }
    radiated_energy += radiated_energy_loc;

    //double t5 = MPI_Wtime();

    //std::cerr << "" << std::endl;
    //std::cerr << "" << istart << " " << nbparticles << " " << ithread << std::endl;
    //std::cerr << "Computation of chi: " << t1 - t0 << std::endl;
    //std::cerr << "Computation of random numbers: " << t2 - t1 << std::endl;
    //std::cerr << "Computation of the diffusion: " << t3 - t2 << std::endl;
    //std::cerr << "Computation of the momentum: " << t4 - t3 << std::endl;
    //std::cerr << "Computation of the radiated energy: " << t5 - t4 << std::endl;
}
