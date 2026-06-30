/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "rpi-helper.h"

#include "ns3/address-utils.h"

namespace ns3
{

RpiHelper::RpiHelper()
    : ApplicationHelper(RpiApplication::GetTypeId())
{
}

RpiHelper::RpiHelper(const Address& address)
    : RpiHelper()
{
    SetAttribute("Remote", AddressValue(address));
}

RpiHelper::RpiHelper(const Address& address, uint16_t port)
    : RpiHelper(addressUtils::ConvertToSocketAddress(address, port))
{
}

} // namespace ns3
