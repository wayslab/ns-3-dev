/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef GENAI_SERVER_H
#define GENAI_SERVER_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <string>

namespace ns3
{

class Address;
class Packet;
class RandomVariableStream;
class Socket;

class GenAIServer : public Application
{
  public:
    static TypeId GetTypeId();

    GenAIServer();
    ~GenAIServer() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    bool ConnectionRequested(Ptr<Socket> socket, const Address& from);
    void ConnectionAccepted(Ptr<Socket> socket, const Address& from);
    void HandleRead(Ptr<Socket> socket);
    void HandleSend(Ptr<Socket> socket, uint32_t available);
    void HandlePeerClose(Ptr<Socket> socket);
    void HandlePeerError(Ptr<Socket> socket);

    void ProcessReceiveBuffer();
    void PrepareResponse();
    void FlushTransmitBuffer();
    uint32_t SampleResponseSize();
    Time SampleProcessingDelay();

    uint16_t m_port;
    std::string m_modality;
    Ptr<RandomVariableStream> m_processingDelay;
    Ptr<RandomVariableStream> m_responseSize;

    Ptr<Socket> m_listenSocket;
    Ptr<Socket> m_clientSocket;
    Ptr<Packet> m_pendingTx;
    Ptr<Packet> m_rxBuffer;
    EventId m_processingEvent;
    bool m_requestReceived;
};

} // namespace ns3

#endif /* GENAI_SERVER_H */
