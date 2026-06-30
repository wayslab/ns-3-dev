/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "genai-server.h"

#include "genai-message-header.h"

#include "ns3/abort.h"
#include "ns3/inet-socket-address.h"
#include "ns3/ipv4-address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("GenAIServer");
NS_OBJECT_ENSURE_REGISTERED(GenAIServer);

// Declares the ns-3 TypeId and the four attributes the helper/config can set
// (Port, Modality, ProcessingDelay, ResponseSize). Each attribute is bound to a
// member variable via an accessor, with a default used when none is supplied.
TypeId
GenAIServer::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::GenAIServer")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<GenAIServer>()
            .AddAttribute("Port",
                          "TCP port on which the server listens.",
                          UintegerValue(5000),
                          MakeUintegerAccessor(&GenAIServer::m_port),
                          MakeUintegerChecker<uint16_t>())
            .AddAttribute("Modality",
                          "The server response modality: text or image.",
                          StringValue("text"),
                          MakeStringAccessor(&GenAIServer::m_modality),
                          MakeStringChecker())
            .AddAttribute("ProcessingDelay",
                          "Random variable that returns time to first response token in seconds.",
                          StringValue("ns3::ConstantRandomVariable[Constant=0.25]"),
                          MakePointerAccessor(&GenAIServer::m_processingDelay),
                          MakePointerChecker<RandomVariableStream>())
            .AddAttribute("ResponseSize",
                          "Random variable that returns application response payload bytes.",
                          StringValue("ns3::ConstantRandomVariable[Constant=5242880]"),
                          MakePointerAccessor(&GenAIServer::m_responseSize),
                          MakePointerChecker<RandomVariableStream>());
    return tid;
}

// Initializes pointers to null and creates an empty receive buffer; real setup
// happens in StartApplication so the object can be re-run cleanly.
GenAIServer::GenAIServer()
    : m_listenSocket(nullptr),
      m_clientSocket(nullptr),
      m_pendingTx(nullptr),
      m_rxBuffer(Create<Packet>()),
      m_requestReceived(false)
{
}

GenAIServer::~GenAIServer() = default;

// Starts the server: validates the modality, resets per-run state, then creates a
// TCP socket, binds it to the port, listens, and registers the accept callbacks so
// new connections are routed to ConnectionRequested/ConnectionAccepted.
void
GenAIServer::StartApplication()
{
    ParseGenAIModality(m_modality);

    m_rxBuffer = Create<Packet>();
    m_pendingTx = nullptr;
    m_requestReceived = false;

    m_listenSocket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    int bindResult = m_listenSocket->Bind(InetSocketAddress(Ipv4Address::GetAny(), m_port));
    NS_ABORT_MSG_IF(bindResult == -1, "GenAIServer failed to bind TCP port " << m_port);
    NS_ABORT_MSG_IF(m_listenSocket->Listen() == -1, "GenAIServer failed to listen");

    m_listenSocket->SetAcceptCallback(MakeCallback(&GenAIServer::ConnectionRequested, this),
                                      MakeCallback(&GenAIServer::ConnectionAccepted, this));
    NS_LOG_INFO("listening on TCP port " << m_port << " with " << m_modality
                                         << " response modality");
}

// Shuts the server down: cancels any pending processing event, detaches callbacks
// and closes both the client and listening sockets, and clears the buffers so a
// later StartApplication begins from a clean slate.
void
GenAIServer::StopApplication()
{
    Simulator::Cancel(m_processingEvent);

    if (m_clientSocket)
    {
        m_clientSocket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        m_clientSocket->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t>());
        m_clientSocket->Close();
        m_clientSocket = nullptr;
    }

    if (m_listenSocket)
    {
        m_listenSocket->Close();
        m_listenSocket = nullptr;
    }

    m_pendingTx = nullptr;
    m_rxBuffer = Create<Packet>();
}

// Accepts a connection only if no client is already connected (one client at a time);
// returning false rejects the SYN.
bool
GenAIServer::ConnectionRequested(Ptr<Socket> socket, const Address& from)
{
    return m_clientSocket == nullptr;
}

// Stores the accepted socket and attaches the read/send/close callbacks, then calls
// HandleRead once in case request bytes already arrived with the handshake.
void
GenAIServer::ConnectionAccepted(Ptr<Socket> socket, const Address& from)
{
    m_clientSocket = socket;
    m_clientSocket->SetRecvCallback(MakeCallback(&GenAIServer::HandleRead, this));
    m_clientSocket->SetSendCallback(MakeCallback(&GenAIServer::HandleSend, this));
    m_clientSocket->SetCloseCallbacks(MakeCallback(&GenAIServer::HandlePeerClose, this),
                                      MakeCallback(&GenAIServer::HandlePeerError, this));
    NS_LOG_INFO("accepted TCP connection at " << Simulator::Now().As(Time::MS));

    HandleRead(socket);
}

// Drains all currently available bytes from the socket, appending them to the
// reassembly buffer, then tries to parse a complete frame out of it.
void
GenAIServer::HandleRead(Ptr<Socket> socket)
{
    while (Ptr<Packet> packet = socket->Recv())
    {
        if (packet->GetSize() == 0)
        {
            break;
        }
        m_rxBuffer->AddAtEnd(packet);
    }
    ProcessReceiveBuffer();
}

