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

// Models a GenAI client: connects to the server over TCP, sends one framed request of
// a sampled size, then waits for and measures the latency of the full response.
class GenAIUser : public Application
{
  public:
    static TypeId GetTypeId(); // Registers the class and its configurable attributes.

    GenAIUser();
    ~GenAIUser() override;

  private:
    void StartApplication() override; // Connects to the server when the app starts.
    void StopApplication() override;  // Closes the socket and clears state at shutdown.

    void ConnectionSucceeded(Ptr<Socket> socket); // TCP connect completed -> send request.
    void ConnectionFailed(Ptr<Socket> socket);    // TCP connect failed.
    void HandleSend(Ptr<Socket> socket, uint32_t available); // TCP has room -> keep sending.
    void HandleRead(Ptr<Socket> socket);          // Response bytes arrived.
    void HandlePeerClose(Ptr<Socket> socket);     // Server closed the connection.
    void HandlePeerError(Ptr<Socket> socket);     // Connection closed with an error.

    void QueueRequest();         // Samples a size and builds the request packet.
    void FlushTransmitBuffer();  // Pushes the pending request into TCP as space allows.
    void ProcessReceiveBuffer(); // Extracts a complete response frame from buffered bytes.
    uint32_t SampleRequestSize();// Draws a request payload size from the random variable.

    Address m_remote;                          // Server TCP address to connect to.
    std::string m_modality;                    // Request modality (text/image).
    Ptr<RandomVariableStream> m_requestSize;   // Distribution of request payload size (bytes).

    Ptr<Socket> m_socket;          // The TCP connection to the server.
    Ptr<Packet> m_pendingTx;       // Request bytes not yet handed to TCP.
    Ptr<Packet> m_rxBuffer;        // Accumulates received response bytes until a full frame.
    Time m_requestStart;           // Send time of the request, used to compute latency.
    uint32_t m_requestPayloadSize; // Sampled request payload size.
    bool m_connected;              // Whether the TCP connection is currently up.
    bool m_responseReceived;       // Whether a complete response has arrived.
};

} // namespace ns3

#endif /* GENAI_USER_H */
