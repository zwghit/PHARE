#ifndef PHARE_FLUID_PARTICLE_INITIALIZER_H
#define PHARE_FLUID_PARTICLE_INITIALIZER_H

#include <functional>
#include <memory>
#include <random>

#include "data/grid/gridlayoutdefs.h"
#include "data/ions/particle_initializers/particle_initializer.h"
#include "data/particles/particle.h"
#include "hybrid/hybrid_quantities.h"
#include "utilities/function/function.h"
#include "utilities/point/point.h"
#include "utilities/types.h"

namespace PHARE
{
void maxwellianVelocity(std::array<double, 3> V, std::array<double, 3> Vth,
                        std::mt19937_64 generator, std::array<double, 3>& partVelocity);


std::array<double, 3> basisTransform(const std::array<std::array<double, 3>, 3> basis,
                                     std::array<double, 3> vec);

void localMagneticBasis(std::array<double, 3> B, std::array<std::array<double, 3>, 3>& basis);




/** @brief a FluidParticleInitializer is a ParticleInitializer that loads particles from a local
 * Maxwellian distribution given density, bulk velocity and thermal velocity profiles.
 */
template<typename ParticleArray, typename GridLayout>
class FluidParticleInitializer : public ParticleInitializer<ParticleArray, GridLayout>
{
private:
    static constexpr auto dimension = GridLayout::dimension;

public:
    FluidParticleInitializer(std::unique_ptr<ScalarFunction<dimension>> density,
                             std::unique_ptr<VectorFunction<dimension>> bulkVelocity,
                             std::unique_ptr<VectorFunction<dimension>> thermalVelocity,
                             double particleCharge, uint32 nbrParticlesPerCell,
                             Basis basis = Basis::Cartesian,
                             std::unique_ptr<VectorFunction<dimension>> magneticField = nullptr)
        : density_{std::move(density)}
        , bulkVelocity_{std::move(bulkVelocity)}
        , thermalVelocity_{std::move(thermalVelocity)}
        , particleCharge_{particleCharge}
        , nbrParticlePerCell_{nbrParticlesPerCell}
        , basis_{basis}
        , magneticField_{std::move(magneticField)}
    {
    }



    /**
     * @brief load particles in a ParticleArray in a domain defined by the given layout
     */
    virtual void loadParticles(ParticleArray& particles, GridLayout const& layout) const override
    {
        if constexpr (dimension == 1)
        {
            loadParticles1D_(particles, layout);
        }
        else if constexpr (dimension == 2)
        {
            loadParticles2D_(particles, layout);
        }
        else if constexpr (dimension == 3)
        {
            loadParticles3D_(particles, layout);
        }
    }


    virtual ~FluidParticleInitializer() = default;



private:
    void loadParticles1D_(ParticleArray& particles, GridLayout const& layout) const
    {
        auto const meshSize = layout.meshSize();
        auto const& dx      = meshSize[0];

        /* get indices start and stop. we take primal/primal/primal because
           that is what GridLayout::cellCenteredCoordinate() requires */
        uint32 ix0 = layout.physicalStartIndex(QtyCentering::primal, Direction::X);
        uint32 ix1 = layout.physicalEndIndex(QtyCentering::primal, Direction::X);

        double cellVolume = dx;
        Point origin      = layout.origin();

        // random seed and generator needed to load maxwellian velocity
        // and random position with the cell
        std::random_device randSeed;
        // std::mt19937_64 generator(randSeed());
        std::mt19937_64 generator(1); // TODO constant seed should be usable for Debug mode.

        // beware: we're looping over the cell but use primal indices because of
        // GridLayout::cellCenteredCoordinates
        // therefore i(x,y,z)1 must be excluded.

        // grab references for convenience
        auto& density         = *density_;
        auto& bulkVelocity    = *bulkVelocity_;
        auto& thermalVelocity = *thermalVelocity_;
        auto& magneticField   = *magneticField_;

        for (uint32 ix = ix0; ix < ix1; ++ix)
        {
            double n;                  // cell centered density
            std::array<double, 3> Vth; // cell centered thermal speed
            std::array<double, 3> V;   // cell centered bulk velocity
            std::array<double, 3> particleVelocity;
            std::array<std::array<double, 3>, 3> basis;

            // get the coordinate of the current cell
            auto coord = layout.cellCenteredCoordinates(ix);
            auto x     = coord[0];

            // now get density, velocity and thermal speed values
            n   = density(x);
            V   = bulkVelocity(x);
            Vth = thermalVelocity(x);

            // weight for all particles in this cell
            auto cellWeight = n * cellVolume / nbrParticlePerCell_;

            std::uniform_real_distribution<float> randPosX(0., 1.);

            if (basis_ == Basis::Magnetic)
            {
                auto B = magneticField(x);
                localMagneticBasis(B, basis);
            }

            for (uint32 ipart = 0; ipart < nbrParticlePerCell_; ++ipart)
            {
                maxwellianVelocity(V, Vth, generator, particleVelocity);

                if (basis_ == Basis::Magnetic)
                {
                    particleVelocity = basisTransform(basis, particleVelocity);
                }

                std::array<float, dimension> delta = {{randPosX(generator)}};

                Particle<dimension> tmpParticle;
                tmpParticle.weight = cellWeight;
                tmpParticle.charge = particleCharge_;
                tmpParticle.iCell  = {{static_cast<int32>(ix)}};
                tmpParticle.delta  = delta;
                tmpParticle.v      = particleVelocity;

                particles.push_back(std::move(tmpParticle));
            }
        }
    }