// Frame parser: peeks the fixed-size header to learn the payload length, returns if
// the full frame has not arrived yet, otherwise strips header+payload, verifies it is
// a single REQUEST, and schedules PrepareResponse after the sampled processing delay.
void
GenAIServer::ProcessReceiveBuffer()
{
    GenAIMessageHeader header;

    while (m_rxBuffer->GetSize() >= header.GetSerializedSize())
    {
        m_rxBuffer->PeekHeader(header);
        NS_ABORT_MSG_IF(!header.IsValid(), "GenAIServer received an invalid application header");

        uint64_t frameSize =
            static_cast<uint64_t>(header.GetSerializedSize()) + header.GetPayloadSize();
        if (m_rxBuffer->GetSize() < frameSize)
        {
            return;
        }

        m_rxBuffer->RemoveHeader(header);
        m_rxBuffer->RemoveAtStart(header.GetPayloadSize());

        NS_ABORT_MSG_IF(header.GetMessageType() != GenAIMessageType::REQUEST,
                        "GenAIServer received a non-request message");
        NS_ABORT_MSG_IF(m_requestReceived,
                        "This GenAIServer version supports one request per connection");

        m_requestReceived = true;
        Time processingDelay = SampleProcessingDelay();

        NS_LOG_INFO("received complete " << GenAIModalityToString(header.GetModality())
                                         << " request of " << header.GetPayloadSize()
                                         << " bytes at " << Simulator::Now().As(Time::MS)
                                         << "; sampled " << GenAIModalityToString(header.GetModality())
                                         << "-to-" << m_modality << " TTFT "
                                         << processingDelay.As(Time::S));

        m_processingEvent =
            Simulator::Schedule(processingDelay, &GenAIServer::PrepareResponse, this);
    }
}

// Draws one processing-delay sample, aborts if it is non-finite or negative, and
// returns it as an ns-3 Time.
Time
GenAIServer::SampleProcessingDelay()
{
    double sampleSeconds = m_processingDelay->GetValue();
    NS_ABORT_MSG_IF(!std::isfinite(sampleSeconds) || sampleSeconds < 0.0,
                    "ProcessingDelay sampled an invalid value: " << sampleSeconds);
    return Seconds(sampleSeconds);
}

// Draws one response-size sample, validates it is finite and within [1, uint32 max),
// and rounds it to a whole number of bytes.
uint32_t
GenAIServer::SampleResponseSize()
{
    double sample = m_responseSize->GetValue();
    NS_ABORT_MSG_IF(!std::isfinite(sample) || sample < 1.0 ||
                        sample > static_cast<double>(std::numeric_limits<uint32_t>::max() - 12),
                    "ResponseSize sampled an invalid payload size: " << sample);
    return static_cast<uint32_t>(std::llround(sample));
}

// Fires after the processing delay: samples a response size, builds a RESPONSE-framed
// packet of that many payload bytes, stores it as the pending transmit buffer, and
// kicks off the first flush into TCP.
void
GenAIServer::PrepareResponse()
{
    NS_ABORT_MSG_IF(!m_clientSocket, "Client disconnected while GenAIServer was processing");

    uint32_t payloadSize = SampleResponseSize();

    GenAIMessageHeader header;
    header.SetMessageType(GenAIMessageType::RESPONSE);
    header.SetModality(ParseGenAIModality(m_modality));
    header.SetPayloadSize(payloadSize);

    m_pendingTx = Create<Packet>(payloadSize);
    m_pendingTx->AddHeader(header);

    NS_LOG_INFO("sampled " << payloadSize << "-byte " << m_modality
                           << " response at " << Simulator::Now().As(Time::MS));
    FlushTransmitBuffer();
}

// TCP send-buffer space freed up: resume flushing the rest of the response.
void
GenAIServer::HandleSend(Ptr<Socket> socket, uint32_t available)
{
    if (socket == m_clientSocket && available > 0)
    {
        FlushTransmitBuffer();
    }
}

// Sends as much of the pending response as TCP will currently accept: loops while
// there is send-buffer room, fragments off that many bytes, hands them to Send, and
// removes the sent bytes. Returns when TCP is full (HandleSend resumes later) and
// clears the buffer once the whole response has been queued.
void
GenAIServer::FlushTransmitBuffer()
{
    while (m_clientSocket && m_pendingTx && m_pendingTx->GetSize() > 0)
    {
        uint32_t available = m_clientSocket->GetTxAvailable();
        if (available == 0)
        {
            return;
        }

        uint32_t toSend = std::min(available, m_pendingTx->GetSize());
        Ptr<Packet> fragment = m_pendingTx->CreateFragment(0, toSend);
        int sent = m_clientSocket->Send(fragment);

        if (sent <= 0)
        {
            return;
        }

        m_pendingTx->RemoveAtStart(static_cast<uint32_t>(sent));
    }

    if (m_pendingTx && m_pendingTx->GetSize() == 0)
    {
        m_pendingTx = nullptr;
        NS_LOG_INFO("complete response queued to TCP at " << Simulator::Now().As(Time::MS));
    }
}

// Client closed normally: close our end and forget the client so a new one may connect.
void
GenAIServer::HandlePeerClose(Ptr<Socket> socket)
{
    if (socket == m_clientSocket)
    {
        socket->Close();
        m_clientSocket = nullptr;
    }
}

// Connection failed: log the error and drop the client reference.
void
GenAIServer::HandlePeerError(Ptr<Socket> socket)
{
    NS_LOG_ERROR("GenAIServer TCP connection closed with an error");
    if (socket == m_clientSocket)
    {
        m_clientSocket = nullptr;
    }
}

} // namespace ns3
