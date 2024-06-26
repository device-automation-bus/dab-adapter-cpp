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
#include "MQTTExportDeclarations.h"
#include "MQTTProperties.h"
#include "MQTTReasonCodes.h"
#include "MQTTSubscribeOpts.h"
#include "MQTTClientPersistence.h"


// thi is the main mqtt interface.   We utilize the paho-mqtt library for mqtt support.
// the template takes a dabBridge object as a parameter which is inferred from the first parameter of the constructor.
// the constructor takes the dabBridge and the ipAddress of the mqtt bridge.

namespace DAB
{
    template< typename BRIDGE >
    class dabMQTTInterface
    {
        const std::string CLIENT_ID;

        constexpr static auto PERIOD = std::chrono::seconds ( 5 );

        MQTTClient client{};

        BRIDGE &bridge;

        std::condition_variable running;
        std::mutex runningMutex;

        static std::string getResponseTopic ( MQTTClient_message *message )
        {
            if ( MQTTProperties_hasProperty ( &message->properties, MQTTPROPERTY_CODE_RESPONSE_TOPIC ) )
            {
                auto *property = MQTTProperties_getProperty ( &message->properties, MQTTPROPERTY_CODE_RESPONSE_TOPIC );

                auto res = std::string ( property->value.data.data, property->value.data.len );
                return res;
            }
            return "";
        }

        static bool hasCorrelationData ( MQTTClient_message *message )
        {
            return  MQTTProperties_hasProperty ( &message->properties, MQTTPROPERTY_CODE_CORRELATION_DATA );
        }

        static auto *getCorrelationData ( MQTTClient_message *message )
        {
            return MQTTProperties_getProperty ( &message->properties, MQTTPROPERTY_CODE_CORRELATION_DATA );
        }

        // this is the message arrived callback.   paho-mqtt uses a void parameter (thin wrapper around a C library).
        // it would have been nice if it was a template that took the calling object as a parameter so that we could maintain type safety.
        // the method takes the context and reinterprets it to the dabMQTTInterface object.
        static int messageArrived ( void *context, char *topic, int, MQTTClient_message *message )
        {
            auto *mqttInterface = reinterpret_cast<dabMQTTInterface *>(context);

            auto &bridge = mqttInterface->bridge;
            try
            {
                std::string reqStr ((char const *) message->payload, message->payloadlen );
                jsonElement req = jsonParser ( reqStr.c_str ());

                // the dispatcher requires the topic to be part of the DAB request.  Add it in.
                req["topic"] = topic;
                // we put the payload in its own "payload" value in the json object
                req["payload"] = jsonParser ( reqStr.c_str ());
                // this leaves us the capability of adding other properties into the top level
                // that might be needed by a potential handler. for instance topic is currently sent
                // but a handler might want responseTopic for logging purposes or correlation data
                // we currently don't send those, but you can do so by commenting out the below lines
                // req["responseTopic"] = getResponseTopic ( message );
                // req["correlationData"] = hasCorrelationData ( message ) ? getCorrelationData ( message ) : "";
                // dispatch to the bridge and start get the response
                jsonElement rsp = bridge.dispatch ( req );

                MQTTClient_message clientMessage = MQTTClient_message_initializer;

                std::string payload;

                // serialize the json response (convert from our internal jsonElement to a string)
                rsp.serialize ( payload, true );
                clientMessage.payload = const_cast<char *>(payload.c_str ());
                clientMessage.payloadlen = (int) payload.size ();
                clientMessage.qos = 0;
                clientMessage.retained = 0;

                if ( hasCorrelationData ( message ) )
                {
                    auto corr_data_req_prop = getCorrelationData ( message );
                    MQTTProperty corr_data_resp_prop;
                    corr_data_resp_prop.identifier = MQTTPROPERTY_CODE_CORRELATION_DATA;
                    corr_data_resp_prop.value.data.data = corr_data_req_prop->value.data.data;
                    corr_data_resp_prop.value.data.len = corr_data_req_prop->value.data.len;

                    int rc = MQTTProperties_add(&clientMessage.properties, &corr_data_resp_prop);
                }

                // get the mutex to serialize calls to the mqtt library
                std::lock_guard l1 ( mqttInterface->runningMutex );
                if ( auto rc = MQTTClient_publishMessage ( mqttInterface->client, getResponseTopic ( message ).c_str (), &clientMessage, nullptr ))
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

        // this is the publishing call-back that we pass to the bridge object (and subsequently to the dabClient).  It's used for notifications where we send telemetry responses without a request
        void publishCB ( jsonElement const &elem )
        {
            MQTTClient_message clientMessage = MQTTClient_message_initializer;

            std::string payload;

            elem["payload"].serialize ( payload, true );

            clientMessage.payload = const_cast<char *>(payload.c_str ());
            clientMessage.payloadlen = (int) payload.size ();
            clientMessage.qos = 0;
            clientMessage.retained = 0;

            std::lock_guard l1 ( runningMutex );
            if ( auto rc = MQTTClient_publishMessage ( client, elem["topic"].operator const std::string & ().c_str (), &clientMessage, nullptr ))
            {
                throw DAB::dabException ( rc, "error publishing message" );
            }
        }

        static void connectionLost ( void *context, char * )
        {
            auto *mqttInterface = reinterpret_cast<dabMQTTInterface *>(context);
            std::lock_guard l1 ( mqttInterface->runningMutex );
            mqttInterface->running.notify_all ();
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

        ~dabMQTTInterface ()
        {
            MQTTClient_destroy ( &client );
        }

        // this is the method to actually establish a connection with the mqtt broker.  At this point any initialization that needs to be done should have finished
        auto connect() {
            MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

            conn_opts.keepAliveInterval = 20;

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
        // this function should be called when the client wish's to cleanly end the mqtt interface in preparation for exiting.
        auto disconnect ()
        {
            if ( auto rc = MQTTClient_disconnect ( client, 10000 ))
            {
                throw DAB::dabException ( rc, std::string ( "Failed to disconnect" ));
            }
            std::lock_guard l1 ( runningMutex );
            running.notify_all ();
            return 0;
        }

        // this function will wait until the mqtt interface has been properly shut down, or errors due to connectivity loss.
        void wait ()
        {
            std::unique_lock l1 ( runningMutex );
            running.wait ( l1 );
            return;
        }
    };
};