    void loadParticles2D_(ParticleArray& particles, GridLayout const& layout) const
    {
        auto const meshSize = layout.meshSize();
        auto const& dx      = meshSize[0];
        auto const& dy      = meshSize[1];

        /* get indices start and stop. we take primal/primal/primal because
           that is what GridLayout::cellCenteredCoordinate() requires */
        auto ix0 = layout.physicalStartIndex(QtyCentering::primal, Direction::X);
        auto ix1 = layout.physicalEndIndex(QtyCentering::primal, Direction::X);
        auto iy0 = layout.physicalStartIndex(QtyCentering::primal, Direction::Y);
        auto iy1 = layout.physicalEndIndex(QtyCentering::primal, Direction::Y);

        auto cellVolume = dx * dy;
        auto origin     = layout.origin();

        // random seed and generator needed to load maxwellian velocity
        // and random position with the cell
        std::random_device randSeed;
        std::mt19937_64 generator(randSeed());

        // beware: we're looping over the cell but use primal indices because of
        // GridLayout::cellCenteredCoordinates
        // therefore i(x,y,z)1 must be excluded.

        // grab references for convenience
        auto& density         = *density_;
        auto& bulkVelocity    = *bulkVelocity_;
        auto& thermalVelocity = *thermalVelocity_;
        auto& magneticField   = *magneticField_;


        for (uint32 ix = ix0; ix < ix1; ++ix)
        {
            for (uint32 iy = iy0; iy < iy1; ++iy)
            {
                double n;                  // cell centered density
                std::array<double, 3> Vth; // cell centered thermal speed
                std::array<double, 3> V;   // cell centered bulk velocity
                std::array<double, 3> particleVelocity;
                std::array<std::array<double, 3>, 3> basis;

                // get the coordinate of the current cell
                auto coord = layout.cellCenteredCoordinates(ix, iy);
                auto x     = coord[0];
                auto y     = coord[1];

                // now get density, velocity and thermal speed values
                n   = density(x, y);
                V   = bulkVelocity(x, y);
                Vth = thermalVelocity(x, y);

                // weight for all particles in this cell
                auto cellWeight = n * cellVolume / nbrParticlePerCell_;
                std::uniform_real_distribution<float> randPosX(0., 1.);
                std::uniform_real_distribution<float> randPosY(0., 1.);

                if (basis_ == Basis::Magnetic)
                {
                    auto B = magneticField(x, y, origin.z);
                    localMagneticBasis(B, basis);
                }


                for (uint32 ipart = 0; ipart < nbrParticlePerCell_; ++ipart)
                {
                    maxwellianVelocity(V, Vth, generator, particleVelocity);

                    if (basis_ == Basis::Magnetic)
                    {
                        particleVelocity = basisTransform(basis, particleVelocity);
                    }

                    std::array<float, dimension> delta
                        = {{randPosX(generator), randPosY(generator)}};

                    Particle<dimension> tmpParticle;
                    tmpParticle.weight = cellWeight;
                    tmpParticle.charge = particleCharge_;
                    tmpParticle.iCell  = {{static_cast<int32>(ix), static_cast<int32>(iy)}};
                    tmpParticle.delta  = delta;
                    tmpParticle.v      = particleVelocity;

                    particles.push_back(std::move(tmpParticle));
                }
            }
        }
    }




