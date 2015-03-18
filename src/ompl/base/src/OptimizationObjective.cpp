/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2008, Willow Garage, Inc.
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Willow Garage nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author: Luis G. Torres, Ioan Sucan */
/* Edited by: Jonathan Gammell (allocInformedStateSampler) */

#include "ompl/base/OptimizationObjective.h"
#include "ompl/geometric/PathGeometric.h"
#include "ompl/tools/config/MagicConstants.h"
#include "ompl/base/goals/GoalRegion.h"
#include "ompl/base/samplers/informed/RejectionInfSampler.h"
#include <limits>
//For boost::make_shared
#include "boost/make_shared.hpp"

ompl::base::OptimizationObjective::OptimizationObjective(const SpaceInformationPtr &si) :
    si_(si),
    threshold_(0.0)
{
}

const std::string& ompl::base::OptimizationObjective::getDescription() const
{
    return description_;
}

bool ompl::base::OptimizationObjective::isSatisfied(Cost c) const
{
    return this->isCostBetterThan(c, threshold_);
}

ompl::base::Cost ompl::base::OptimizationObjective::getCostThreshold() const
{
    return threshold_;
}

void ompl::base::OptimizationObjective::setCostThreshold(Cost c)
{
    threshold_ = c;
}

ompl::base::Cost ompl::base::OptimizationObjective::getCost(const Path &path) const
{
    // Cast path down to a PathGeometric
    const geometric::PathGeometric *pathGeom = dynamic_cast<const geometric::PathGeometric*>(&path);

    // Give up if this isn't a PathGeometric or if the path is empty.
    if (!pathGeom)
    {
        OMPL_ERROR("Error: Cost computation is only implemented for paths of type PathGeometric.");
        return this->identityCost();
    }
    else
    {
        std::size_t numStates = pathGeom->getStateCount();
        if (numStates == 0)
        {
            OMPL_ERROR("Cannot compute cost of an empty path.");
            return this->identityCost();
        }
        else
        {
            // Compute path cost by accumulating the cost along the path
            Cost cost(this->identityCost());
            for (std::size_t i = 1; i < numStates; ++i)
            {
                const State *s1 = pathGeom->getState(i-1);
                const State *s2 = pathGeom->getState(i);
                cost = this->combineCosts(cost, this->motionCost(s1, s2));
            }

            return cost;
        }
    }
}

bool ompl::base::OptimizationObjective::isCostBetterThan(Cost c1, Cost c2) const
{
    return (c1.value() + magic::BETTER_PATH_COST_MARGIN) < c2.value();
}

bool ompl::base::OptimizationObjective::isCostWorseThan(Cost c1, Cost c2) const
{
    //If c2 is better than c1, then c1 is worse than c2
    return isCostBetterThan(c2, c1);
}

bool ompl::base::OptimizationObjective::isCostEquivalentTo(Cost c1, Cost c2) const
{
    //If c1 is not better than c2, and c2 is not better than c1, then they are equal
    return !isCostBetterThan(c1,c2) && !isCostBetterThan(c2,c1);
}

bool ompl::base::OptimizationObjective::isCostNotEquivalentTo(Cost c1, Cost c2) const
{
    //If c1 is better than c2, or c2 is better than c1, then they are not equal
    return isCostBetterThan(c1,c2) || isCostBetterThan(c2,c1);
}

bool ompl::base::OptimizationObjective::isCostBetterThanOrEquivalentTo(Cost c1, Cost c2) const
{
    //If c2 is not better than c1, then c1 is better than, or equal to, c2
    return !isCostBetterThan(c2,c1);
}

bool ompl::base::OptimizationObjective::isCostWorseThanOrEquivalentTo(Cost c1, Cost c2) const
{
    //If c1 is not better than c2, than c1 is worse than, or equal to, c2
    return !isCostBetterThan(c1,c2);
}

bool ompl::base::OptimizationObjective::isFinite(Cost cost) const
{
    return std::isfinite(cost.value());
}

ompl::base::Cost ompl::base::OptimizationObjective::minCost(Cost c1, Cost c2) const
{
    if (isCostBetterThan(c1, c2))
    {
        return c1;
    }
    else
    {
        return c2;
    }
}

ompl::base::Cost ompl::base::OptimizationObjective::combineCosts(Cost c1, Cost c2) const
{
    return Cost(c1.value() + c2.value());
}

