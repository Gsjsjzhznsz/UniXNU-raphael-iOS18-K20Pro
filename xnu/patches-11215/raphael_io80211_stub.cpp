/*
 * A minimal IO80211Interface for iOS 18's Wi-Fi side.
 *
 * wifid matches an IOKit service named IO80211Interface before it will call the
 * radio's ioctl interface anything but "no driver" ("PPM Error: No matching
 * driver found" in its own log), which in turn leaves Settings' Wi-Fi pane
 * greyed even though SIOCSA80211/SIOCGA80211 and SIOCGIFMEDIA already work.
 * This object publishes exactly that class name and the properties wifid and
 * IO80211.framework read; the actual requests keep going through the ioctl path.
 *
 * Off unless wifi_iokit=1, so a normal boot is untouched.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>

class IO80211Interface : public IOService {
	OSDeclareDefaultStructors(IO80211Interface)
public:
	bool start(IOService *provider) override;
};

OSDefineMetaClassAndStructors(IO80211Interface, IOService)

bool
IO80211Interface::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty("IO80211InterfaceRole", "Station");
	setProperty("IO80211InterfaceProductType", 0x10ULL, 32);
	setProperty("IO80211InterfaceRadioMODE", 0x1ULL, 32);
	setProperty("IO80211InterfaceCallStatus", 0x0ULL, 32);
	setProperty("IO80211InterfaceIPV", 0x1ULL, 32);
	setProperty("IOModel", "AppleWiFi");
	setProperty("IOClass", "IO80211Interface");
	registerService();
	return true;
}


/*
 * wifid's "PPM" path also names AppleWiFi and IO80211Controller; publishing all
 * three makes the experiment tell which one its matching dictionary wants.
 */
class AppleWiFi : public IOService {
	OSDeclareDefaultStructors(AppleWiFi)
public:
	bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(AppleWiFi, IOService)
bool
AppleWiFi::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty("IOClass", "AppleWiFi");
	setProperty("IOModel", "AppleWiFi");
	registerService();
	return true;
}

class IO80211Controller : public IOService {
	OSDeclareDefaultStructors(IO80211Controller)
public:
	bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(IO80211Controller, IOService)
bool
IO80211Controller::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty("IOClass", "IO80211Controller");
	setProperty("IO80211ControllerRole", "Station");
	registerService();
	return true;
}



/*
 * A logging user client: IO80211.framework opens one on the interface and drives
 * Apple80211 through it. Answering every selector and printing the numbers turns
 * the next boot into a map of the method table, instead of guessing it.
 */
class IO80211UserClient : public IOUserClient {
	OSDeclareDefaultStructors(IO80211UserClient)
public:
	bool initWithTask(task_t owningTask, void *securityToken, UInt32 type,
	    OSDictionary *properties) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
	    IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(IO80211UserClient, IOUserClient)

bool
IO80211UserClient::initWithTask(task_t owningTask, void *securityToken, UInt32 type,
    OSDictionary *properties)
{
	if (!IOUserClient::initWithTask(owningTask, securityToken, type, properties)) {
		return false;
	}
	printf("HAKIOS IO80211: user client opened, type %u properties %s\n", (unsigned)type,
	    properties != NULL ? properties->getMetaClass()->getClassName() : "none");
	return true;
}

IOReturn
IO80211UserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
    IOExternalMethodDispatch *dispatch, OSObject *target, void *reference)
{
	static unsigned said;
	if (said < 200) {
		said++;
		printf("HAKIOS IO80211: selector %u, scalars in %u out %u, struct in %u out %u\n",
		    (unsigned)selector,
		    (unsigned)(arguments ? arguments->scalarInputCount : 0),
		    (unsigned)(arguments ? arguments->scalarOutputCount : 0),
		    (unsigned)(arguments ? arguments->structureInputSize : 0),
		    (unsigned)(arguments ? arguments->structureOutputSize : 0));
	}
	if (arguments != NULL && said < 60) {
		uint64_t s0 = arguments->scalarInputCount > 0 ? arguments->scalarInput[0] : 0;
		uint64_t s1 = arguments->scalarInputCount > 1 ? arguments->scalarInput[1] : 0;
		uint64_t s2 = arguments->scalarInputCount > 2 ? arguments->scalarInput[2] : 0;
		uint32_t in0 = 0;
		if (arguments->structureInput != NULL && arguments->structureInputSize >= 4) {
			in0 = *(const uint32_t *)arguments->structureInput;
		}
		printf("HAKIOS IO80211: call sel %u scalars %u [%llx %llx %llx] struct in %u first %x\n",
		    (unsigned)selector, (unsigned)arguments->scalarInputCount,
		    (unsigned long long)s0, (unsigned long long)s1, (unsigned long long)s2,
		    (unsigned)arguments->structureInputSize, in0);
	}
	if (arguments != NULL && arguments->structureOutput != NULL && arguments->structureOutputSize > 0) {
		bzero(arguments->structureOutput, arguments->structureOutputSize);
		if (arguments->structureOutputSize == 1) {
			((uint8_t *)arguments->structureOutput)[0] = 1;
		} else if (selector == 0 && arguments->structureOutputSize >= 256) {
			((uint32_t *)arguments->structureOutput)[0] = 1;   /* a version, not zero */
		}
	}
	return kIOReturnSuccess;
}

IOReturn
IO80211UserClient::clientClose(void)
{
	return kIOReturnSuccess;
}

/*
 * iOS 18's wifid matches this one, not the older classes: its log line is
 * "IOServiceAddMatchingNotification IO80211SkywalkInterface failed" when the
 * class is absent. Publish it with the interface name so the ioctl-backed
 * en0 is what it binds to.
 */