    void loadParticles3D_(ParticleArray& particles, GridLayout const& layout) const
    {
        auto const meshSize = layout.meshSize();
        auto const& dx      = meshSize[0];
        auto const& dy      = meshSize[1];
        auto const& dz      = meshSize[2];

        /* get indices start and stop. we take primal/primal/primal because
           that is what GridLayout::cellCenteredCoordinate() requires */
        auto ix0 = layout.physicalStartIndex(QtyCentering::primal, Direction::X);
        auto ix1 = layout.physicalEndIndex(QtyCentering::primal, Direction::X);
        auto iy0 = layout.physicalStartIndex(QtyCentering::primal, Direction::Y);
        auto iy1 = layout.physicalEndIndex(QtyCentering::primal, Direction::Y);
        auto iz0 = layout.physicalStartIndex(QtyCentering::primal, Direction::Z);
        auto iz1 = layout.physicalEndIndex(QtyCentering::primal, Direction::Z);

        double cellVolume = dx * dy * dz;

        // random seed and generator needed to load maxwellian velocity
        // and random position with the cell
        std::random_device randSeed;
        std::mt19937_64 generator(randSeed());

        // beware: we're looping over the cell but use primal indices because of
        // GridLayout::cellCenteredCoordinates
        // therefore i(x,y,z)1 must be excluded.

        // grab references for convenience
        auto& density         = *density_;
        auto& bulkVelocity    = *bulkVelocity_;
        auto& thermalVelocity = *thermalVelocity_;
        auto& magneticField   = *magneticField_;

        for (uint32 ix = ix0; ix < ix1; ++ix)
        {
            for (uint32 iy = iy0; iy < iy1; ++iy)
            {
                for (uint32 iz = iz0; iz < iz1; ++iz)
                {
                    double n;                  // cell centered density
                    std::array<double, 3> Vth; // cell centered thermal speed
                    std::array<double, 3> V;   // cell centered bulk velocity
                    std::array<double, 3> particleVelocity;
                    std::array<std::array<double, 3>, 3> basis;

                    // get the coordinate of the current cell
                    auto coord = layout.cellCenteredCoordinates(ix, iy, iz);
                    auto x     = coord[0];
                    auto y     = coord[1];
                    auto z     = coord[2];

                    // now get density, velocity and thermal speed values
                    n   = density(x, y, z);
                    V   = bulkVelocity(x, y, z);
                    Vth = thermalVelocity(x, y, z);

                    // weight for all particles in this cell
                    auto cellWeight = n * cellVolume / nbrParticlePerCell_;

                    std::uniform_real_distribution<float> randPosX(0., 1.);
                    std::uniform_real_distribution<float> randPosY(0., 1.);
                    std::uniform_real_distribution<float> randPosZ(0., 1.);

                    if (basis_ == Basis::Magnetic)
                    {
                        auto B = magneticField(x, y, z);
                        localMagneticBasis(B, basis);
                    }

                    for (uint32 ipart = 0; ipart < nbrParticlePerCell_; ++ipart)
                    {
                        maxwellianVelocity(V, Vth, generator, particleVelocity);

                        if (basis_ == Basis::Magnetic)
                        {
                            particleVelocity = basisTransform(basis, particleVelocity);
                        }

                        std::array<float, 3> delta
                            = {{randPosX(generator), randPosY(generator), randPosZ(generator)}};


                        Particle<dimension> tmpParticle;
                        tmpParticle.weight = cellWeight;
                        tmpParticle.charge = particleCharge_;
                        tmpParticle.iCell  = {{static_cast<int32>(ix), static_cast<int32>(iy),
                                              static_cast<int32>(iz)}};
                        tmpParticle.delta  = delta;
                        tmpParticle.v      = particleVelocity;

                        particles.push_back(std::move(tmpParticle));
                    } // end particle looop
                }     // end z
            }         // end y
        }             // end x
    }




    std::unique_ptr<ScalarFunction<dimension>> density_;
    std::unique_ptr<VectorFunction<dimension>> bulkVelocity_;
    std::unique_ptr<VectorFunction<dimension>> thermalVelocity_;

    double particleCharge_;
    uint32 nbrParticlePerCell_;
    Basis basis_;
    std::unique_ptr<VectorFunction<dimension>> magneticField_;
};

} // namespace PHARE


#endif
