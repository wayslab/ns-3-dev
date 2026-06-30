/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "genai-message-header.h"

#include "ns3/abort.h"

#include <algorithm>
#include <cctype>

namespace ns3
{

NS_OBJECT_ENSURE_REGISTERED(GenAIMessageHeader);

GenAIModality
ParseGenAIModality(const std::string& value)
{
    std::string normalized = value;
    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (normalized == "text")
    {
        return GenAIModality::TEXT;
    }
    if (normalized == "image")
    {
        return GenAIModality::IMAGE;
    }

    NS_ABORT_MSG("Unsupported GenAI modality '" << value << "'; use 'text' or 'image'");
}

std::string
GenAIModalityToString(GenAIModality modality)
{
    switch (modality)
    {
    case GenAIModality::TEXT:
        return "text";
    case GenAIModality::IMAGE:
        return "image";
    }
    NS_ABORT_MSG("Invalid GenAI modality value");
}

TypeId
GenAIMessageHeader::GetTypeId()
{
    static TypeId tid = TypeId("ns3::GenAIMessageHeader")
                            .SetParent<Header>()
                            .SetGroupName("Applications")
                            .AddConstructor<GenAIMessageHeader>();
    return tid;
}

TypeId
GenAIMessageHeader::GetInstanceTypeId() const
{
    return GetTypeId();
}

GenAIMessageHeader::GenAIMessageHeader()
    : m_magic(MAGIC),
      m_version(VERSION),
      m_messageType(GenAIMessageType::REQUEST),
      m_modality(GenAIModality::TEXT),
      m_payloadSize(0)
{
}

void
GenAIMessageHeader::SetMessageType(GenAIMessageType messageType)
{
    m_messageType = messageType;
}

GenAIMessageType
GenAIMessageHeader::GetMessageType() const
{
    return m_messageType;
}

void
GenAIMessageHeader::SetModality(GenAIModality modality)
{
    m_modality = modality;
}

GenAIModality
GenAIMessageHeader::GetModality() const
{
    return m_modality;
}

void
GenAIMessageHeader::SetPayloadSize(uint32_t payloadSize)
{
    m_payloadSize = payloadSize;
}

uint32_t
GenAIMessageHeader::GetPayloadSize() const
{
    return m_payloadSize;
}

bool
GenAIMessageHeader::IsValid() const
{
    bool validType = m_messageType == GenAIMessageType::REQUEST ||
                     m_messageType == GenAIMessageType::RESPONSE;
    bool validModality = m_modality == GenAIModality::TEXT || m_modality == GenAIModality::IMAGE;
    return m_magic == MAGIC && m_version == VERSION && validType && validModality;
}

uint32_t
GenAIMessageHeader::GetSerializedSize() const
{
    return 12;
}

void
GenAIMessageHeader::Serialize(Buffer::Iterator start) const
{
    start.WriteHtonU32(m_magic);
    start.WriteU8(m_version);
    start.WriteU8(static_cast<uint8_t>(m_messageType));
    start.WriteU8(static_cast<uint8_t>(m_modality));
    start.WriteU8(0);
    start.WriteHtonU32(m_payloadSize);
}

uint32_t
GenAIMessageHeader::Deserialize(Buffer::Iterator start)
{
    m_magic = start.ReadNtohU32();
    m_version = start.ReadU8();
    m_messageType = static_cast<GenAIMessageType>(start.ReadU8());
    m_modality = static_cast<GenAIModality>(start.ReadU8());
    start.ReadU8();
    m_payloadSize = start.ReadNtohU32();
    return GetSerializedSize();
}

void
GenAIMessageHeader::Print(std::ostream& os) const
{
    os << "type="
       << (m_messageType == GenAIMessageType::REQUEST ? "request" : "response")
       << " modality=" << GenAIModalityToString(m_modality)
       << " payload=" << m_payloadSize;
}

} // namespace ns3
