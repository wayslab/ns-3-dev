/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef GENAI_USER_H
#define GENAI_USER_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>

namespace ns3
{

class Packet;
class RandomVariableStream;
class Socket;

class GenAIUser : public Application
{
  public:
    static TypeId GetTypeId();

    GenAIUser();
    ~GenAIUser() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    void ConnectionSucceeded(Ptr<Socket> socket);
    void ConnectionFailed(Ptr<Socket> socket);
    void HandleSend(Ptr<Socket> socket, uint32_t available);
    void HandleRead(Ptr<Socket> socket);
    void HandlePeerClose(Ptr<Socket> socket);
    void HandlePeerError(Ptr<Socket> socket);

    void QueueRequest();
    void FlushTransmitBuffer();
    void ProcessReceiveBuffer();
    uint32_t SampleRequestSize();

    Address m_remote;
    std::string m_modality;
    Ptr<RandomVariableStream> m_requestSize;

    Ptr<Socket> m_socket;
    Ptr<Packet> m_pendingTx;
    Ptr<Packet> m_rxBuffer;
    Time m_requestStart;
    uint32_t m_requestPayloadSize;
    bool m_connected;
    bool m_responseReceived;
};

} // namespace ns3

#endif /* GENAI_USER_H */
