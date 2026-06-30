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

// Models a GenAI inference server: listens on TCP, receives one framed request,
// waits a sampled "thinking" delay, then sends back a sampled-size response.
class GenAIServer : public Application
{
  public:
    static TypeId GetTypeId(); // Registers the class and its configurable attributes.

    GenAIServer();
    ~GenAIServer() override;

  private:
    void StartApplication() override; // Opens the listening socket when the app starts.
    void StopApplication() override;  // Closes sockets and cancels pending work at shutdown.

    bool ConnectionRequested(Ptr<Socket> socket, const Address& from); // Accept/reject an incoming connection.
    void ConnectionAccepted(Ptr<Socket> socket, const Address& from);  // Wires up callbacks once a client connects.
    void HandleRead(Ptr<Socket> socket);                  // Called when request bytes arrive.
    void HandleSend(Ptr<Socket> socket, uint32_t available); // Called when TCP has room to send more.
    void HandlePeerClose(Ptr<Socket> socket);             // Client closed the connection normally.
    void HandlePeerError(Ptr<Socket> socket);             // Connection closed with an error.

    void ProcessReceiveBuffer(); // Extracts a complete request frame from buffered bytes.
    void PrepareResponse();      // Builds the response packet after the processing delay.
    void FlushTransmitBuffer();  // Pushes the pending response into TCP as space allows.
    uint32_t SampleResponseSize(); // Draws a response payload size from the random variable.
    Time SampleProcessingDelay();  // Draws the time-to-first-token delay.

    uint16_t m_port;                            // TCP port to listen on.
    std::string m_modality;                     // Response modality (text/image).
    Ptr<RandomVariableStream> m_processingDelay; // Distribution of processing delay (seconds).
    Ptr<RandomVariableStream> m_responseSize;    // Distribution of response payload size (bytes).

    Ptr<Socket> m_listenSocket;   // Passive socket accepting connections.
    Ptr<Socket> m_clientSocket;   // The active connection to the current client.
    Ptr<Packet> m_pendingTx;      // Response bytes not yet handed to TCP.
    Ptr<Packet> m_rxBuffer;       // Accumulates received request bytes until a full frame.
    EventId m_processingEvent;    // Scheduled event that fires PrepareResponse().
    bool m_requestReceived;       // Guards the one-request-per-connection contract.
};

} // namespace ns3

#endif /* GENAI_SERVER_H */
