/*
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef RPI_APPLICATION_H
#define RPI_APPLICATION_H

#include "ns3/address.h"
#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"
#include "ns3/type-id.h"
#include "ns3/traced-callback.h"

namespace ns3
{

class Packet;
class Socket;

class RpiApplication : public Application
{
  public:
    static TypeId GetTypeId();

    RpiApplication();
    ~RpiApplication() override;

  private:
    void StartApplication() override;
    void StopApplication() override;

    void ScheduleNextTx();
    void SendPacket();

    Address m_remote;
    Time m_interval;
    uint32_t m_packetSize;
    uint32_t m_maxPackets;

    Ptr<Socket> m_socket;
    EventId m_sendEvent;
    uint32_t m_packetsSent;

    TracedCallback<Ptr<const Packet>> m_txTrace;
};

} // namespace ns3

#endif /* RPI_APPLICATION_H */
