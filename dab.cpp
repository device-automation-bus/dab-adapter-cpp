/**
 Copyright 2023 Amazon.com, Inc. or its affiliates.
 Copyright 2023 Netflix Inc.
 Copyright 2023 Google LLC
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at
 http://www.apache.org/licenses/LICENSE-2.0
 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */


#include <iostream>
#include "dabMqttInterface.h"

// This is the main client class for the DAB protocol.   Methods in this class will be called upon receipt of a DAB message
// This class must inherit from DAB::dabCLient.   The template takes two parameters, the first one is the type of the class being created.
// this is a CRTP pattern and internally is used for detection of shadowed methods to detect implementation by the class.
// the second is a string literal "name" for the class.  This is used by the bridge function to allow for symbolic instantiation of the class
// using only a string literal to select among multiple implementations.
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

    jsonElement systemSettingsGet ()
    {
        // an example of how to return json data
        return {{"status",                200},
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
    }

    jsonElement appList ()
    {
        jsonElement rsp;

        rsp["applications"].makeArray ();           // rsp will be an object with "applications" : []
#ifdef _WIN32
        std::string output = execCmd ( "taskList /NH /Fo CSV" );

        for ( auto it = output.cbegin (); it != output.cend (); )
        {
            if ( *it == '"' )
            {
                it++;
                std::string taskName;
                while ( it != output.cend () && *it != '"' )
                {
                    taskName += *it;
                    it++;
                }
                if ( it != output.cend () )
                {
                    it++;
                }
                rsp["applications"].push_back ( taskName );     // push our task name to the end of the applications array
                while ( it != output.cend () && *it != '\n' )
                {
                    it++;
                }
                if ( it != output.cend () )
                {
                    it++;
                }
            } else
            {
                it++;
            }
        }
#else
#endif
        return rsp;
    }

    jsonElement appLaunchWithContent ( std::string const &appId, std::string const &contentId, jsonElement const &elem )
    {
        // example return
        return {{"status", 200},
                {"state",  "launched"}};
    }

    jsonElement deviceInfo ()
    {
        // example return
        return {{"status",  200},
                {"version", "2.0"}};
    }

    jsonElement deviceTelemetry ()
    {
        // example exception
        throw DAB::dabException{501, "unsupported"};
    }

    jsonElement appTelemetry ( std::string const &appId )
    {
        // example return
        return { "app-status:", "all systems nominal" };
    }

    // these are the prototypes for the currently unsupported operations
    // to start receiving callbacks simply uncomment out any of the handlers you wish to receive calls for
    // the library will detect the fact that there is now a handler defined, add it to the dab/<deviceid>/oplist response
    // and begin routing request automatically.  Nothing else needs be done.
#if 0
    jsonElement deviceInfo ()
    {
        throw std::pair ( 403, "not found" );
    }

    jsonElement appLaunch ( std::string const &appId, jsonElement const &elem )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement appLaunchWithContent ( std::string const &appId, std::string const &contentId, jsonElement const &elem )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement appGetState ( std::string const &appId )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement appExit ( std::string const &appId, bool force )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement systemRestart ()
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement systemSettingsList ()
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement systemSettingsSet ( jsonElement const &elem )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement inputKeyList ()
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement inputKeyPress ( std::string keyCode )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement inputKeyLongPress ( std::string keyCode, int64_t durationMs )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement outputImage ()
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement healthCheckGet ()
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement voiceList ()
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement voiceSet ( jsonelement const &voiceSystem )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement voiceSendAudio ( std::string const &fileLocation, std::string const &voiceSystem )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement voiceSendText ( std::string const &requestText, std::string const &voiceSystem )
    {
        throw std::pair ( 403, "not found" );
    }
    jsonElement discovery ()
    {
		throw std::pair ( 403, "not found" );
    }
#endif
};


int main ( int argc, char *argv[] )
{
    // dabBridge takes a list of class types
    DAB::dabBridge<dab_panel> bridge;

    if ( argc != 4 )
    {
        std::cout << "usage dab <mqtt broker> <deviceId> <ipAddress>" << std::endl;
        return 0;
    }

    // this instantiates a particular class based.  It takes <deviceId> as the first parameter and the <ipAddress>
    // of the device as the second parameter
    bridge.makeDeviceInstance ( argv[2], argv[3] );

    // this creates the mqtt interface.  It takes the bridge and the ip address of the mqtt broker
    auto mqtt = dabMQTTInterface ( bridge, argv[1] );

    // this connects the mqtt interface to the mqtt broker
    mqtt.connect ();

    // wait forever or until the connection with the broker is finished
    // a user can call mqtt.disconnect() to gracefully exit.
    mqtt.wait ();
}