ompl::base::Cost ompl::base::OptimizationObjective::combineCosts(Cost c1, Cost c2, Cost c3) const
{
    return combineCosts( combineCosts(c1, c2), c3 );
}

ompl::base::Cost ompl::base::OptimizationObjective::combineCosts(Cost c1, Cost c2, Cost c3, Cost c4) const
{
    return combineCosts( combineCosts(c1, c2, c3), c4 );
}

ompl::base::Cost ompl::base::OptimizationObjective::identityCost() const
{
    return Cost(0.0);
}

ompl::base::Cost ompl::base::OptimizationObjective::infiniteCost() const
{
    return Cost(std::numeric_limits<double>::infinity());
}

ompl::base::Cost ompl::base::OptimizationObjective::initialCost(const State *s) const
{
    return identityCost();
}

ompl::base::Cost ompl::base::OptimizationObjective::terminalCost(const State *s) const
{
    return identityCost();
}

bool ompl::base::OptimizationObjective::isSymmetric() const
{
    return si_->getStateSpace()->hasSymmetricInterpolate();
}

ompl::base::Cost ompl::base::OptimizationObjective::averageStateCost(unsigned int numStates) const
{
    StateSamplerPtr ss = si_->allocStateSampler();
    State *state = si_->allocState();
    Cost totalCost(this->identityCost());

    for (unsigned int i = 0 ; i < numStates ; ++i)
    {
        ss->sampleUniform(state);
        totalCost = this->combineCosts(totalCost, this->stateCost(state));
    }

    si_->freeState(state);

    return Cost(totalCost.value() / (double)numStates);
}

void ompl::base::OptimizationObjective::setCostToGoHeuristic(const CostToGoHeuristic& costToGo)
{
    costToGoFn_ = costToGo;
}

bool ompl::base::OptimizationObjective::hasCostToGoHeuristic() const
{
    return static_cast<bool>(costToGoFn_);
}

ompl::base::Cost ompl::base::OptimizationObjective::costToGo(const State *state, const Goal *goal) const
{
    if (hasCostToGoHeuristic())
        return costToGoFn_(state, goal);
    else
        return this->identityCost(); // assumes that identity < all costs
}

ompl::base::Cost ompl::base::OptimizationObjective::motionCostHeuristic(const State *s1, const State *s2) const
{
    return this->identityCost(); // assumes that identity < all costs
}

const ompl::base::SpaceInformationPtr& ompl::base::OptimizationObjective::getSpaceInformation() const
{
    return si_;
}

ompl::base::InformedStateSamplerPtr ompl::base::OptimizationObjective::allocInformedStateSampler(const StateSpace* space, const ProblemDefinitionPtr probDefn, const Cost* bestCost) const
{
    OMPL_WARN("%s: No direct informed sampling scheme is defined, defaulting to rejection sampling.", description_.c_str());
    return boost::make_shared<RejectionInfSampler>(space, probDefn, bestCost);
}

ompl::base::Cost ompl::base::goalRegionCostToGo(const State *state, const Goal *goal)
{
    const GoalRegion *goalRegion = goal->as<GoalRegion>();

    // Ensures that all states within the goal region's threshold to
    // have a cost-to-go of exactly zero.
    return Cost(std::max(goalRegion->distanceGoal(state) - goalRegion->getThreshold(),
                         0.0));
}

ompl::base::MultiOptimizationObjective::MultiOptimizationObjective(const SpaceInformationPtr &si) :
    OptimizationObjective(si),
    locked_(false)
{
}

ompl::base::MultiOptimizationObjective::Component::
Component(const OptimizationObjectivePtr& obj, double weight) :
    objective(obj), weight(weight)
{
}

void ompl::base::MultiOptimizationObjective::addObjective(const OptimizationObjectivePtr& objective,
                                                          double weight)
{
    if (locked_)
    {
        throw Exception("This optimization objective is locked. No further objectives can be added.");
    }
    else
        components_.push_back(Component(objective, weight));
}

std::size_t ompl::base::MultiOptimizationObjective::getObjectiveCount() const
{
    return components_.size();
}

const ompl::base::OptimizationObjectivePtr& ompl::base::MultiOptimizationObjective::getObjective(unsigned int idx) const
{
    if (components_.size() > idx)
        return components_[idx].objective;
    else
        throw Exception("Objective index does not exist.");
}

