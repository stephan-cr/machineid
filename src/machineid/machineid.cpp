#include "machineid/machineid.h"
#include <cstdint>
#include <sstream>

using u16 = uint16_t;
using u32 = uint32_t;

namespace machineid {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <iphlpapi.h>

// we just need this for purposes of unique machine id. So any one or two mac's is
// fine.
u16 hashMacAddress(PIP_ADAPTER_INFO info) {
	u16 hash = 0;
	for (u32 i = 0; i < info->AddressLength; i++) {
		hash += (info->Address[i] << ((i & 1) * 8));
	}
	return hash;
}

void getMacHash(u16 &mac1, u16 &mac2) {
	IP_ADAPTER_INFO AdapterInfo[32];
	DWORD dwBufLen = sizeof(AdapterInfo);

	DWORD dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);
	if (dwStatus != ERROR_SUCCESS)
		return; // no adapters.

	PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
	mac1 = hashMacAddress(pAdapterInfo);
	if (pAdapterInfo->Next)
		mac2 = hashMacAddress(pAdapterInfo->Next);

	// sort the mac addresses. We don't want to invalidate
	// both macs if they just change order.
	if (mac1 > mac2) {
		u16 tmp = mac2;
		mac2 = mac1;
		mac1 = tmp;
	}
}

u16 getVolumeHash() {
	DWORD serialNum = 0;

	// Determine if this volume uses an NTFS file system.
	GetVolumeInformation("c:\\", NULL, 0, &serialNum, NULL, NULL, NULL, 0);
	u16 hash = (u16)((serialNum + (serialNum >> 16)) & 0xFFFF);

	return hash;
}

u16 getCpuHash() {
	int cpuinfo[4] = {0, 0, 0, 0};
	__cpuid(cpuinfo, 0);
	u16 hash = 0;
	u16 *ptr = (u16 * )(&cpuinfo[0]);
	for (u32 i = 0; i < 8; i++)
		hash += ptr[i];

	return hash;
}

const char *getMachineName() {
	static char computerName[1024];
	DWORD size = 1024;
	GetComputerName(computerName, &size);
	return &(computerName[0]);
}
#else
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#ifdef DARWIN
#include <net/if_dl.h>
#include <ifaddrs.h>
#include <net/if_types.h>
#else //!DARWIN
#include <linux/if.h>
#include <linux/sockios.h>
#endif //!DARWIN

	const char *getMachineName() {
		static struct utsname u;

		if (uname(&u) < 0) {
			return "unknown";
		}

		return u.nodename;
	}


	//---------------------------------get MAC addresses ------------------------------------unsigned short-unsigned short----------
	// we just need this for purposes of unique machine id. So any one or two mac's is fine.
	unsigned short hashMacAddress(unsigned char *mac) {
		unsigned short hash = 0;

		for (unsigned int i = 0; i < 6; i++) {
			hash += (mac[i] << ((i & 1) * 8));
		}
		return hash;
	}

	void getMacHash(unsigned short &mac1, unsigned short &mac2) {
		mac1 = 0;
		mac2 = 0;

#ifdef DARWIN

		struct ifaddrs* ifaphead;
	   if ( getifaddrs( &ifaphead ) != 0 )
		  return;

	   // iterate over the net interfaces
	   bool foundMac1 = false;
	   struct ifaddrs* ifap;
	   for ( ifap = ifaphead; ifap; ifap = ifap->ifa_next )
	   {
		  struct sockaddr_dl* sdl = (struct sockaddr_dl*)ifap->ifa_addr;
		  if ( sdl && ( sdl->sdl_family == AF_LINK ) && ( sdl->sdl_type == IFT_ETHER ))
		  {
			  if ( !foundMac1 )
			  {
				 foundMac1 = true;
				 mac1 = hashMacAddress( (unsigned char*)(LLADDR(sdl))); //sdl->sdl_data) + sdl->sdl_nlen) );
			  } else {
				 mac2 = hashMacAddress( (unsigned char*)(LLADDR(sdl))); //sdl->sdl_data) + sdl->sdl_nlen) );
				 break;
			  }
		  }
	   }

	   freeifaddrs( ifaphead );

#else // !DARWIN

		int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
		if (sock < 0)
			return;

		// enumerate all IP addresses of the system
		struct ifconf conf;
		char ifconfbuf[128 * sizeof(struct ifreq)];
		memset(ifconfbuf, 0, sizeof(ifconfbuf));
		conf.ifc_buf = ifconfbuf;
		conf.ifc_len = sizeof(ifconfbuf);
		if (ioctl(sock, SIOCGIFCONF, &conf)) {
			return;
		}

		// get MAC address
		bool foundMac1 = false;
		struct ifreq *ifr;
		for (ifr = conf.ifc_req; (char *)ifr < (char *)conf.ifc_req + conf.ifc_len; ifr++) {
			if (memcmp(ifr->ifr_addr.sa_data, (ifr + 1)->ifr_addr.sa_data, IFHWADDRLEN) == 0)
				continue;  // duplicate, skip it

			if (ioctl(sock, SIOCGIFFLAGS, ifr))
				continue;  // failed to get flags, skip it
			if (ioctl(sock, SIOCGIFHWADDR, ifr) == 0) {
				if (!foundMac1) {
					foundMac1 = true;
					mac1 = hashMacAddress((unsigned char *)&(ifr->ifr_addr.sa_data));
				} else {
					mac2 = hashMacAddress((unsigned char *)&(ifr->ifr_addr.sa_data));
					break;
				}
			}
		}

		close(sock);

#endif // !DARWIN

		// sort the mac addresses. We don't want to invalidate
		// both macs if they just change order.
		if (mac1 > mac2) {
			unsigned short tmp = mac2;
			mac2 = mac1;
			mac1 = tmp;
		}
	}

	unsigned short getVolumeHash() {
		// we don't have a 'volume serial number' like on windows. Lets hash the system name instead.
		unsigned char *sysname = (unsigned char *)getMachineName();
		unsigned short hash = 0;

		for (unsigned int i = 0; sysname[i]; i++)
			hash += (sysname[i] << ((i & 1) * 8));

		return hash;
	}

