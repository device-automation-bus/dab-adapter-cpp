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

 #pragma once

#include <iostream>
#include <string>
#include <chrono>
#include <exception>
#include <condition_variable>
#include <mutex>

#include "dabBridge.h"
#include "MQTTClient.h"

template <typename BRIDGE>
class dabMQTTInterface {
    const std::string CLIENT_ID;

    constexpr static auto PERIOD = std::chrono::seconds(5);

    const int MAX_BUFFERED_MSGS = 120;	// 120 * 5sec => 10min off-line buffering

    MQTTClient client{};

    BRIDGE  &bridge;

    std::condition_variable     running;
    std::mutex                  runningMutex;

    static int messageArrived(void *context, char *topic, int, MQTTClient_message *message)
    {
        auto *mqttInterface = reinterpret_cast<dabMQTTInterface *>(context);

        auto &bridge = mqttInterface->bridge;
        try
        {
            std::string reqStr ((char const *) message->payload, message->payloadlen );
            char const *reqStrPtr = reqStr.c_str();
            jsonElement req = jsonElement ( &reqStrPtr );
            req["topic"] = topic;
            jsonElement rsp = bridge.dispatch ( req );

            MQTTClient_message clientMessage = MQTTClient_message_initializer;

            std::string payload;

            rsp.serialize ( payload, true );

            clientMessage.payload = const_cast<char *>(payload.c_str ());
            clientMessage.payloadlen = (int) payload.size ();
            clientMessage.qos = 0;
            clientMessage.retained = 0;

            std::lock_guard l1 ( mqttInterface->runningMutex );
            if ( auto rc = MQTTClient_publishMessage ( mqttInterface->client, rsp["topic"].operator const std::string & ().c_str (), &clientMessage, nullptr ))
            {
                throw DAB::dabException ( rc, "error publishing message" );
            }
        } catch ( DAB::dabException &e )
        {
            std::cout << "error (" << e.errorCode << "): " << e.errorText << std::endl;
        } catch ( ... )
        {
        }
        return 1;
    }

    void publishCB ( jsonElement const &elem )
    {
        MQTTClient_message clientMessage = MQTTClient_message_initializer;

        std::string payload;

        elem.serialize ( payload, true );

        clientMessage.payload = const_cast<char *>(payload.c_str() );
        clientMessage.payloadlen = (int)payload.size();
        clientMessage.qos = 0;
        clientMessage.retained = 0;

        std::lock_guard l1 ( runningMutex );
        if ( auto rc = MQTTClient_publishMessage( client, elem["topic"].operator const std::string &().c_str(), &clientMessage, nullptr ))
        {
            throw DAB::dabException ( rc, "error publishing message" );
        }
    }

    static void connectionLost(void *context, char *)
    {
        auto *mqttInterface = reinterpret_cast<dabMQTTInterface *>(context);
        std::lock_guard l1 ( mqttInterface->runningMutex );
        mqttInterface->running.notify_all();
    }

public:

    dabMQTTInterface ( BRIDGE &bridge, std::string const &brokerAddress ) : bridge ( bridge )
    {
        if ( auto rc = MQTTClient_create(&client, brokerAddress.c_str(), "dab", MQTTCLIENT_PERSISTENCE_NONE, nullptr) )
        {
            throw DAB::dabException ( rc, std::string ( "Failed to create client" ) );
        }

        if ( auto rc = MQTTClient_setCallbacks(client, this, connectionLost, messageArrived, nullptr) )
        {
            throw DAB::dabException ( rc, std::string ( "Failed to set callbacks" ) );
        }
        bridge.setPublishCallback ( std::function ( [this](jsonElement const &elem){ return publishCB ( elem );} ) );
    }

    ~dabMQTTInterface()
    {
        MQTTClient_destroy( &client );
    }

    auto connect() {
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;

        if ( auto rc = MQTTClient_connect(client, &conn_opts) )
        {
            throw DAB::dabException ( rc, std::string ( "Failed to set connect" ) );
        }

        auto topics = bridge.getTopics ();

        for ( auto const &topic : topics )
        {
            if ( auto rc = MQTTClient_subscribe(client, topic.c_str(), 1) )
            {
                throw DAB::dabException ( rc, std::string ( "Failed to subscribe" ) );
            }
        }

        return 0;
    }

    auto disconnect()
    {
        if ( auto rc = MQTTClient_disconnect(client, 10000) )
        {
            throw DAB::dabException ( rc, std::string ( "Failed to disconnect" ) );
        }
        std::lock_guard l1 ( runningMutex );
        running.notify_all();
        return 0;
    }

    void wait()
    {
        std::unique_lock l1 ( runningMutex );
        running.wait( l1);
        return;
    }
};