class IO80211SkywalkInterface : public IOService {
	OSDeclareDefaultStructors(IO80211SkywalkInterface)
public:
	bool start(IOService *provider) override;
	IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type,
	    OSDictionary *properties, IOUserClient **handler) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(IO80211SkywalkInterface, IOService)
IOReturn
IO80211SkywalkInterface::newUserClient(task_t owningTask, void *securityID, UInt32 type,
    OSDictionary *properties, IOUserClient **handler)
{
	IO80211UserClient *client = new IO80211UserClient;
	if (client == NULL) {
		return kIOReturnNoMemory;
	}
	if (!client->initWithTask(owningTask, securityID, type, properties)) {
		client->release();
		return kIOReturnError;
	}
	if (!client->attach(this)) {
		client->release();
		return kIOReturnError;
	}
	if (!client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnError;
	}
	*handler = client;
	return kIOReturnSuccess;
}
bool
IO80211SkywalkInterface::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty("IOClass", "IO80211SkywalkInterface");
	setProperty("IOInterfaceName", "en0");
	setProperty("IOInterfaceNamePrefix", "en");
	setProperty("IOInterfaceUnit", 0ULL, 32);
	setProperty("IOBSDName", "en0");
	setProperty("IOInterfaceType", "IEEE80211");
	static const uint8_t wifiAddress[6] = { 0xe0, 0xdc, 0xff, 0xda, 0xf8, 0xe7 };
	OSData *address = OSData::withBytes(wifiAddress, sizeof(wifiAddress));
	if (address != NULL) {
		setProperty("WifiAddressData", address);
		address->release();
	}
	setProperty("IO80211InterfaceRole", "Infrastructure");
	setProperty("IO80211InterfaceProductType", 0x10ULL, 32);
	setProperty("IO80211InterfaceRadioMODE", 0x1ULL, 32);
	registerService();
	return true;
}


/* Apple's Skywalk Wi-Fi driver classes, named next to the interface roles in wifid. */
class IONetworkInterface : public IOService {
	OSDeclareDefaultStructors(IONetworkInterface)
public:
	bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(IONetworkInterface, IOService)
bool
IONetworkInterface::start(IOService *provider)
{
	if (!IOService::start(provider)) return false;
	setProperty("IOClass", "IONetworkInterface");
	setProperty("IOBSDName", "en0");
	setProperty("IOInterfaceName", "en0");
	setProperty("IOInterfaceNamePrefix", "en");
	setProperty("IOInterfaceUnit", 0ULL, 32);
	setProperty("IOInterfaceType", "IEEE80211");
	registerService();
	return true;
}

class AppleWLANDriver : public IOService {
	OSDeclareDefaultStructors(AppleWLANDriver)
public:
	bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(AppleWLANDriver, IOService)
bool
AppleWLANDriver::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty("IOClass", "AppleWLANDriver");
	setProperty("IO80211InterfaceRole", "Infrastructure");
	registerService();
	return true;
}

class AppleSunriseWLAN : public IOService {
	OSDeclareDefaultStructors(AppleSunriseWLAN)
public:
	bool start(IOService *provider) override;
};
OSDefineMetaClassAndStructors(AppleSunriseWLAN, IOService)
bool
AppleSunriseWLAN::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	setProperty("IOClass", "AppleSunriseWLAN");
	registerService();
	return true;
}

static IO80211Interface *gIO80211Stub;

extern "C" void
hakios_io80211_stub_start(void)
{
	OSDictionary *match;
	IOService *parent;

	if (gIO80211Stub != NULL) {
		return;
	}
	match = IOService::serviceMatching("IOPlatformExpertDevice");
	if (match == NULL) {
		printf("HAKIOS IO80211: no matching dictionary\n");
		return;
	}
	parent = IOService::waitForMatchingService(match, 5000);
	match->release();
	if (parent == NULL) {
		printf("HAKIOS IO80211: no platform expert to attach to\n");
		return;
	}
	gIO80211Stub = new IO80211Interface;
	if (gIO80211Stub == NULL) {
		parent->release();
		return;
	}
	if (!gIO80211Stub->init()) {
		printf("HAKIOS IO80211: init failed\n");
		gIO80211Stub->release();
		gIO80211Stub = NULL;
		parent->release();
		return;
	}
	gIO80211Stub->attach(parent);
	gIO80211Stub->start(parent);
	AppleWiFi *wifi = new AppleWiFi;
	if (wifi != NULL && wifi->init()) {
		wifi->attach(parent);
		wifi->start(parent);
	}
	IO80211Controller *controller = new IO80211Controller;
	if (controller != NULL && controller->init()) {
		controller->attach(parent);
		controller->start(parent);
	}
	IONetworkInterface *netif = new IONetworkInterface;
	if (netif != NULL && netif->init()) {
		netif->attach(parent);
		netif->start(parent);
	}
	AppleWLANDriver *driver = new AppleWLANDriver;
	if (driver != NULL && driver->init()) {
		IOService *driverParent = netif != NULL ? (IOService *)netif : parent;
		driver->attach(driverParent);
		driver->start(driverParent);
	}
	IO80211SkywalkInterface *skywalk = new IO80211SkywalkInterface;
	if (skywalk != NULL && skywalk->init()) {
		IOService *skywalkParent = driver != NULL ? (IOService *)driver : parent;
		skywalk->attach(skywalkParent);
		skywalk->start(skywalkParent);
	}
	AppleSunriseWLAN *sunrise = new AppleSunriseWLAN;
	if (sunrise != NULL && sunrise->init()) {
		sunrise->attach(parent);
		sunrise->start(parent);
	}
	printf("HAKIOS IO80211: IO80211Interface/AppleWiFi/IO80211Controller/IO80211SkywalkInterface published (wifi_iokit=1)\n");
	parent->release();
}
