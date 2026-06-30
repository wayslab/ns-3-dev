/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "lognormal-mixture-random-variable.h"

#include "ns3/abort.h"
#include "ns3/log.h"
#include "ns3/rng-stream.h"

#include <cmath>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LogNormalMixtureRandomVariable");
NS_OBJECT_ENSURE_REGISTERED(LogNormalMixtureRandomVariable);

TypeId
LogNormalMixtureRandomVariable::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::LogNormalMixtureRandomVariable")
            .SetParent<RandomVariableStream>()
            .SetGroupName("Core")
            .AddConstructor<LogNormalMixtureRandomVariable>();
    return tid;
}

LogNormalMixtureRandomVariable::LogNormalMixtureRandomVariable()
    : m_totalWeight(0.0)
{
}

LogNormalMixtureRandomVariable::~LogNormalMixtureRandomVariable() = default;

void
LogNormalMixtureRandomVariable::AddComponent(double weight, double mu, double sigma)
{
    NS_ABORT_MSG_IF(!std::isfinite(weight) || weight <= 0.0,
                    "Log-normal mixture weight must be finite and positive");
    NS_ABORT_MSG_IF(!std::isfinite(mu), "Log-normal mixture mu must be finite");
    NS_ABORT_MSG_IF(!std::isfinite(sigma) || sigma < 0.0,
                    "Log-normal mixture sigma must be finite and nonnegative");
    NS_ABORT_MSG_IF(!std::isfinite(m_totalWeight + weight),
                    "Log-normal mixture total weight overflowed");

    m_components.push_back({weight, mu, sigma});
    m_totalWeight += weight;
}

void
LogNormalMixtureRandomVariable::ClearComponents()
{
    m_components.clear();
    m_totalWeight = 0.0;
}

std::size_t
LogNormalMixtureRandomVariable::GetComponentCount() const
{
    return m_components.size();
}

double
LogNormalMixtureRandomVariable::GetUniformValue()
{
    double value = Peek()->RandU01();
    return IsAntithetic() ? 1.0 - value : value;
}

double
LogNormalMixtureRandomVariable::GetValue()
{
    NS_ABORT_MSG_IF(m_components.empty(), "Log-normal mixture has no components");

    double target = GetUniformValue() * m_totalWeight;
    const Component* selected = &m_components.back();
    double cumulativeWeight = 0.0;

    for (const auto& component : m_components)
    {
        cumulativeWeight += component.weight;
        if (target < cumulativeWeight)
        {
            selected = &component;
            break;
        }
    }

    double first;
    double second;
    double radiusSquared;
    do
    {
        first = -1.0 + 2.0 * GetUniformValue();
        second = -1.0 + 2.0 * GetUniformValue();
        radiusSquared = first * first + second * second;
    } while (radiusSquared >= 1.0 || radiusSquared == 0.0);

    double standardNormal =
        first * std::sqrt(-2.0 * std::log(radiusSquared) / radiusSquared);
    double value = std::exp(selected->mu + selected->sigma * standardNormal);

    NS_LOG_DEBUG("sampled " << value << " from component mu=" << selected->mu
                            << " sigma=" << selected->sigma);
    return value;
}

} // namespace ns3
