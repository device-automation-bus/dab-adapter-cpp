# Device Automation Bus - C++ Reference

## Structure
The reference code is distributed as a header-only library.

The library utilizes several classes.

    DAB::dabClient          -   This is the base class that an implementation will inherit from when implementing their DAB methods
    DAB::dabBridge          -   This class implements the dabBridge functionality.
    DAB::dabMQTTInterface   -   This class implements the MQTT interface layer.

External dependencies.

    eclipse-paho-mqtt-c     -   This library implements the MQTT layer

## Implementation

### DAB::dabClient
The dabClient class implments all the basic functionality needed to implement the DAB interface.   You must inherit it using the CRTP patttern.  This is used to allow the class to detect overrides of functionality and to auto-populate the opList DAB method.
A minimal class inheriting from DAB::dabClient with no methods being implemented (other than those implemented by DAB::dabClient would be
    
```c++
    class dab_panel : public DAB::dabClient<dab_panel>
    {
    public:
    
        dab_panel ( std::string deviceId, std::string ipAddress ) : dabClient ( deviceId )
        {}
    
        static bool isCompatible ( char const *ipAddress )
        {
            // should connect to device specified in ipAddress and see if this class is capable of managing that device
            return true;
        }
    };
```
Classes inheriting DAB::dabClient should not be instantiated directly.  Instead the class type is passed as a parameter to DAB::dabBridge.

### DAB::dabBridge

```
   DAB::dabBridge<dab_panel,...> bridge;
```
dabBridge can take multiple classes

Instantiation occurs when an instance of dabBridge has it's makeDeviceInstance method called:
```c++
    bridge.makeDeviceInstance ( <deviceId>, <ipAddress> )
```
The makeDeviceInstance call will in turn call each class type (dab_panel) and call the isCompatible() static method.

If the method returns true, the dabBridge client will instantiate the class, passing in the deviceId and ipAddress to the class's constructor.  No other classes will be examined once isCompatible returns true.

*NOTE:  The actual implementation of makeDeviceInstance is a bit more complex than stated above.  In reality, ipAddress is simply a parameter that is passed to the constructor and isCompatible.  By convention this is an ipAddress, but it can be any value of use to the implementor (a HW device ID for instance).   Additionally, it's possible to pass additional parameters to the makeDeviceInstance call and these will be perfectly forwarded to the constructor (only the first parameter is passed to the isCompatible call).

Once we have instantiated all supported devices (it's possible for DAB::dabBridge to support multiple devices with a single instance.  Each device needs to respond with isCompatible when it's ipAddress allows it to connect to a supported device.   The deviceID will be used to route requests to the appropriate instance of the class), we can now attach it to the DAB::dabMQTTInterface.

### DAB::dabMQTTInterface
```c++
auto mqtt = DAB::dabMQTTInterface ( bridge, <mqtt bride ip address> );
```

After creation of the interface we can then start handling messages by connecting to the the mqttBridge

```c++
    mqtt.connect ();
```

This method will spawn off a worker thread to handle incoming mqtt requests and pass appropriate requests to the dabClient object instantiated with associated deviceId during construction.

Because all activity occurs within a worker thread, a wait method is supplied.  This method will exit only after the mqttBridge closes the connetion or the mqtt.disconnect() method is called

```c++
    mqtt.wait ();
```

## Implementing DAB methods

The library does all the heavy lifting for you.   Implementation of DAB methods is a simple as implementing the functionality within the class inheriting from DAB::dabClient.
```c++
    DAB::jsonElement deviceInfo ()
    {
        // example return
        return {{"status",  200},
                {"version", "2.0"}};
    }
```
The library will detect the presence of an implementation method with the correct signature.  It will then add this to the opList response, and direct any requests to the method.   The method need only return a jsonElement response.

Additionally, the library implements all necessary timers/management for telemetry operations.  Telemetry start/stop (for both device and application) will simply call the defined telemetry method at the appropriate time.   The implementor need not worry about any details of managing timing queues, or starting and stopping timers as this is all handled by the library.

For signatures of all supported methods, please see the dab.cpp example file.

Additionally, the library will parse any non-optional parameters for you and pass them to the method.  Optional parameters are passed as a jsonElement const reference.

### jsonElement

The jsonElement class is the DAB clients c++ library for supporting json operations.

#### assigning a constant value
```c++
DAB::jsonElement x = 5;
```
Constants can be assigned through simple initialization to a constant.   
Internally the jsonElement supports int64_t, std::string, double, bool and null (default value)

null can be set by calling the clear() method and tested for with isNull().

#### objects
```c++
DAB::jsonElement x = { {"name", "value"}};
std::string value = x ["name"];
```
An object is declared by creating an array containing nested arrays of two-elements of name-value pairs.
You can have multiple name/value pairs in an object.

```c++
DAB::jsonElmement x={{"status",                200},
                {"language",              "en-US"},
                {"outputResolution",      {{"width", 3840}, {"height", 2160}, {"frequency", 60}}},
                {"memc",                  false},
                {"cec",                   true},
                {"lowLatencyMode",        true},
                {"matchContentFrameRate", "EnabledSeamlessOnly"},
                {"hdrOutputMode",         "AlwaysHdr"},
                {"pictureMode",           "Other"},
                {"audioOutputMode",       "Auto"},
                {"audioOutputSource",     "HDMI"},
                {"videoInputSource",      "Other"},
                {"audioVolume",           20},
                {"mute",                  false},
                {"textToSpeech",          true}};
```

The jsonElment class also supports the has ( <"name"> ) method.   This can be used to detect if a jsonElement object has a specified name associated with it.
By convention, a reference is returned to the value, so simply accessing a value in an object that does not contain that named value will create the name/value pair and return a reference to the value.

This then allows you to programmatically create json objects.

```c++
DAB::jsonElment x;
x["name1"] = "value 1";
x["name2"] = "value 2";
```

#### arrays
```c++
DAB::jsonElement x = { 1, 2, 3, 4, 5 };
int64_t y = x[2];
```
Arrays are declared and accessed in a fashion to std::vector but with restricted functionality.

A special case, however exists if, in the case of an array of length two with the first element being a string.  By convention, the library will assume all length-2 arrays with the first element being a string to represent a jsonObject and not an array.

In order to force the library to interpret such an initializer as an array, it is necessary to pass a special value as part of the initializer;

```c++
DAB::jsonElment x = { DAB::jsonElement::array, "name", "value" };  // this will be interpreted as an array of length two and not as an object
```




