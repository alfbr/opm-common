// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  Copyright 2023 SINTEF Digital, Mathematics and Cybernetics.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/

#ifndef OPM_GENERICFLUIDSYSTEM_HH
#define OPM_GENERICFLUIDSYSTEM_HH

#include <opm/common/OpmLog/OpmLog.hpp>

#include <opm/material/fluidsystems/BaseFluidSystem.hpp>
#include <opm/material/fluidsystems/PTFlashParameterCache.hpp> // TODO: this is something else need to check
#include <opm/material/viscositymodels/LBC.hpp>

#include <cassert>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace Opm {

    template <typename Scalar>
    struct ComponentParam {
        std::string name;
        Scalar molar_mass;
        Scalar critic_temp;
        Scalar critic_pres;
        Scalar critic_vol;
        Scalar acentric_factor;

        ComponentParam(const std::string_view name_, const Scalar molar_mass_, const Scalar critic_temp_,
                       const Scalar critic_pres_, const Scalar critic_vol_, const Scalar acentric_factor_)
                       : name(name_),
                         molar_mass(molar_mass_),
                         critic_temp(critic_temp_),
                         critic_pres(critic_pres_),
                         critic_vol(critic_vol_),
                         acentric_factor(acentric_factor_)
        {}
    };
/*!
 * \ingroup FluidSystem
 *
 * \brief A two phase system that can contain NumComp components
 *
 * \tparam Scalar  The floating-point type that specifies the precision of the numerical operations.
 * \tparam NumComp The number of the components in the fluid system.
 */
    template<class Scalar, int NumComp>
    class GenericFluidSystem : public BaseFluidSystem<Scalar, GenericFluidSystem<Scalar, NumComp> > {
    public:
        // TODO: I do not think these should be constant in fluidsystem, will try to make it non-constant later
        static const int numPhases=2;
        static const int numComponents = NumComp;
        static const int numMisciblePhases=2;
        static const int numMiscibleComponents = 3;
        // TODO: phase location should be more general
        static constexpr int oilPhaseIdx = 0;
        static constexpr int gasPhaseIdx = 1;

        template <class ValueType>
        using ParameterCache = Opm::PTFlashParameterCache<ValueType, GenericFluidSystem<Scalar, NumComp>>;
        using ViscosityModel = Opm::ViscosityModels<Scalar, GenericFluidSystem<Scalar, NumComp>>;
        using PengRobinsonMixture = Opm::PengRobinsonMixture<Scalar, GenericFluidSystem<Scalar, NumComp>>;

        template<typename Param>
        static void addComponent(const Param& param)
        {
            assert(component_param_.size() <= numComponents);
            if (component_param_.size() == numComponents) {
                const std::string msg =  fmt::format("the fluid system has reached maximum {} component, the component {} will not be added",
                                                     NumComp, param.name);
                OpmLog::note(msg);
            }
            component_param_.push_back(param);
        }

        static void init()
        {
            component_param_.reserve(numComponents);
        }

        /*!
         * \brief The acentric factor of a component [].
         *
         * \copydetails Doxygen::compIdxParam
         */
        static Scalar acentricFactor(unsigned compIdx)
        {
            return component_param_[compIdx].acentric_factor;
        }
        /*!
         * \brief Critical temperature of a component [K].
         *
         * \copydetails Doxygen::compIdxParam
         */
        static Scalar criticalTemperature(unsigned compIdx)
        {
            return component_param_[compIdx].critic_temp;
        }
        /*!
         * \brief Critical pressure of a component [Pa].
         *
         * \copydetails Doxygen::compIdxParam
         */
        static Scalar criticalPressure(unsigned compIdx)
        {
            return component_param_[compIdx].critic_pres;
        }
        /*!
        * \brief Critical volume of a component [m3].
        *
        * \copydetails Doxygen::compIdxParam
        */
        static Scalar criticalVolume(unsigned compIdx)
        {
            return component_param_[compIdx].critic_vol;
        }

        //! \copydoc BaseFluidSystem::molarMass
        static Scalar molarMass(unsigned compIdx)
        {
            return component_param_[compIdx].molar_mass;
        }

        /*!
         * \brief Returns the interaction coefficient for two components.
         *.
         */
        static Scalar interactionCoefficient(unsigned /*comp1Idx*/, unsigned /*comp2Idx*/)
        {
            // TODO: some data structure is needed to support this
            return 0.0;
        }

        //! \copydoc BaseFluidSystem::phaseName
        static std::string_view phaseName(unsigned phaseIdx)
        {
                static const char* name[] = {"o",  // oleic phase
                                             "g"};  // gas phase

                assert(phaseIdx < 2);
                return name[phaseIdx];
        }

        //! \copydoc BaseFluidSystem::componentName
        static std::string_view componentName(unsigned compIdx)
        {
            return component_param_[compIdx].name;
        }

        /*!
         * \copydoc BaseFluidSystem::density
         */
        template <class FluidState, class LhsEval = typename FluidState::Scalar, class ParamCacheEval = LhsEval>
        static LhsEval density(const FluidState& fluidState,
                               const ParameterCache<ParamCacheEval>& paramCache,
                               unsigned phaseIdx)
        {

            LhsEval dens;
            if (phaseIdx == oilPhaseIdx || phaseIdx == gasPhaseIdx) {
                dens = decay<LhsEval>(fluidState.averageMolarMass(phaseIdx) / paramCache.molarVolume(phaseIdx));
            }
            return dens;

        }

        //! \copydoc BaseFluidSystem::viscosity
        template <class FluidState, class LhsEval = typename FluidState::Scalar, class ParamCacheEval = LhsEval>
        static LhsEval viscosity(const FluidState& fluidState,
                                 const ParameterCache<ParamCacheEval>& paramCache,
                                 unsigned phaseIdx)
        {
            // Use LBC method to calculate viscosity
            return decay<LhsEval>(ViscosityModel::LBC(fluidState, paramCache, phaseIdx));
        }

        //! \copydoc BaseFluidSystem::fugacityCoefficient
        template <class FluidState, class LhsEval = typename FluidState::Scalar, class ParamCacheEval = LhsEval>
        static LhsEval fugacityCoefficient(const FluidState& fluidState,
                                           const ParameterCache<ParamCacheEval>& paramCache,
                                           unsigned phaseIdx,
                                           unsigned compIdx)
        {
            assert(phaseIdx < numPhases);
            assert(compIdx < numComponents);

            return decay<LhsEval>(PengRobinsonMixture::computeFugacityCoefficient(fluidState, paramCache, phaseIdx, compIdx));
        }

        //! \copydoc BaseFluidSystem::isCompressible
        static bool isCompressible([[maybe_unused]] unsigned phaseIdx)
        {
            assert(phaseIdx < numPhases);

            return true;
        }

        //! \copydoc BaseFluidSystem::isIdealMixture
        static bool isIdealMixture([[maybe_unused]] unsigned phaseIdx)
        {
            assert(phaseIdx < numPhases);

            return false;
        }

        //! \copydoc BaseFluidSystem::isLiquid
        static bool isLiquid(unsigned phaseIdx)
        {
            assert(phaseIdx < numPhases);

            return (phaseIdx == 0);
        }

        //! \copydoc BaseFluidSystem::isIdealGas
        static bool isIdealGas(unsigned phaseIdx)
        {
            assert(phaseIdx < numPhases);

            return (phaseIdx == 1);
        }
    private:
        static std::vector<ComponentParam<Scalar>> component_param_;
    };

    template <class Scalar, int NumComp>
    std::vector<ComponentParam<Scalar>>
    GenericFluidSystem<Scalar, NumComp>::component_param_;
}
#endif // OPM_GENERICFLUIDSYSTEM_HH
