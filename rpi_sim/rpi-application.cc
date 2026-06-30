/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "rpi-application.h"

#include "ns3/address.h"
#include "ns3/log.h"
#include "ns3/packet.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/uinteger.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RpiApplication");
NS_OBJECT_ENSURE_REGISTERED(RpiApplication);

TypeId
RpiApplication::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RpiApplication")
                            .SetParent<Application>()
                            .SetGroupName("Applications")
                            .AddConstructor<RpiApplication>()
                            .AddAttribute("Remote",
                                          "The destination address for generated packets.",
                                          AddressValue(),
                                          MakeAddressAccessor(&RpiApplication::m_remote),
                                          MakeAddressChecker())
                            .AddAttribute("Interval",
                                          "Time between generated packets.",
                                          TimeValue(Seconds(1)),
                                          MakeTimeAccessor(&RpiApplication::m_interval),
                                          MakeTimeChecker())
                            .AddAttribute("PacketSize",
                                          "Generated packet payload size in bytes.",
                                          UintegerValue(128),
                                          MakeUintegerAccessor(&RpiApplication::m_packetSize),
                                          MakeUintegerChecker<uint32_t>(1, 65507))
                            .AddAttribute("MaxPackets",
                                          "Maximum number of packets to send. Zero means send until stopped.",
                                          UintegerValue(5),
                                          MakeUintegerAccessor(&RpiApplication::m_maxPackets),
                                          MakeUintegerChecker<uint32_t>())
                            .AddTraceSource("Tx",
                                            "A packet was sent.",
                                            MakeTraceSourceAccessor(&RpiApplication::m_txTrace),
                                            "ns3::Packet::TracedCallback");
    return tid;
}

RpiApplication::RpiApplication()
    : m_socket(nullptr),
      m_packetsSent(0)
{
    NS_LOG_FUNCTION(this);
}

RpiApplication::~RpiApplication()
{
    NS_LOG_FUNCTION(this);
}

void
RpiApplication::StartApplication()
{
    NS_LOG_FUNCTION(this);

    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Connect(m_remote);
    }

    m_packetsSent = 0;
    SendPacket();
}

void
RpiApplication::StopApplication()
{
    NS_LOG_FUNCTION(this);
    Simulator::Cancel(m_sendEvent);

    if (m_socket)
    {
        m_socket->Close();
        m_socket = nullptr;
    }
}

void
RpiApplication::SendPacket()
{
    NS_LOG_FUNCTION(this);

    Ptr<Packet> packet = Create<Packet>(m_packetSize);
    m_socket->Send(packet);
    m_txTrace(packet);

    ++m_packetsSent;
    NS_LOG_INFO("sent packet " << m_packetsSent << " of " << m_packetSize << " bytes at "
                               << Simulator::Now().As(Time::S));

    if (m_maxPackets == 0 || m_packetsSent < m_maxPackets)
    {
        ScheduleNextTx();
    }
}

void
RpiApplication::ScheduleNextTx()
{
    NS_LOG_FUNCTION(this);
    m_sendEvent = Simulator::Schedule(m_interval, &RpiApplication::SendPacket, this);
}

} // namespace ns3