#ifdef DARWIN
#include <mach-o/arch.h>
	unsigned short getCpuHash()
	{
		const NXArchInfo* info = NXGetLocalArchInfo();
		unsigned short val = 0;
		val += (unsigned short)info->cputype;
		val += (unsigned short)info->cpusubtype;
		return val;
	}

#else // !DARWIN

	static void getCpuid(unsigned int *eax, unsigned int *ebx, unsigned int *ecx, unsigned int *edx) {
#if defined(__arm__)
		*eax = 0xFD;
		*ebx = 0xC1;
		*ecx = 0x72;
		*edx = 0x1D;
#elif defined(__aarch64__)
		uint64_t midr;

		asm volatile("mrs %0, MIDR_EL1" : "=r"(midr));

		// Split the 64-bit value into four 32-bit parts
		*eax = (uint32_t)(midr & 0xFFFFFFFF);
		*ebx = (uint32_t)((midr >> 32) & 0xFFFFFFFF);
		*ecx = 0;
		*edx = 0;
#elif defined(__x86_64__) || defined(__i386__)
		asm volatile("cpuid" :
			"=a" (*eax),
			"=b" (*ebx),
			"=c" (*ecx),
			"=d" (*edx) : "0" (*eax), "2" (*ecx));
#else
#error Unsupported CPU architecture
#endif
	}

	unsigned short getCpuHash() {
		unsigned int cpuinfo[4] = {0, 0, 0, 0};
		getCpuid(&cpuinfo[0], &cpuinfo[1], &cpuinfo[2], &cpuinfo[3]);
		unsigned short hash = 0;
		unsigned int *ptr = (&cpuinfo[0]);
		for (unsigned int i = 0; i < 4; i++)
			hash += (ptr[i] & 0xFFFF) + (ptr[i] >> 16);

		return hash;
	}

#endif // !DARWIN
#endif

	std::string generateHash(const std::string &bytes) {
		static char chars[] = "0123456789ABCDEF";
		std::stringstream stream;

		auto size = bytes.size();
		for (unsigned long i = 0; i < size; ++i) {
			unsigned char ch = ~((unsigned char)((unsigned short)bytes[i] +
				(unsigned short)bytes[(i + 1) % size] +
				(unsigned short)bytes[(i + 2) % size] +
				(unsigned short)bytes[(i + 3) % size])) * (i + 1);

			stream << chars[(ch >> 4) & 0x0F] << chars[ch & 0x0F];
		}

		return stream.str();
	}


	static std::string *cachedHash = nullptr;

	std::string machineHash() {
		static const unsigned long TargetLength = 64;

		if (cachedHash != nullptr) {
			return *cachedHash;
		}

		std::stringstream stream;

		stream << getMachineName();
		stream << getCpuHash();
		stream << getVolumeHash();

		auto string = stream.str();

		while (string.size() < TargetLength) {
			string = string + string;
		}

		if (string.size() > TargetLength) {
			string = string.substr(0, TargetLength);
		}

		return generateHash(string);
	}
}
