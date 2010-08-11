/*
 * Copyright 2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <NetworkInterface.h>

#include <errno.h>
#include <net/if.h>
#include <sys/sockio.h>

#include <AutoDeleter.h>


static status_t
do_ifaliasreq(const char* name, int32 option, BNetworkInterfaceAddress& address,
	bool readBack = false)
{
	int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (socket < 0)
		return errno;

	FileDescriptorCloser closer(socket);

	ifaliasreq request;
	strlcpy(request.ifra_name, name, IF_NAMESIZE);
	request.ifra_index = address.Index();
	request.ifra_flags = address.Flags();

	memcpy(&request.ifra_addr, &address.Address().SockAddr(),
		address.Address().Length());
	memcpy(&request.ifra_mask, &address.Mask().SockAddr(),
		address.Mask().Length());
	memcpy(&request.ifra_broadaddr, &address.Broadcast().SockAddr(),
		address.Broadcast().Length());
	
	if (ioctl(socket, option, &request, sizeof(struct ifaliasreq)) < 0)
		return errno;

	if (readBack) {
		address.SetFlags(request.ifra_flags);
		address.Address().SetTo(request.ifra_addr);
		address.Mask().SetTo(request.ifra_mask);
		address.Broadcast().SetTo(request.ifra_broadaddr);
	}

	return B_OK;
}


static status_t
do_ifaliasreq(const char* name, int32 option,
	const BNetworkInterfaceAddress& address)
{
	return do_ifaliasreq(name, option,
		const_cast<BNetworkInterfaceAddress&>(address));
}


static status_t
do_request(ifreq& request, const char* name, int option)
{
	int socket = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (socket < 0)
		return errno;

	FileDescriptorCloser closer(socket);

	strlcpy(request.ifr_name, name, IF_NAMESIZE);

	if (ioctl(socket, option, &request, sizeof(struct ifreq)) < 0)
		return errno;

	return B_OK;
}


// #pragma mark -


BNetworkInterfaceAddress::BNetworkInterfaceAddress()
	:
	fIndex(0),
	fFlags(0)
{
}


BNetworkInterfaceAddress::~BNetworkInterfaceAddress()
{
}


status_t
BNetworkInterfaceAddress::SetTo(BNetworkInterface& interface, int32 index)
{
	fIndex = index;
	return do_ifaliasreq(interface.Name(), B_SOCKET_GET_ALIAS, *this, true);
}


void
BNetworkInterfaceAddress::SetAddress(BNetworkAddress& address)
{
	fAddress = address;
}


void
BNetworkInterfaceAddress::SetMask(BNetworkAddress& mask)
{
	fMask = mask;
}


void
BNetworkInterfaceAddress::SetBroadcast(BNetworkAddress& broadcast)
{
	fBroadcast = broadcast;
}


void
BNetworkInterfaceAddress::SetFlags(uint32 flags)
{
	fFlags = flags;
}


// #pragma mark -


BNetworkInterface::BNetworkInterface()
{
	Unset();
}


BNetworkInterface::BNetworkInterface(const char* name)
{
	SetTo(name);
}


BNetworkInterface::BNetworkInterface(uint32 index)
{
	SetTo(index);
}


BNetworkInterface::~BNetworkInterface()
{
}


void
BNetworkInterface::Unset()
{
	fName[0] = '\0';
}


void
BNetworkInterface::SetTo(const char* name)
{
	strlcpy(fName, name, IF_NAMESIZE);
}


status_t
BNetworkInterface::SetTo(uint32 index)
{
	ifreq request;
	request.ifr_index = index;

	status_t status = do_request(request, "", SIOCGIFNAME);
	if (status != B_OK)
		return status;

	strlcpy(fName, request.ifr_name, IF_NAMESIZE);
	return B_OK;
}


bool
BNetworkInterface::Exists() const
{
	ifreq request;
	return do_request(request, Name(), SIOCGIFINDEX) == B_OK;
}


const char*
BNetworkInterface::Name() const
{
	return fName;
}


uint32
BNetworkInterface::Flags() const
{
	ifreq request;
	if (do_request(request, Name(), SIOCGIFFLAGS) != B_OK)
		return 0;

	return request.ifr_flags;
}


uint32
BNetworkInterface::MTU() const
{
	ifreq request;
	if (do_request(request, Name(), SIOCGIFMTU) != B_OK)
		return 0;

	return request.ifr_mtu;
}


uint32
BNetworkInterface::Type() const
{
	ifreq request;
	if (do_request(request, Name(), SIOCGIFTYPE) != B_OK)
		return 0;

	return request.ifr_type;
}


status_t
BNetworkInterface::GetStats(ifreq_stats& stats)
{
	ifreq request;
	status_t status = do_request(request, Name(), SIOCGIFSTATS);
	if (status != B_OK)
		return status;

	memcpy(&stats, &request.ifr_stats, sizeof(ifreq_stats));
	return B_OK;
}


bool
BNetworkInterface::HasLink() const
{
	return (Flags() & IFF_LINK) != 0;
}


status_t
BNetworkInterface::SetFlags(uint32 flags)
{
	ifreq request;
	request.ifr_flags = flags;
	return do_request(request, Name(), SIOCSIFFLAGS);
}


status_t
BNetworkInterface::SetMTU(uint32 mtu)
{
	ifreq request;
	request.ifr_mtu = mtu;
	return do_request(request, Name(), SIOCSIFMTU);
}


int32
BNetworkInterface::CountAddresses() const
{
	ifreq request;
	if (do_request(request, Name(), B_SOCKET_COUNT_ALIASES) != B_OK)
		return 0;

	return request.ifr_count;
}


status_t
BNetworkInterface::GetAddressAt(int32 index, BNetworkInterfaceAddress& address)
{
	return address.SetTo(*this, index);
}


status_t
BNetworkInterface::AddAddress(const BNetworkInterfaceAddress& address)
{
	return do_ifaliasreq(Name(), B_SOCKET_ADD_ALIAS, address);
}


status_t
BNetworkInterface::SetAddress(const BNetworkInterfaceAddress& address)
{
	return do_ifaliasreq(Name(), B_SOCKET_SET_ALIAS, address);
}


status_t
BNetworkInterface::RemoveAddress(const BNetworkInterfaceAddress& address)
{
	ifreq request;
	memcpy(&request.ifr_addr, &address.Address().SockAddr(),
		address.Address().Length());

	return do_request(request, Name(), B_SOCKET_REMOVE_ALIAS);
}


status_t
BNetworkInterface::RemoveAddressAt(int32 index)
{
	BNetworkInterfaceAddress address;
	status_t status = GetAddressAt(index, address);
	if (status != B_OK)
		return status;

	return RemoveAddress(address);
}


status_t
BNetworkInterface::GetHardwareAddress(BNetworkAddress& address)
{
	int socket = ::socket(AF_LINK, SOCK_DGRAM, 0);
	if (socket < 0)
		return errno;

	FileDescriptorCloser closer(socket);

	ifreq request;
	strlcpy(request.ifr_name, Name(), IF_NAMESIZE);

	if (ioctl(socket, SIOCGIFADDR, &request, sizeof(struct ifreq)) < 0)
		return errno;

	address.SetTo(request.ifr_addr);
	return B_OK;
}