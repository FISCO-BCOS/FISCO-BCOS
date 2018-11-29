/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file UPnP.cpp
 * @authors:
 *   Gav Wood <i@gavwood.com>
 *   Lefteris Karapetsas <lefteris@refu.co>
 * @date 2014, 2015
 */

#include "UPnP.h"

#include <stdio.h>
#include <string.h>
#if ETH_MINIUPNPC
#include <miniupnpc/miniwget.h>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#endif
#include <libdevcore/Exceptions.h>
#include <libdevcore/Common.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/easylog.h>
using namespace std;
using namespace dev;
using namespace dev::p2p;

UPnP::UPnP()
{
}

UPnP::~UPnP()
{
	auto r = m_reg;
	for (auto i: r)
		removeRedirect(i);
}

string UPnP::externalIP()
{
#if ETH_MINIUPNPC
	char addr[16];
	if (!UPNP_GetExternalIPAddress(m_urls->controlURL, m_data->first.servicetype, addr))
		return addr;
	else
#endif
		return "0.0.0.0";
}

int UPnP::addRedirect(char const* _addr, int _port)
{
	(void)_addr;
	(void)_port;
#if ETH_MINIUPNPC
	if (m_urls->controlURL[0] == '\0')
	{
		LOG(WARNING) << "UPnP::addRedirect() called without proper initialisation?";
		return -1;
	}

	// Try direct mapping first (port external, port internal).
	char port_str[16];
	char ext_port_str[16];
	sprintf(port_str, "%d", _port);
	if (!UPNP_AddPortMapping(m_urls->controlURL, m_data->first.servicetype, port_str, port_str, _addr, "ethereum", "TCP", NULL, NULL))
		return _port;

	// Failed - now try (random external, port internal) and cycle up to 10 times.
	srand(time(NULL));
	for (unsigned i = 0; i < 10; ++i)
	{
		_port = rand() % (32768 - 1024) + 1024;
		sprintf(ext_port_str, "%d", _port);
		if (!UPNP_AddPortMapping(m_urls->controlURL, m_data->first.servicetype, ext_port_str, port_str, _addr, "ethereum", "TCP", NULL, NULL))
			return _port;
	}

	// Failed. Try asking the router to give us a free external port.
	if (UPNP_AddPortMapping(m_urls->controlURL, m_data->first.servicetype, port_str, NULL, _addr, "ethereum", "TCP", NULL, NULL))
		// Failed. Exit.
		return 0;

	// We got mapped, but we don't know which ports we got mapped to. Now to find...
	unsigned num = 0;
	UPNP_GetPortMappingNumberOfEntries(m_urls->controlURL, m_data->first.servicetype, &num);
	for (unsigned i = 0; i < num; ++i)
	{
		char extPort[16];
		char intClient[16];
		char intPort[6];
		char protocol[4];
		char desc[80];
		char enabled[4];
		char rHost[64];
		char duration[16];
		UPNP_GetGenericPortMappingEntry(m_urls->controlURL, m_data->first.servicetype, toString(i).c_str(), extPort, intClient, intPort, protocol, desc, enabled, rHost, duration);
		if (string("ethereum") == desc)
		{
			m_reg.insert(atoi(extPort));
			return atoi(extPort);
		}
	}
	LOG(ERROR) << "ERROR: Mapped port not found." << "\n";
#endif
	return 0;
}

void UPnP::removeRedirect(int _port)
{
	(void)_port;
#if ETH_MINIUPNPC
	char port_str[16];
//		int t;
	printf("TB : upnp_rem_redir (%d)\n", _port);
	if (m_urls->controlURL[0] == '\0')
	{
		printf("TB : the init was not done !\n");
		return;
	}
	sprintf(port_str, "%d", _port);
	UPNP_DeletePortMapping(m_urls->controlURL, m_data->first.servicetype, port_str, "TCP", NULL);
	m_reg.erase(_port);
#endif
}
