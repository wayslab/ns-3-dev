/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "genai-user.h"

#include "genai-message-header.h"

#include "ns3/abort.h"
#include "ns3/address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/pointer.h"
#include "ns3/random-variable-stream.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/string.h"
#include "ns3/tcp-socket-factory.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("GenAIUser");
NS_OBJECT_ENSURE_REGISTERED(GenAIUser);

// Declares the ns-3 TypeId and the three attributes the helper/config can set
// (Remote, Modality, RequestSize), each bound to a member via an accessor with a
// default used when none is supplied.
TypeId
GenAIUser::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::GenAIUser")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<GenAIUser>()
            .AddAttribute("Remote",
                          "The GenAIServer TCP address.",
                          AddressValue(),
                          MakeAddressAccessor(&GenAIUser::m_remote),
                          MakeAddressChecker())
            .AddAttribute("Modality",
                          "The user request modality: text or image.",
                          StringValue("text"),
                          MakeStringAccessor(&GenAIUser::m_modality),
                          MakeStringChecker())
            .AddAttribute("RequestSize",
                          "Random variable that returns application request payload bytes.",
                          StringValue("ns3::ConstantRandomVariable[Constant=1048576]"),
                          MakePointerAccessor(&GenAIUser::m_requestSize),
                          MakePointerChecker<RandomVariableStream>());
    return tid;
}

// Initializes pointers to null and creates an empty receive buffer; real setup
// happens in StartApplication so the object can be re-run cleanly.
GenAIUser::GenAIUser()
    : m_socket(nullptr),
      m_pendingTx(nullptr),
      m_rxBuffer(Create<Packet>()),
      m_requestPayloadSize(0),
      m_connected(false),
      m_responseReceived(false)
{
}

GenAIUser::~GenAIUser() = default;

// Starts the client: validates config, resets per-run state, creates and binds a TCP
// socket, registers all the connect/send/recv/close callbacks, and initiates the
// connection to the server (the request is sent later, from ConnectionSucceeded).
void
GenAIUser::StartApplication()
{
    NS_ABORT_MSG_IF(m_remote.IsInvalid(), "GenAIUser Remote address is not configured");
    ParseGenAIModality(m_modality);

    m_rxBuffer = Create<Packet>();
    m_pendingTx = nullptr;
    m_connected = false;
    m_responseReceived = false;

    m_socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_socket->Bind();
    m_socket->SetConnectCallback(MakeCallback(&GenAIUser::ConnectionSucceeded, this),
                                 MakeCallback(&GenAIUser::ConnectionFailed, this));
    m_socket->SetSendCallback(MakeCallback(&GenAIUser::HandleSend, this));
    m_socket->SetRecvCallback(MakeCallback(&GenAIUser::HandleRead, this));
    m_socket->SetCloseCallbacks(MakeCallback(&GenAIUser::HandlePeerClose, this),
                                MakeCallback(&GenAIUser::HandlePeerError, this));

    int result = m_socket->Connect(m_remote);
    NS_ABORT_MSG_IF(result == -1, "GenAIUser could not start its TCP connection");
}

// Shuts the client down: detaches callbacks, closes the socket, and clears buffers
// and flags so a later StartApplication begins from a clean slate.
void
GenAIUser::StopApplication()
{
    if (m_socket)
    {
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
        m_socket->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t>());
        m_socket->Close();
        m_socket = nullptr;
    }

    m_pendingTx = nullptr;
    m_rxBuffer = Create<Packet>();
    m_connected = false;
}

// Connection established: mark connected and queue the request for sending.
void
GenAIUser::ConnectionSucceeded(Ptr<Socket> socket)
{
    NS_ABORT_MSG_IF(socket != m_socket, "GenAIUser received a callback for an unknown socket");
    m_connected = true;
    NS_LOG_INFO("TCP connection established at " << Simulator::Now().As(Time::MS));
    QueueRequest();
}

// Connect failed: log it; nothing more to send.
void
GenAIUser::ConnectionFailed(Ptr<Socket> socket)
{
    NS_LOG_ERROR("TCP connection to GenAIServer failed at " << Simulator::Now().As(Time::MS));
}

// Draws one request-size sample, validates it is finite and within [1, uint32 max),
// and rounds it to a whole number of bytes.
uint32_t
GenAIUser::SampleRequestSize()
{
    double sample = m_requestSize->GetValue();
    NS_ABORT_MSG_IF(!std::isfinite(sample) || sample < 1.0 ||
                        sample > static_cast<double>(std::numeric_limits<uint32_t>::max() - 12),
                    "RequestSize sampled an invalid payload size: " << sample);
    return static_cast<uint32_t>(std::llround(sample));
}

