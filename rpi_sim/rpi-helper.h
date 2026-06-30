/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef RPI_HELPER_H
#define RPI_HELPER_H

#include "rpi-application.h"

#include "ns3/address.h"
#include "ns3/application-helper.h"

namespace ns3
{

class RpiHelper : public ApplicationHelper
{
  public:
    RpiHelper();
    RpiHelper(const Address& address);
    RpiHelper(const Address& address, uint16_t port);
};

} // namespace ns3

#endif /* RPI_HELPER_H */
