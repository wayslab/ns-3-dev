/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef GENAI_HELPER_H
#define GENAI_HELPER_H

#include "ns3/address.h"
#include "ns3/application-helper.h"

namespace ns3
{

class GenAIUserHelper : public ApplicationHelper
{
  public:
    GenAIUserHelper(const Address& serverAddress, uint16_t port);
};

class GenAIServerHelper : public ApplicationHelper
{
  public:
    explicit GenAIServerHelper(uint16_t port);
};

} // namespace ns3

#endif /* GENAI_HELPER_H */
