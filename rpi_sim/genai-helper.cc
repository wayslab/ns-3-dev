/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "genai-helper.h"

#include "genai-server.h"
#include "genai-user.h"

#include "ns3/address-utils.h"
#include "ns3/uinteger.h"

namespace ns3
{

GenAIUserHelper::GenAIUserHelper(const Address& serverAddress, uint16_t port)
    : ApplicationHelper(GenAIUser::GetTypeId())
{
    SetAttribute("Remote", AddressValue(addressUtils::ConvertToSocketAddress(serverAddress, port)));
}

GenAIServerHelper::GenAIServerHelper(uint16_t port)
    : ApplicationHelper(GenAIServer::GetTypeId())
{
    SetAttribute("Port", UintegerValue(port));
}

} // namespace ns3