// Builds the request: samples a payload size, prepends a REQUEST frame header, stamps
// the start time (for latency measurement), and starts flushing it into TCP.
void
GenAIUser::QueueRequest()
{
    m_requestPayloadSize = SampleRequestSize();

    GenAIMessageHeader header;
    header.SetMessageType(GenAIMessageType::REQUEST);
    header.SetModality(ParseGenAIModality(m_modality));
    header.SetPayloadSize(m_requestPayloadSize);

    m_pendingTx = Create<Packet>(m_requestPayloadSize);
    m_pendingTx->AddHeader(header);
    m_requestStart = Simulator::Now();

    NS_LOG_INFO("sampled " << m_requestPayloadSize << "-byte " << m_modality
                           << " request at " << Simulator::Now().As(Time::MS));
    FlushTransmitBuffer();
}

// TCP send-buffer space freed up: resume flushing the rest of the request.
void
GenAIUser::HandleSend(Ptr<Socket> socket, uint32_t available)
{
    if (socket == m_socket && available > 0)
    {
        FlushTransmitBuffer();
    }
}

// Sends as much of the pending request as TCP will currently accept: loops while there
// is send-buffer room, fragments off that many bytes, hands them to Send, and removes
// the sent bytes. Returns when TCP is full (HandleSend resumes later) and clears the
// buffer once the whole request has been queued.
void
GenAIUser::FlushTransmitBuffer()
{
    while (m_connected && m_pendingTx && m_pendingTx->GetSize() > 0)
    {
        uint32_t available = m_socket->GetTxAvailable();
        if (available == 0)
        {
            return;
        }

        uint32_t toSend = std::min(available, m_pendingTx->GetSize());
        Ptr<Packet> fragment = m_pendingTx->CreateFragment(0, toSend);
        int sent = m_socket->Send(fragment);

        if (sent <= 0)
        {
            return;
        }

        m_pendingTx->RemoveAtStart(static_cast<uint32_t>(sent));
    }

    if (m_pendingTx && m_pendingTx->GetSize() == 0)
    {
        m_pendingTx = nullptr;
        NS_LOG_INFO("complete request queued to TCP at " << Simulator::Now().As(Time::MS));
    }
}

// Drains all currently available bytes from the socket, appending them to the
// reassembly buffer, then tries to parse a complete response frame out of it.
void
GenAIUser::HandleRead(Ptr<Socket> socket)
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

// Frame parser: peeks the fixed-size header to learn the payload length, returns if the
// full frame has not arrived yet, otherwise strips header+payload, verifies it is a
// RESPONSE, logs the end-to-end transaction latency, and closes the connection.
void
GenAIUser::ProcessReceiveBuffer()
{
    GenAIMessageHeader header;

    while (m_rxBuffer->GetSize() >= header.GetSerializedSize())
    {
        m_rxBuffer->PeekHeader(header);
        NS_ABORT_MSG_IF(!header.IsValid(), "GenAIUser received an invalid application header");

        uint64_t frameSize =
            static_cast<uint64_t>(header.GetSerializedSize()) + header.GetPayloadSize();
        if (m_rxBuffer->GetSize() < frameSize)
        {
            return;
        }

        m_rxBuffer->RemoveHeader(header);
        m_rxBuffer->RemoveAtStart(header.GetPayloadSize());

        NS_ABORT_MSG_IF(header.GetMessageType() != GenAIMessageType::RESPONSE,
                        "GenAIUser received a non-response message");
        m_responseReceived = true;

        NS_LOG_INFO("received complete " << GenAIModalityToString(header.GetModality())
                                         << " response of " << header.GetPayloadSize()
                                         << " bytes at " << Simulator::Now().As(Time::MS)
                                         << "; transaction latency "
                                         << (Simulator::Now() - m_requestStart).As(Time::MS));

        m_socket->Close();
        m_connected = false;
    }
}

// Server closed: warn if it happened before a full response arrived, and mark down.
void
GenAIUser::HandlePeerClose(Ptr<Socket> socket)
{
    if (!m_responseReceived)
    {
        NS_LOG_WARN("GenAIServer closed the TCP connection before a complete response arrived");
    }
    m_connected = false;
}

// Connection failed: log the error and mark down.
void
GenAIUser::HandlePeerError(Ptr<Socket> socket)
{
    NS_LOG_ERROR("GenAIUser TCP connection closed with an error");
    m_connected = false;
}

} // namespace ns3
