/*
 *  Copyright (C) 2004-2015 Savoir-Faire Linux Inc.
 *  Author: Stepan Salenikovich <stepan.salenikovich@savoirfairelinux.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *  Additional permission under GNU GPL version 3 section 7:
 *
 *  If you modify this program, or any covered work, by linking or
 *  combining it with the OpenSSL project's OpenSSL library (or a
 *  modified version of that library), containing parts covered by the
 *  terms of the OpenSSL or SSLeay licenses, Savoir-Faire Linux Inc.
 *  grants you additional permission to convey the resulting work.
 *  Corresponding Source for a non-source form of such a combination
 *  shall include the source code for the parts of OpenSSL used as well
 *  as that of the covered work.
 */

#ifndef UPNP_CONTEXT_H_
#define UPNP_CONTEXT_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <set>
#include <map>
#include <mutex>
#include <memory>
#include <condition_variable>

#if HAVE_LIBUPNP
#include <upnp/upnp.h>
#include <upnp/upnptools.h>
#endif

#include "noncopyable.h"
#include "upnp_igd.h"

namespace ring {
class IpAddr;
}

namespace ring { namespace upnp {

class UPnPContext {

public:

#if HAVE_LIBUPNP

    /* search timeout in seconds */
    constexpr static int SEARCH_TIMEOUT_SEC = 30;

    UPnPContext();
    ~UPnPContext();

    /**
     * returns 'true' if there is at least one valid (connected) IGD
     * note: this function will wait until an IGD has been found or SEARCH_TIMEOUT_SEC
     *       have expired; the timeout starts when the context is created as we start
     *       searching for IGDs immediately
     */
    bool hasValidIGD();

    /**
     * tries to add mapping from and to the port_desired
     * if unique == true, makes sure the client is not using this port already
     * if the mapping fails, tries other available ports until success
     *
     * tries to use a random port between 1024 < > 65535 if desired port fails
     *
     * maps port_desired to port_local; if use_same_port == true, makes sure
     * that the external and internal ports are the same
     *
     * returns a valid mapping on success and an invalid mapping on failure
     *
     * note: this function will call hasValidIGD and wait upto SEARCH_TIMEOUT_SEC
     */
    Mapping addAnyMapping(uint16_t port_desired,
                          uint16_t port_local,
                          PortType type,
                          bool use_same_port,
                          bool unique);

    /**
     * tries to remove the given mapping
     *
     * note: this function will call hasValidIGD and wait upto SEARCH_TIMEOUT_SEC
     */
    void removeMapping(const Mapping& mapping);

    IpAddr getExternalIP();

#else
    /* use default constructor and destructor */
    UPnPContext() = default;
    ~UPnPContext() = default;
#endif

    bool isInitialized() const { return initialized_; };

private:
    NON_COPYABLE(UPnPContext);

    bool initialized_ {false};

#if HAVE_LIBUPNP

    /**
     * UPnP devices typically send out several discovery
     * packets at the same time. libupnp creates a separate event
     * for each discovery packet which is processed in the threadpool,
     * even if the multiple discovery packets are received from the
     * same IP at the same time. In order to prevent trying
     * to download and parse the device description from the
     * same location in multiple threads at the same time, we
     * keep track from which URL(s) we are in the process of downloading
     * and parsing the device description in this set.
     *
     * The main purspose of this is to prevent blocking multiple
     * threads when trying to download the description from an
     * unresponsive device (the timeout can be several seconds)
     *
     * The mutex is to access the set in a thread safe manner
     */
    std::set<std::string> cpDevices_;
    std::mutex cpDeviceMutex_;

    /**
     * control and device handles;
     * set by the SDK once each is registered
     */
    UpnpClient_Handle ctrlptHandle_ {-1};
    UpnpDevice_Handle deviceHandle_ {-1};

    /**
     * keep track if we've successfully registered
     * a client and/ore device
     */
    bool clientRegistered_ {false};
    bool deviceRegistered_ {false};

    /**
     * map of valid IGDs - IGDs which have the correct services and are connected
     * to some external network (have an external IP)
     *
     * the UDN string is used to uniquely identify the IGD
     *
     * the mutex is used to access these lists and IGDs in a thread-safe manner
     */
    std::map<std::string, std::unique_ptr<IGD>> validIGDs_;
    std::mutex validIGDMutex_;

    /**
     * chooses the IGD to use (currently selects the first one in the map)
     * assumes you already have a lock on igd_mutex_
     */
    IGD* chooseIGD_unlocked();

    /* sends out async search for IGD */
    void searchForIGD();

    /* vars to sync search timeout */
    unsigned pendingIGDSearchRequests_ {0};
    std::mutex igdSearchMutex_;
    std::condition_variable igdSearchCondition_;

    /**
     * callback function for the UPnP client (control point)
     * all UPnP events received by the client are processed here
     */
    friend int cp_callback(Upnp_EventType, void*, void*);

    /**
     * Parses the device description and adds desired devices to
     * relevant lists
     */
    void parseDevice(IXML_Document* doc, const Upnp_Discovery* d_event);

    void parseIGD(IXML_Document* doc, const Upnp_Discovery* d_event);

    /* tries to add mapping, assumes you alreayd have lock on igd_mutex_ */
    Mapping addMapping(IGD* igd,
                       uint16_t port_external,
                       uint16_t port_internal,
                       PortType type,
                       int *upnp_error);

    uint16_t chooseRandomPort(const IGD* igd, PortType type);

    /* these functions directly create UPnP actions
     * and make synchronous UPnP control point calls
     * they assume you have a lock on the igd_mutex_ */
    bool isIGDConnected(const IGD* igd);

    IpAddr getExternalIP(const IGD* igd);

    void removeMappingsByLocalIPAndDescription(const IGD* igd,
                                               const std::string& description);

    bool deletePortMapping(const IGD* igd,
                           const std::string& port_external,
                           const std::string& protocol);

    bool addPortMapping(const IGD* igd,
                        const Mapping& mapping,
                        int* error_code);

#endif /* HAVE_LIBUPNP */

};

/**
 * This should be used to get a UPnPContext.
 * It only makes sense to have one unless you have separate
 * contexts for multiple internet interfaces, which is not currently
 * supported.
 */
std::shared_ptr<UPnPContext> getUPnPContext();

}} // namespace ring::upnp

#endif /* UPNP_CONTEXT_H_ */