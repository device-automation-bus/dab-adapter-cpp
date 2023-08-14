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

#include "dabClient.h"
#include <cassert>

namespace DAB
{
	// typelist should be a list of types inheriting from dabClient (which itself inherits from dabInterface which is the base class we're interested in)
	template<typename ... C>
	class dabBridge {
        std::map<std::string, std::unique_ptr<dabInterface>, std::less<>> instances;

        // typelist for our metaprogram below
        template<class ...>
        struct types {
        };

    public:

        virtual ~dabBridge() = default;

        virtual jsonElement dispatch( jsonElement const &json ) {
            if (json.has("topic")) {
                std::string const &topic = json["topic"];
                if (topic.starts_with("dab/")) {
                    auto slashPos = std::string_view(topic.begin() + 4, topic.end()).find_first_of('/');
                    if (slashPos == std::string::npos) {
                        throw DAB::dabException ( 400, "topic is malformed" );
                    }

                    // the deviceId is extracted from "dab/<deviceId>/<method>"
                    auto deviceId = std::string_view(topic.begin() + 4, topic.begin() + 4 + (int)slashPos);
                    auto it = instances.find(deviceId);
                    if (it != instances.end()) {
                        // now call the dabInterface associated with the deviceId;
                        return it->second->dispatch(json);
                    } else {
                        throw DAB::dabException ( 400, "deviceId does not exist" );
                    }
                } else {
                    throw DAB::dabException ( 400, "topic is malformed" );
                }
            } else {
                throw DAB::dabException ( 400, "no topic found" );
            }
        }

        std::vector<std::string> getTopics() {
            std::vector<std::string> topics;

            topics.reserve(instances.size());
            for (auto const &instance: instances) {
                auto newTopics = instance.second->getTopics();
                topics.insert ( topics.end(), newTopics.begin(), newTopics.end() );
            }
            return topics;
        }

        template<typename F>
        void setPublishCallback(F f)
        {
            for ( auto &it : instances )
            {
                it.second->setPublishCallback( f );
            }
        }
        // makeInstance will instantiate a dabInterface object.  There can be multiple specialized classes, so each one must have a name attached to the class that can be used to specify which one.
        // this is done using a fixed_string template paramter and a helper function getName() to retrieve the value of that parameter.   This is then used in a standard recursive tempalate
        // to select the proper type to instantiate

        template <typename ...VS>
        dabInterface *makeDeviceInstance ( char const *deviceId, VS  &&...vs )
        {
            return makeInstances<0> ( deviceId, types<C...>{}, std::forward<VS>(vs)... );
        }
		private:

        template <typename FIRST, typename ...VS>
        FIRST &getFirstParameter ( FIRST &&first, VS &&... ) {
            return first;
        }
		// this is a recursive template that eats away one of our template type parameters at a time (HEAD).   It calls GetName() on HEAD (static method) which in turn calls the fixed_string type paramter which stors the string literal passed to it
		// as a type parameter.   This is used as the "name" of the class and therefore we can use this name to select which of our specialized types to instantiate
		template<int dummy, class HEAD, class ... Tail, class ...VS>
		dabInterface *makeInstances ( char const *deviceId, types<HEAD, Tail...>, VS &&...vs )
		{
            if ( sizeof... ( VS ) ) {
                // check the name of type HEAD and see if it's the one we want to instantiate
                if ( HEAD::isCompatible ( getFirstParameter(std::forward<VS>(vs)... ) ) ) {
                    // it is, so instantiate HEAD and save a unique pointer to it in our map.  The key is the UUID
                    instances.insert(std::move(std::make_pair(std::move(std::string(deviceId)), std::move(std::make_unique<HEAD>(deviceId, std::forward<VS>(vs)...)))));
                    return instances.find(std::string_view(deviceId))->second.get();
                } else {
                    return makeInstances<dummy>(deviceId, types<Tail...>{}, std::forward<VS>(vs)...);
                }
            } else {
                instances.insert(std::move(std::make_pair(std::move(std::string(deviceId)), std::move(std::make_unique<HEAD>(deviceId, std::forward<VS>(vs)...)))));
                return instances.find(std::string_view(deviceId))->second.get();
            }
		}

		// we need dummy here otherwise, once HEAD and ...TAIL are exhausted, the template argument list is <> which becomes an invalid specialization.   So we just need SOMETHING there, in this case a dummy int
		template< int , typename ...VS >
		dabInterface *makeInstances ( char const *, types<>, VS &&...vs ) {
            // if we ever got here, then we never found the proper class name to instantiate
            throw DAB::dabException ( 400, "class not found" );
        }
	};
};