double ompl::base::MultiOptimizationObjective::getObjectiveWeight(unsigned int idx) const
{
    if (components_.size() > idx)
        return components_[idx].weight;
    else
        throw Exception("Objective index does not exist.");
}

void ompl::base::MultiOptimizationObjective::setObjectiveWeight(unsigned int idx,
                                                                double weight)
{
    if (components_.size() > idx)
        components_[idx].weight = weight;
    else
        throw Exception("Objecitve index does not exist.");
}

void ompl::base::MultiOptimizationObjective::lock()
{
    locked_ = true;
}

bool ompl::base::MultiOptimizationObjective::isLocked() const
{
    return locked_;
}

ompl::base::Cost ompl::base::MultiOptimizationObjective::stateCost(const State *s) const
{
    Cost c = this->identityCost();
    for (std::vector<Component>::const_iterator comp = components_.begin();
         comp != components_.end();
         ++comp)
    {
        c = Cost(c.value() + comp->weight * (comp->objective->stateCost(s).value()));
    }

    return c;
}

ompl::base::Cost ompl::base::MultiOptimizationObjective::motionCost(const State *s1,
                                                                    const State *s2) const
{
    Cost c = this->identityCost();
     for (std::vector<Component>::const_iterator comp = components_.begin();
         comp != components_.end();
         ++comp)
     {
         c = Cost(c.value() + comp->weight * (comp->objective->motionCost(s1, s2).value()));
     }

     return c;
}

ompl::base::OptimizationObjectivePtr ompl::base::operator+(const OptimizationObjectivePtr &a,
                                                           const OptimizationObjectivePtr &b)
{
    std::vector<MultiOptimizationObjective::Component> components;

    if (a)
    {
        if (MultiOptimizationObjective *mult = dynamic_cast<MultiOptimizationObjective*>(a.get()))
        {
            for (std::size_t i = 0; i < mult->getObjectiveCount(); ++i)
            {
                components.push_back(MultiOptimizationObjective::
                                     Component(mult->getObjective(i),
                                               mult->getObjectiveWeight(i)));
            }
        }
        else
            components.push_back(MultiOptimizationObjective::Component(a, 1.0));
    }

    if (b)
    {
        if (MultiOptimizationObjective *mult = dynamic_cast<MultiOptimizationObjective*>(b.get()))
        {
            for (std::size_t i = 0; i < mult->getObjectiveCount(); ++i)
            {
                components.push_back(MultiOptimizationObjective::Component(mult->getObjective(i),
                                                                           mult->getObjectiveWeight(i)));
            }
        }
        else
            components.push_back(MultiOptimizationObjective::Component(b, 1.0));
    }

    MultiOptimizationObjective *multObj = new MultiOptimizationObjective(a->getSpaceInformation());

    for (std::vector<MultiOptimizationObjective::Component>::const_iterator comp = components.begin();
         comp != components.end();
         ++comp)
    {
        multObj->addObjective(comp->objective, comp->weight);
    }

    return OptimizationObjectivePtr(multObj);
}

ompl::base::OptimizationObjectivePtr ompl::base::operator*(double weight,
                                                           const OptimizationObjectivePtr &a)
{
    std::vector<MultiOptimizationObjective::Component> components;

    if (a)
    {
        if (MultiOptimizationObjective *mult = dynamic_cast<MultiOptimizationObjective*>(a.get()))
        {
            for (std::size_t i = 0; i < mult->getObjectiveCount(); ++i)
            {
                components.push_back(MultiOptimizationObjective
                                     ::Component(mult->getObjective(i),
                                                 weight * mult->getObjectiveWeight(i)));
            }
        }
        else
            components.push_back(MultiOptimizationObjective::Component(a, weight));
    }

    MultiOptimizationObjective *multObj = new MultiOptimizationObjective(a->getSpaceInformation());

    for (std::vector<MultiOptimizationObjective::Component>::const_iterator comp = components.begin();
         comp != components.end();
         ++comp)
    {
        multObj->addObjective(comp->objective, comp->weight);
    }

    return OptimizationObjectivePtr(multObj);
}

ompl::base::OptimizationObjectivePtr ompl::base::operator*(const OptimizationObjectivePtr &a,
                                                           double weight)
{
    return weight * a;
}
