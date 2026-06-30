/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef LOGNORMAL_MIXTURE_RANDOM_VARIABLE_H
#define LOGNORMAL_MIXTURE_RANDOM_VARIABLE_H

#include "ns3/random-variable-stream.h"

#include <cstddef>
#include <vector>

namespace ns3
{

class LogNormalMixtureRandomVariable : public RandomVariableStream
{
  public:
    static TypeId GetTypeId();

    LogNormalMixtureRandomVariable();
    ~LogNormalMixtureRandomVariable() override;

    void AddComponent(double weight, double mu, double sigma);
    void ClearComponents();
    std::size_t GetComponentCount() const;

    double GetValue() override;

  private:
    struct Component
    {
        double weight;
        double mu;
        double sigma;
    };

    double GetUniformValue();

    std::vector<Component> m_components;
    double m_totalWeight;
};

} // namespace ns3

#endif /* LOGNORMAL_MIXTURE_RANDOM_VARIABLE_H */
