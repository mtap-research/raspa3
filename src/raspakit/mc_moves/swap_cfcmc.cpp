module;

#ifdef USE_LEGACY_HEADERS
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>
#endif

module mc_moves_swap_cfcmc;

#ifndef USE_LEGACY_HEADERS
import <complex>;
import <vector>;
import <array>;
import <tuple>;
import <optional>;
import <span>;
import <optional>;
import <tuple>;
import <algorithm>;
import <chrono>;
import <cmath>;
import <iostream>;
import <iomanip>;
import <type_traits>;
#endif

import component;
import molecule;
import atom;
import double3;
import double3x3;
import simd_quatd;
import simulationbox;
import cbmc;
import randomnumbers;
import system;
import energy_factor;
import energy_status;
import energy_status_inter;
import property_lambda_probability_histogram;
import property_widom;
import averages;
import running_energy;
import forcefield;
import move_statistics;
import mc_moves_probabilities_particles;
import interactions_framework_molecule;
import interactions_intermolecular;
import interactions_ewald;
import interactions_external_field;

std::pair<std::optional<RunningEnergy>, double3> MC_Moves::swapMove_CFCMC(RandomNumber& random, System& system,
                                                                          size_t selectedComponent,
                                                                          size_t selectedMolecule,
                                                                          bool insertionDisabled, bool deletionDisabled)
{
  std::chrono::system_clock::time_point time_begin, time_end;

  // Retrieve lambda parameters and select a new lambda bin for the move
  PropertyLambdaProbabilityHistogram& lambda = system.components[selectedComponent].lambdaGC;
  size_t oldBin = lambda.currentBin;
  double deltaLambda = lambda.delta;
  double maxChange = system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.maxChange[2];
  std::make_signed_t<std::size_t> selectedNewBin = lambda.selectNewBin(random, maxChange);
  size_t oldN = system.numberOfIntegerMoleculesPerComponent[selectedComponent];

  size_t indexFractionalMolecule = system.indexOfGCFractionalMoleculesPerComponent_CFCMC(selectedComponent);

  if (selectedNewBin >= std::make_signed_t<std::size_t>(lambda.numberOfSamplePoints))  // Insertion move
  {
    if (insertionDisabled)
    {
      return {std::nullopt, double3(0.0, 0.0, 0.0)};
    }

    // Steps for insertion Lambda_new = 1 + epsilon
    // ===================================================================
    // (1) Unbiased: the fractional molecule with lambda=lambda_old is made integer (lambda=1) and deltaU is computed
    // (2) Unbiased: a new fractional molecule is inserted with lambda_new = epsilon

    size_t newBin = static_cast<size_t>(selectedNewBin - std::make_signed_t<std::size_t>(lambda.numberOfSamplePoints));
    double newLambda = deltaLambda * static_cast<double>(newBin);

    // Update counts for insertion move
    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.counts[0] += 1;
    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalCounts[0] += 1;

    std::span<Atom> fractionalMolecule = system.spanOfMolecule(selectedComponent, indexFractionalMolecule);

    // Make copy of old fractional molecule for reference and restoring
    std::vector<Atom> oldFractionalMolecule(fractionalMolecule.begin(), fractionalMolecule.end());

    // Fractional particle becomes integer (lambda=1.0)
    for (Atom& atom : fractionalMolecule)
    {
      atom.setScalingToInteger();
    }

    if ((system.insideBlockedPockets(system.components[selectedComponent], fractionalMolecule)))
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute external field energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> externalFieldDifferenceStep1 = Interactions::computeExternalFieldEnergyDifference(
        system.hasExternalField, system.forceField, system.simulationBox, fractionalMolecule, oldFractionalMolecule);
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCExternalField +=
        (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCExternalField += (time_end - time_begin);
    if (!externalFieldDifferenceStep1.has_value())
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute framework-molecule energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> frameworkDifferenceStep1 = Interactions::computeFrameworkMoleculeEnergyDifference(
        system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(), fractionalMolecule,
        oldFractionalMolecule);
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCFramework +=
        (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCFramework += (time_end - time_begin);
    if (!frameworkDifferenceStep1.has_value())
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute molecule-molecule energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> moleculeDifferenceStep1 = Interactions::computeInterMolecularEnergyDifference(
        system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(), fractionalMolecule,
        oldFractionalMolecule);
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCMolecule +=
        (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCMolecule += (time_end - time_begin);
    if (!moleculeDifferenceStep1.has_value())
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());

      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute Ewald energy contribution
    time_begin = std::chrono::system_clock::now();
    RunningEnergy EwaldEnergyDifferenceStep1 = Interactions::energyDifferenceEwaldFourier(
        system.eik_x, system.eik_y, system.eik_z, system.eik_xy, system.storedEik, system.totalEik, system.forceField,
        system.simulationBox, fractionalMolecule, oldFractionalMolecule);
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCEwald += (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCEwald += (time_end - time_begin);

    // Compute tail-correction energy contribution
    time_begin = std::chrono::system_clock::now();
    RunningEnergy tailEnergyDifference1 = Interactions::computeInterMolecularTailEnergyDifference(
                                              system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(),
                                              fractionalMolecule, oldFractionalMolecule) +
                                          Interactions::computeFrameworkMoleculeTailEnergyDifference(
                                              system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(),
                                              fractionalMolecule, oldFractionalMolecule);
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCTail += (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCTail += (time_end - time_begin);

    RunningEnergy energyDifferenceStep1 = externalFieldDifferenceStep1.value() + frameworkDifferenceStep1.value() +
                                          moleculeDifferenceStep1.value() + EwaldEnergyDifferenceStep1 +
                                          tailEnergyDifference1;

    // Generate trial molecule for insertion
    std::pair<Molecule, std::vector<Atom>> trialMolecule =
        system.components[selectedComponent].equilibratedMoleculeRandomInBox(random, system.simulationBox);

    if ((system.insideBlockedPockets(system.components[selectedComponent], trialMolecule.second)))
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Copy atoms from the old fractional molecule, including the groupIds
    size_t upcomingMoleculeId = system.numberOfMoleculesPerComponent[selectedComponent];
    size_t groupId = static_cast<size_t>(oldFractionalMolecule.front().groupId);
    std::for_each(std::begin(trialMolecule.second), std::end(trialMolecule.second),
                  [selectedComponent, upcomingMoleculeId, groupId, newLambda](Atom& atom)
                  {
                    atom.moleculeId = static_cast<uint32_t>(upcomingMoleculeId);
                    atom.componentId = static_cast<uint8_t>(selectedComponent);
                    atom.groupId = static_cast<uint8_t>(groupId);
                    atom.setScaling(newLambda);
                  });

    if ((system.insideBlockedPockets(system.components[selectedComponent], trialMolecule.second)))
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute external field energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> externalFieldDifferenceStep2 = Interactions::computeExternalFieldEnergyDifference(
        system.hasExternalField, system.forceField, system.simulationBox, trialMolecule.second, {});
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCExternalField +=
        (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCExternalField += (time_end - time_begin);
    if (!externalFieldDifferenceStep2.has_value())
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute framework-molecule energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> frameworkDifferenceStep2 = Interactions::computeFrameworkMoleculeEnergyDifference(
        system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(), trialMolecule.second, {});
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCFramework +=
        (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCFramework += (time_end - time_begin);
    if (!frameworkDifferenceStep2.has_value())
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute molecule-molecule energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> moleculeDifferenceStep2 = Interactions::computeInterMolecularEnergyDifference(
        system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(), trialMolecule.second, {});
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCMolecule +=
        (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCMolecule += (time_end - time_begin);

    if (!moleculeDifferenceStep2.has_value())
    {
      // Reject, set fractional molecule back to old state
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());

      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute Ewald energy contribution
    time_begin = std::chrono::system_clock::now();
    RunningEnergy EwaldEnergyDifferenceStep2 = Interactions::energyDifferenceEwaldFourier(
        system.eik_x, system.eik_y, system.eik_z, system.eik_xy, system.totalEik, system.totalEik, system.forceField,
        system.simulationBox, trialMolecule.second, {});
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCEwald += (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCEwald += (time_end - time_begin);

    // Compute tail-correction energy contribution
    time_begin = std::chrono::system_clock::now();
    RunningEnergy tailEnergyDifference2 =
        Interactions::computeInterMolecularTailEnergyDifference(
            system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(), trialMolecule.second, {}) +
        Interactions::computeFrameworkMoleculeTailEnergyDifference(
            system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(), trialMolecule.second, {});
    time_end = std::chrono::system_clock::now();
    system.components[selectedComponent].mc_moves_cputime.swapLambdaInsertionMoveCFCMCTail += (time_end - time_begin);
    system.mc_moves_cputime.swapLambdaInsertionMoveCFCMCTail += (time_end - time_begin);

    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.constructed[0] += 1;
    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalConstructed[0] += 1;

    RunningEnergy energyDifferenceStep2 = externalFieldDifferenceStep2.value() + frameworkDifferenceStep2.value() +
                                          moleculeDifferenceStep2.value() + EwaldEnergyDifferenceStep2 +
                                          tailEnergyDifference2;

    // Calculate acceptance probability
    double fugacity = system.components[selectedComponent].fugacityCoefficient.value_or(1.0) * system.pressure;
    double preFactor = system.beta * system.components[selectedComponent].molFraction * fugacity *
                       system.simulationBox.volume /
                       static_cast<double>(1 + system.numberOfIntegerMoleculesPerComponent[selectedComponent]);
    double biasTerm = lambda.biasFactor[newBin] - lambda.biasFactor[oldBin];
    double Pacc =
        preFactor *
        exp(-system.beta * (energyDifferenceStep1.potentialEnergy() + energyDifferenceStep2.potentialEnergy()) +
            biasTerm);

    double biasTransitionMatrix = system.tmmc.biasFactor(oldN + 1, oldN);

    if (system.tmmc.doTMMC)
    {
      size_t newN = oldN + 1;
      if (newN > system.tmmc.maxMacrostate)
      {
        return {std::nullopt, double3(0.0, 1.0 - Pacc, Pacc)};
      }
    }

    // Apply acceptance/rejection rule
    if (random.uniform() < biasTransitionMatrix * Pacc)
    {
      Interactions::acceptEwaldMove(system.forceField, system.storedEik, system.totalEik);

      system.components[selectedComponent].lambdaGC.setCurrentBin(newBin);

      // Note: inserting invalidates iterators and spans (the vector could reallocate memory)
      system.insertMolecule(selectedComponent, trialMolecule.first, trialMolecule.second);

      // Swap molecules to maintain fractional molecule index
      size_t lastMoleculeId = system.numberOfMoleculesPerComponent[selectedComponent] - 1;
      std::span<Atom> lastMolecule = system.spanOfMolecule(selectedComponent, lastMoleculeId);
      fractionalMolecule = system.spanOfMolecule(selectedComponent, indexFractionalMolecule);
      std::swap_ranges(fractionalMolecule.begin(), fractionalMolecule.end(), lastMolecule.begin());
      std::swap(system.moleculePositions[system.moleculeIndexOfComponent(selectedComponent, indexFractionalMolecule)],
                system.moleculePositions[system.moleculeIndexOfComponent(selectedComponent, lastMoleculeId)]);

      system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.accepted[0] += 1;
      system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalAccepted[0] += 1;

      return {energyDifferenceStep1 + energyDifferenceStep2, double3(0.0, 1.0 - Pacc, Pacc)};
    };

    // Restore old lambda
    std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());

    return {std::nullopt, double3(0.0, 1.0, 0.0)};
  }
  else if (selectedNewBin < 0)  // Deletion move
  {
    if (deletionDisabled)
    {
      return {std::nullopt, double3(0.0, 0.0, 0.0)};
    }
    // Steps for deletion Lambda_new = -epsilon
    // ===================================================================
    // (1) Unbiased: the existing fractional molecule with lambda=lambda_o is removed.
    // (2) Unbiased: A new fractional molecule is chosen with lambda_new = 1 - epsilon, deltaU is computed.

    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.counts[1] += 1;
    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalCounts[1] += 1;

    if (system.numberOfIntegerMoleculesPerComponent[selectedComponent] > 0)
    {
      selectedMolecule = system.randomIntegerMoleculeOfComponent(random, selectedComponent);

      std::span<Atom> fractionalMolecule = system.spanOfMolecule(selectedComponent, indexFractionalMolecule);
      std::span<Atom> newFractionalMolecule = system.spanOfMolecule(selectedComponent, selectedMolecule);

      // Make copies of molecules for restoring if needed
      std::vector<Atom> oldFractionalMolecule(fractionalMolecule.begin(), fractionalMolecule.end());
      std::vector<Atom> oldNewFractionalMolecule(newFractionalMolecule.begin(), newFractionalMolecule.end());

      // Set scaling to zero for deletion
      for (Atom& atom : fractionalMolecule)
      {
        atom.setScalingFullyOff();
        atom.groupId = uint8_t{0};
      }

      if ((system.insideBlockedPockets(system.components[selectedComponent], fractionalMolecule)))
      {
        // Reject, set fractional molecule back to old state
        std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
        return {std::nullopt, double3(0.0, 1.0, 0.0)};
      }

      // Compute external field energy contribution
      time_begin = std::chrono::system_clock::now();
      std::optional<RunningEnergy> externalFieldDifferenceStep1 = Interactions::computeExternalFieldEnergyDifference(
          system.hasExternalField, system.forceField, system.simulationBox, fractionalMolecule, oldFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCExternalField +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCExternalField += (time_end - time_begin);
      if (!externalFieldDifferenceStep1.has_value())
      {
        // Reject, set fractional molecule back to old state
        std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
        return {std::nullopt, double3(0.0, 1.0, 0.0)};
      }

      // Compute framework-molecule energy contribution
      time_begin = std::chrono::system_clock::now();
      std::optional<RunningEnergy> frameworkDifferenceStep1 = Interactions::computeFrameworkMoleculeEnergyDifference(
          system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(), fractionalMolecule,
          oldFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCFramework +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCFramework += (time_end - time_begin);
      if (!frameworkDifferenceStep1.has_value())
      {
        // Reject, set fractional molecule back to old state
        std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
        return {std::nullopt, double3(0.0, 1.0, 0.0)};
      }

      // Compute molecule-molecule energy contribution
      time_begin = std::chrono::system_clock::now();
      std::optional<RunningEnergy> moleculeDifferenceStep1 = Interactions::computeInterMolecularEnergyDifference(
          system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(), fractionalMolecule,
          oldFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCMolecule +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCMolecule += (time_end - time_begin);
      if (!moleculeDifferenceStep1.has_value())
      {
        // Reject, set fractional molecule back to old state
        std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());

        return {std::nullopt, double3(0.0, 1.0, 0.0)};
      }

      // Compute Ewald energy contribution
      time_begin = std::chrono::system_clock::now();
      RunningEnergy EwaldEnergyDifferenceStep1 = Interactions::energyDifferenceEwaldFourier(
          system.eik_x, system.eik_y, system.eik_z, system.eik_xy, system.storedEik, system.totalEik, system.forceField,
          system.simulationBox, fractionalMolecule, oldFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCEwald += (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCEwald += (time_end - time_begin);

      // Compute tail-correction energy contribution
      time_begin = std::chrono::system_clock::now();
      RunningEnergy tailEnergyDifference1 = Interactions::computeInterMolecularTailEnergyDifference(
                                                system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(),
                                                fractionalMolecule, oldFractionalMolecule) +
                                            Interactions::computeFrameworkMoleculeTailEnergyDifference(
                                                system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(),
                                                fractionalMolecule, oldFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCTail += (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCTail += (time_end - time_begin);

      RunningEnergy energyDifferenceStep1 = externalFieldDifferenceStep1.value() + frameworkDifferenceStep1.value() +
                                            moleculeDifferenceStep1.value() + EwaldEnergyDifferenceStep1 +
                                            tailEnergyDifference1;

      std::vector<Atom> savedFractionalMolecule(newFractionalMolecule.begin(), newFractionalMolecule.end());

      // (2) Unbiased: A new fractional molecule is chosen with lambda_new = 1 - epsilon, deltaU is computed.
      size_t newBin =
          static_cast<size_t>(selectedNewBin + std::make_signed_t<std::size_t>(lambda.numberOfSamplePoints));
      double newLambda = deltaLambda * static_cast<double>(newBin);

      // Update new fractional molecule with new lambda
      std::transform(newFractionalMolecule.begin(), newFractionalMolecule.end(), oldFractionalMolecule.begin(),
                     newFractionalMolecule.begin(), [newLambda](const Atom& a, const Atom& b)
                     { return Atom(a.position, a.charge, newLambda, a.moleculeId, a.type, a.componentId, b.groupId); });

      // Compute external field energy contribution
      time_begin = std::chrono::system_clock::now();
      std::optional<RunningEnergy> externalFieldDifferenceStep2 = Interactions::computeExternalFieldEnergyDifference(
          system.hasExternalField, system.forceField, system.simulationBox, newFractionalMolecule,
          savedFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCExternalField +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCExternalField += (time_end - time_begin);
      if (!externalFieldDifferenceStep2.has_value())
      {
        // Restore molecules and reject
        std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
        std::copy(oldNewFractionalMolecule.begin(), oldNewFractionalMolecule.end(), newFractionalMolecule.begin());

        return {std::nullopt, double3(0.0, 1.0, 0.0)};
      }

      // Compute framework-molecule energy contribution
      time_begin = std::chrono::system_clock::now();
      std::optional<RunningEnergy> frameworkDifferenceStep2 = Interactions::computeFrameworkMoleculeEnergyDifference(
          system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(), newFractionalMolecule,
          savedFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCFramework +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCFramework += (time_end - time_begin);
      if (!frameworkDifferenceStep2.has_value())
      {
        // Restore molecules and reject
        std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
        std::copy(oldNewFractionalMolecule.begin(), oldNewFractionalMolecule.end(), newFractionalMolecule.begin());

        return {std::nullopt, double3(0.0, 1.0, 0.0)};
      }

      // Compute molecule-molecule energy contribution
      time_begin = std::chrono::system_clock::now();
      std::optional<RunningEnergy> moleculeDifferenceStep2 = Interactions::computeInterMolecularEnergyDifference(
          system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(), newFractionalMolecule,
          savedFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCMolecule +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCMolecule += (time_end - time_begin);
      if (!moleculeDifferenceStep2.has_value())
      {
        // Restore molecules and reject
        std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
        std::copy(oldNewFractionalMolecule.begin(), oldNewFractionalMolecule.end(), newFractionalMolecule.begin());

        return {std::nullopt, double3(0.0, 1.0, 0.0)};
      }

      // Compute Ewald energy contribution
      time_begin = std::chrono::system_clock::now();
      RunningEnergy EwaldEnergyDifferenceStep2 = Interactions::energyDifferenceEwaldFourier(
          system.eik_x, system.eik_y, system.eik_z, system.eik_xy, system.totalEik, system.totalEik, system.forceField,
          system.simulationBox, newFractionalMolecule, savedFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCEwald += (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCEwald += (time_end - time_begin);

      // Compute tail-correction energy contribution
      time_begin = std::chrono::system_clock::now();
      RunningEnergy tailEnergyDifferenceStep2 =
          Interactions::computeInterMolecularTailEnergyDifference(system.forceField, system.simulationBox,
                                                                  system.spanOfMoleculeAtoms(), newFractionalMolecule,
                                                                  savedFractionalMolecule) +
          Interactions::computeFrameworkMoleculeTailEnergyDifference(system.forceField, system.simulationBox,
                                                                     system.spanOfFrameworkAtoms(),
                                                                     newFractionalMolecule, savedFractionalMolecule);
      time_end = std::chrono::system_clock::now();
      system.components[selectedComponent].mc_moves_cputime.swapLambdaDeletionMoveCFCMCTail += (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaDeletionMoveCFCMCTail += (time_end - time_begin);

      RunningEnergy energyDifferenceStep2 = externalFieldDifferenceStep2.value() + frameworkDifferenceStep2.value() +
                                            moleculeDifferenceStep2.value() + EwaldEnergyDifferenceStep2 +
                                            tailEnergyDifferenceStep2;

      system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.constructed[1] += 1;
      system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalConstructed[1] += 1;

      // Calculate acceptance probability
      double fugacity = system.components[selectedComponent].fugacityCoefficient.value_or(1.0) * system.pressure;
      double preFactor =
          double(system.numberOfIntegerMoleculesPerComponent[selectedComponent]) /
          (system.beta * system.components[selectedComponent].molFraction * fugacity * system.simulationBox.volume);
      double biasTerm = lambda.biasFactor[newBin] - lambda.biasFactor[oldBin];
      double Pacc =
          preFactor *
          exp(-system.beta * (energyDifferenceStep1.potentialEnergy() + energyDifferenceStep2.potentialEnergy()) +
              biasTerm);

      double biasTransitionMatrix = system.tmmc.biasFactor(oldN - 1, oldN);

      if (system.tmmc.doTMMC)
      {
        size_t newN = oldN - 1;
        if (newN < system.tmmc.minMacrostate)
        {
          return {std::nullopt, double3(Pacc, 1.0 - Pacc, 0.0)};
        }
      }

      // Apply acceptance/rejection rule
      if (random.uniform() < biasTransitionMatrix * Pacc)
      {
        Interactions::acceptEwaldMove(system.forceField, system.storedEik, system.totalEik);
        system.components[selectedComponent].lambdaGC.setCurrentBin(newBin);

        // Swap molecules to maintain fractional molecule index
        std::swap_ranges(newFractionalMolecule.begin(), newFractionalMolecule.end(), fractionalMolecule.begin());
        std::swap(system.moleculePositions[system.moleculeIndexOfComponent(selectedComponent, selectedMolecule)],
                  system.moleculePositions[system.moleculeIndexOfComponent(selectedComponent, 0)]);

        system.deleteMolecule(selectedComponent, selectedMolecule, newFractionalMolecule);

        system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.accepted[1] += 1;
        system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalAccepted[1] += 1;

        return {energyDifferenceStep1 + energyDifferenceStep2, double3(Pacc, 1.0 - Pacc, 0.0)};
      };

      // Restore the old and the newly chosen fractional molecule
      std::copy(oldFractionalMolecule.begin(), oldFractionalMolecule.end(), fractionalMolecule.begin());
      std::copy(oldNewFractionalMolecule.begin(), oldNewFractionalMolecule.end(), newFractionalMolecule.begin());

      return {std::nullopt, double3(Pacc, 1.0 - Pacc, 0.0)};
    }
    return {std::nullopt, double3(0.0, 1.0, 0.0)};
  }
  else  // Lambda-move
  {
    size_t newBin = static_cast<size_t>(selectedNewBin);
    double newLambda = deltaLambda * static_cast<double>(newBin);

    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.counts[2] += 1;
    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalCounts[2] += 1;

    std::span<Atom> molecule = system.spanOfMolecule(selectedComponent, indexFractionalMolecule);

    // Update molecule with new lambda
    std::vector<Atom> trialPositions(molecule.begin(), molecule.end());
    std::transform(molecule.begin(), molecule.end(), trialPositions.begin(),
                   [&](Atom a)
                   {
                     a.setScaling(newLambda);
                     return a;
                   });

    if (system.insideBlockedPockets(system.components[selectedComponent], trialPositions))
    {
      return {std::nullopt, double3(0.0, 1.0, 0.0)};
    }

    // Compute external field energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> externalFieldEnergyDifference = Interactions::computeExternalFieldEnergyDifference(
        system.hasExternalField, system.forceField, system.simulationBox, trialPositions, molecule);
    time_end = std::chrono::system_clock::now();
    if (insertionDisabled || deletionDisabled)
    {
      system.components[selectedComponent].mc_moves_cputime.WidomMoveCFCMCExternalField += (time_end - time_begin);
      system.mc_moves_cputime.WidomMoveCFCMCExternalField += (time_end - time_begin);
    }
    else
    {
      system.components[selectedComponent].mc_moves_cputime.swapLambdaChangeMoveCFCMCExternalField +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaChangeMoveCFCMCExternalField += (time_end - time_begin);
    }
    if (!externalFieldEnergyDifference.has_value()) return {std::nullopt, double3(0.0, 1.0, 0.0)};

    // Compute framework-molecule energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> frameworkEnergyDifference = Interactions::computeFrameworkMoleculeEnergyDifference(
        system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(), trialPositions, molecule);
    time_end = std::chrono::system_clock::now();
    if (insertionDisabled || deletionDisabled)
    {
      system.components[selectedComponent].mc_moves_cputime.WidomMoveCFCMCFramework += (time_end - time_begin);
      system.mc_moves_cputime.WidomMoveCFCMCFramework += (time_end - time_begin);
    }
    else
    {
      system.components[selectedComponent].mc_moves_cputime.swapLambdaChangeMoveCFCMCFramework +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaChangeMoveCFCMCFramework += (time_end - time_begin);
    }
    if (!frameworkEnergyDifference.has_value()) return {std::nullopt, double3(0.0, 1.0, 0.0)};

    // Compute molecule-molecule energy contribution
    time_begin = std::chrono::system_clock::now();
    std::optional<RunningEnergy> interEnergyDifference = Interactions::computeInterMolecularEnergyDifference(
        system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(), trialPositions, molecule);
    time_end = std::chrono::system_clock::now();
    if (insertionDisabled || deletionDisabled)
    {
      system.components[selectedComponent].mc_moves_cputime.WidomMoveCFCMCMolecule += (time_end - time_begin);
      system.mc_moves_cputime.WidomMoveCFCMCMolecule += (time_end - time_begin);
    }
    else
    {
      system.components[selectedComponent].mc_moves_cputime.swapLambdaChangeMoveCFCMCMolecule +=
          (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaChangeMoveCFCMCMolecule += (time_end - time_begin);
    }
    if (!interEnergyDifference.has_value()) return {std::nullopt, double3(0.0, 1.0, 0.0)};

    // Compute Ewald energy contribution
    time_begin = std::chrono::system_clock::now();
    RunningEnergy EwaldFourierDifference = Interactions::energyDifferenceEwaldFourier(
        system.eik_x, system.eik_y, system.eik_z, system.eik_xy, system.storedEik, system.totalEik, system.forceField,
        system.simulationBox, trialPositions, molecule);
    time_end = std::chrono::system_clock::now();
    if (insertionDisabled || deletionDisabled)
    {
      system.components[selectedComponent].mc_moves_cputime.WidomMoveCFCMCEwald += (time_end - time_begin);
      system.mc_moves_cputime.WidomMoveCFCMCEwald += (time_end - time_begin);
    }
    else
    {
      system.components[selectedComponent].mc_moves_cputime.swapLambdaChangeMoveCFCMCEwald += (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaChangeMoveCFCMCEwald += (time_end - time_begin);
    }

    // Compute tail-correction energy contribution
    time_begin = std::chrono::system_clock::now();
    RunningEnergy tailEnergyDifference =
        Interactions::computeInterMolecularTailEnergyDifference(
            system.forceField, system.simulationBox, system.spanOfMoleculeAtoms(), trialPositions, molecule) +
        Interactions::computeFrameworkMoleculeTailEnergyDifference(
            system.forceField, system.simulationBox, system.spanOfFrameworkAtoms(), trialPositions, molecule);
    time_end = std::chrono::system_clock::now();
    if (insertionDisabled || deletionDisabled)
    {
      system.components[selectedComponent].mc_moves_cputime.WidomMoveCFCMCTail += (time_end - time_begin);
      system.mc_moves_cputime.WidomMoveCFCMCTail += (time_end - time_begin);
    }
    else
    {
      system.components[selectedComponent].mc_moves_cputime.swapLambdaChangeMoveCFCMCTail += (time_end - time_begin);
      system.mc_moves_cputime.swapLambdaChangeMoveCFCMCTail += (time_end - time_begin);
    }

    RunningEnergy energyDifference = externalFieldEnergyDifference.value() + frameworkEnergyDifference.value() +
                                     interEnergyDifference.value() + EwaldFourierDifference + tailEnergyDifference;

    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.constructed[2] += 1;
    system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalConstructed[2] += 1;

    double biasTerm = lambda.biasFactor[newBin] - lambda.biasFactor[oldBin];

    // Apply acceptance/rejection rule
    if (random.uniform() < std::exp(-system.beta * energyDifference.potentialEnergy() + biasTerm))
    {
      Interactions::acceptEwaldMove(system.forceField, system.storedEik, system.totalEik);
      system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.accepted[2] += 1;
      system.components[selectedComponent].mc_moves_statistics.swapMove_CFCMC.totalAccepted[2] += 1;

      std::copy(trialPositions.begin(), trialPositions.end(), molecule.begin());

      system.components[selectedComponent].lambdaGC.setCurrentBin(newBin);

      return {energyDifference, double3(0.0, 1.0, 0.0)};
    };
    return {std::nullopt, double3(0.0, 1.0, 0.0)};
  }
}
