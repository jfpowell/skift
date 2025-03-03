#pragma once

#include "kernel/devices/Device.h"
#include "kernel/devices/DeviceAddress.h"

class DeviceDriver
{
private:
    DeviceBus _bus;
    const char *_name;

public:
    DeviceBus bus() const { return _bus; }

    const char *name() const { return _name; }

    constexpr DeviceDriver(DeviceBus bus, const char *name)
        : _bus(bus),
          _name(name)
    {
    }

    virtual bool match(DeviceAddress address) = 0;

    virtual RefPtr<Device> instance(DeviceAddress address) = 0;
};

void driver_initialize();

DeviceDriver *driver_for(DeviceAddress address);
