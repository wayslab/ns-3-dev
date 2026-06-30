/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef GENAI_MESSAGE_HEADER_H
#define GENAI_MESSAGE_HEADER_H

#include "ns3/header.h"

#include <cstdint>
#include <string>

namespace ns3
{

enum class GenAIMessageType : uint8_t
{
    REQUEST = 1,
    RESPONSE = 2
};

enum class GenAIModality : uint8_t
{
    TEXT = 1,
    IMAGE = 2
};

GenAIModality ParseGenAIModality(const std::string& value);
std::string GenAIModalityToString(GenAIModality modality);

class GenAIMessageHeader : public Header
{
  public:
    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;

    GenAIMessageHeader();

    void SetMessageType(GenAIMessageType messageType);
    GenAIMessageType GetMessageType() const;

    void SetModality(GenAIModality modality);
    GenAIModality GetModality() const;

    void SetPayloadSize(uint32_t payloadSize);
    uint32_t GetPayloadSize() const;

    bool IsValid() const;

    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

  private:
    static constexpr uint32_t MAGIC = 0x47414931; // "GAI1"
    static constexpr uint8_t VERSION = 1;

    uint32_t m_magic;
    uint8_t m_version;
    GenAIMessageType m_messageType;
    GenAIModality m_modality;
    uint32_t m_payloadSize;
};

} // namespace ns3

#endif /* GENAI_MESSAGE_HEADER_H */
