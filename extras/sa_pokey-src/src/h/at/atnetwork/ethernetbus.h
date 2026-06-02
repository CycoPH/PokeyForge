//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#ifndef f_AT_ATNETWORK_ETHERNETBUS_H
#define f_AT_ATNETWORK_ETHERNETBUS_H

#include <vd2/system/vdstl.h>
#include <at/atnetwork/ethernet.h>

class ATEthernetBus final : public IATEthernetSegment, protected IATEthernetClockEventSink {
	ATEthernetBus(const ATEthernetBus&);
	ATEthernetBus& operator=(const ATEthernetBus&);
public:
	ATEthernetBus();
	~ATEthernetBus();

	uint32 AddEndpoint(IATEthernetEndpoint *endpoint) override;
	void RemoveEndpoint(uint32 endpointId) override;

	IATEthernetClock *GetClock(uint32 clockId) const override;
	uint32 AddClock(IATEthernetClock *clock) override;
	void RemoveClock(uint32 clockId) override;

	void ClearPendingFrames();
	void TransmitFrame(uint32 source, const ATEthernetPacket& packet) override;

protected:
	void OnClockEvent(uint32 eventid, uint32 userid) override;

protected:
	uint32 mNextEndpoint;
	uint32 mNextPacketId;

	struct Endpoint {
		IATEthernetEndpoint *mpEndpoint;
		uint32 mId;
	};

	typedef vdfastvector<Endpoint> Endpoints;
	Endpoints mEndpoints;

	typedef vdfastvector<IATEthernetClock *> Clocks;
	Clocks mClocks;

	struct QueuedPacket {
		uint32 mSourceId;
		uint32 mClockEventId;
		uint32 mNextPacketId;
		ATEthernetPacket mPacket;
	};

	typedef vdhashmap<uint32, QueuedPacket *> Packets;
	Packets mPackets;

	typedef vdhashmap<uint32, QueuedPacket *> PacketsByTimestamp;
	PacketsByTimestamp mPacketsByTimestamp;
};

#endif
