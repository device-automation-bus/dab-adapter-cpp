/**
 Copyright 2023 Collabora Ltd
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

#include <fstream>
#include <random>
#include <ranges>
#include <unordered_set>
#include <curl/curl.h>

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace DAB
{
namespace RDK
{
    template <typename CType, typename Deleter>
    auto make_c_unique (CType * p, Deleter d) -> std::unique_ptr<CType, decltype (d)>
    {
        return { p, d };
    }

    struct rdkException: dabException
    {
        jsonElement errorReply;

        rdkException ( int64_t errorCode, std::string errorText, jsonElement errorReply = jsonElement ()):
            dabException ( errorCode, std::move ( errorText ) ),
            errorReply ( std::move ( errorReply ) ) {}
    };

    class UploadServer
    {
        static inline const uint16_t PORT = 7878;

        boost::asio::io_context ctx;
        boost::asio::ip::tcp::acceptor acceptor;
        const std::string targetPath;
    public:
        UploadServer ( const boost::asio::ip::address &local_address, const std::string &targetPath ):
            acceptor ( ctx, {local_address, PORT} ), targetPath ( targetPath ) {}

        std::string url ()
        {
            return std::format ( "http://{}:{}/{}", acceptor.local_endpoint ().address ().to_string (), PORT, targetPath );
        }

        std::vector<uint8_t> receive ()
        {
            namespace beast = boost::beast;

            boost::asio::ip::tcp::socket socket{acceptor.get_executor()};
            acceptor.accept (socket);

            beast::http::request<beast::http::vector_body<decltype ( UploadServer::receive () )::value_type>> req;

            beast::error_code ec;
            beast::flat_buffer buffer;

            beast::http::read ( socket, buffer, req, ec );

            if ( !ec &&
                 req.method () == beast::http::verb::post &&
                 req[beast::http::field::content_type] == "image/png" &&
                 req.target () == '/' + targetPath )
            {
                beast::http::response<beast::http::empty_body> rsp {beast::http::status::ok, req.version()};
                beast::write ( socket, beast::http::message_generator ( std::move ( rsp ) ), ec );
            } else
            {
                dabException e ( 400, "Invalid request received" );

                beast::http::response<beast::http::string_body> msg{beast::http::status::bad_request, req.version ()};
                msg.body () = e.errorText;
                beast::write ( socket, beast::http::message_generator ( std::move ( msg ) ), ec );

                throw e;
            }

            return req.body ();
        }
    };

    class DownloadClient
    {
        std::unique_ptr<CURL, std::function<void (CURL *)>> curl;
    public:
        DownloadClient ( const std::string& url ): curl ( make_c_unique ( curl_easy_init (), curl_easy_cleanup ) )
        {
            curl_easy_setopt ( curl.get (), CURLOPT_URL, url.c_str () );
        }

        typedef std::function<void(uint8_t *, size_t)> ReceiveCb;

        void receive ( ReceiveCb cb )
        {
            curl_easy_setopt ( curl.get (), CURLOPT_WRITEDATA, cb );
            curl_easy_setopt ( curl.get (), CURLOPT_WRITEFUNCTION, +[] ( uint8_t *data, size_t size, size_t nmemb, ReceiveCb cb ) -> size_t {
                size_t realsize = size * nmemb;

                cb ( data, realsize );

                return realsize;
            } );

            CURLcode res = curl_easy_perform ( curl.get() );
            if ( res != CURLE_OK )
            {
                throw dabException ( 500, std::format ( "HTTP request failed: {}", static_cast<int> ( res ) ) );
            }
        }
    };

    class Interface
    {
        boost::asio::io_context ctx;
        boost::beast::tcp_stream stream;

        std::string http_post ( const std::string &string )
        {
            namespace beast = boost::beast;

            beast::http::request<beast::http::string_body> req;
            req.method ( beast::http::verb::post );
            req.target ( "/jsonrpc" );
            req.set ( beast::http::field::content_type, "application/json" );
            req.body () = string;
            req.prepare_payload ();

            beast::http::write ( stream, req );

            beast::http::response<beast::http::string_body> res;
            beast::flat_buffer buffer;
            beast::http::read ( stream, buffer, res );

            return res.body ();
        }

        class ServiceBase
        {
        protected:
            Interface &rdk;

            ServiceBase (Interface &rdk): rdk ( rdk ) {}
        };

        std::map<std::string, std::unique_ptr<ServiceBase>> services;

        template<class S>
        struct Service: public ServiceBase
        {
            Service (Interface &rdk): ServiceBase ( rdk ) {}

            void activate ()
            {
                rdk.s<Controller>().activate (S::callsign);
            }

        protected:
            jsonElement request ( const std::string &method, const jsonElement &params = jsonElement () )
            {
                return rdk.request (S::callsign + '.' + method, params);
            }
        };

    public:
        Interface (const std::string &ipAddress): stream ( ctx )
        {
            stream.connect(boost::beast::tcp_stream::endpoint_type (
                boost::asio::ip::address::from_string ( ipAddress ), 9998 ) );
        }

        ~Interface ()
        {
            boost::beast::error_code ec;
            stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        }

        boost::asio::ip::address localAddress ()
        {
            return std::move ( stream.socket ().local_endpoint ().address () );
        }

        jsonElement request ( const std::string &method, const jsonElement &params = jsonElement ())
        {
            static unsigned request_id = 0;

            jsonElement request {
                { "jsonrpc", "2.0" },
                { "id", request_id++ },
                { "method", method }
            };

            if ( !params.isNull() )
            {
                request["params"] = params;
            }

            std::string buf;
            request.serialize ( buf, true );

            auto reply = jsonParser ( http_post ( buf ).c_str () );

            if ( reply.has ( "error" ) )
            {
                throw rdkException ( 500, std::format ( "RDK method {} failed", method ), reply["error"] );
            } else if ( reply["result"].has ("success") && !reply["result"]["success"] )
            {
                throw rdkException ( 500, std::format ( "RDK method {} finished without success", method ));
            }

            return reply["result"];
        }

        bool hasService ( const std::string &callsign )
        {
            try {
                s<Controller>().status ( callsign );
            } catch ( rdkException &e )
            {
                if (std::string ( e.errorReply["message"] ) == "ERROR_UNKNOWN_KEY") {
                    return false;
                } else {
                    throw;
                }
            }

            return true;
        }

        struct Controller: public Service<Controller>
        {
            static inline const std::string callsign = "Controller.1";

            void activate () {}

            void activate (const std::string &callsign)
            {
                request ( "activate", {
                    {"callsign", callsign }
                } );
            }

            jsonElement status (const std::string &callsign)
            {
                return request ( "status@" + callsign );
            }
        };

        struct System: public Service<System>
        {
            static inline const std::string callsign = "org.rdk.System";

            jsonElement getDeviceInfo ()
            {
                return request ( "getDeviceInfo" );
            }

            jsonElement reboot ( const std::string &rebootReason )
            {
                return request ( "reboot", {
                    { "rebootReason", rebootReason }
                } );
            }
        };

        struct RDKShell: public Service<RDKShell>
        {
            static inline const std::string callsign = "org.rdk.RDKShell";

            jsonElement getAvailableTypes ()
            {
                return request ( "getAvailableTypes" );
            }

            jsonElement getState ()
            {
                return request ( "getState" );
            }

            jsonElement launch ( const jsonElement &params )
            {
                return request ( "launch", params );
            }

            jsonElement setFocus ( const std::string &callsign )
            {
                return request ( "launch", {
                    { "client", callsign },
                    { "callsign", callsign },
                } );
            }

            jsonElement suspend ( const std::string &callsign )
            {
                return request ( "suspend", {
                    { "callsign", callsign },
                } );
            }

            jsonElement destroy ( const std::string &callsign )
            {
                return request ( "destroy", {
                    { "callsign", callsign }
                } );
            }

            jsonElement getScreenResolution ()
            {
                return request ( "getScreenResolution" );
            }

            jsonElement injectKey ( uint16_t keyCode )
            {
                return request ( "injectKey", {
                    { "keyCode", keyCode }
                } );
            }
        };

        struct DeviceInfo: public Service<DeviceInfo>
        {
            static inline const std::string callsign = "DeviceInfo";

            jsonElement systeminfo ()
            {
                return request ( "systeminfo" );
            }
        };


        struct DeviceIdentification: public Service<DeviceIdentification>
        {
            static inline const std::string callsign = "DeviceIdentification";

            jsonElement deviceidentification ()
            {
                return request ( "deviceidentification" );
            }
        };

        struct DisplaySettings: public Service<DisplaySettings>
        {
            static inline const std::string callsign = "org.rdk.DisplaySettings";

            jsonElement getConnectedVideoDisplays ()
            {
                return request ( "getConnectedVideoDisplays" );
            }

            jsonElement getSupportedResolutions ()
            {
                return request ( "getSupportedResolutions" );
            }

            jsonElement getSupportedAudioPorts ()
            {
                return request ( "getSupportedAudioPorts" );
            }

            jsonElement getConnectedAudioPorts ()
            {
                return request ( "getConnectedAudioPorts" );
            }

            jsonElement getMuted ( const std::string &audioPort )
            {
                return request ( "getMuted", {
                    {"audioPort", audioPort}
                });
            }

            jsonElement setMuted ( const std::string &audioPort, bool muted )
            {
                return request ( "setMuted", {
                    {"audioPort", audioPort},
                    {"muted", muted}
                });
            }

            jsonElement getSupportedAudioModes ( const std::string &audioPort )
            {
                return request ( "getSupportedAudioModes", {
                    {"audioPort", audioPort}
                });
            }

            jsonElement getSoundMode ( const std::string &audioPort )
            {
                return request ( "getSoundMode", {
                    {"audioPort", audioPort}
                });
            }

            jsonElement setSoundMode ( const std::string &audioPort, const std::string &soundMode )
            {
                return request ( "setSoundMode", {
                    {"audioPort", audioPort},
                    {"soundMode", soundMode},
                });
            }

            jsonElement getVolumeLevel ( const std::string &audioPort )
            {
                return request ( "getVolumeLevel", {
                    {"audioPort", audioPort}
                });
            }

            jsonElement setVolumeLevel ( const std::string &audioPort, uint32_t volume )
            {
                return request ( "setVolumeLevel", {
                    {"audioPort", audioPort},
                    {"volumeLevel", volume}
                });
            }

            jsonElement getSettopHDRSupport ()
            {
                return request ( "getSettopHDRSupport" );
            }

            jsonElement getTvHDRSupport ()
            {
                return request ( "getTvHDRSupport" );
            }

            jsonElement setCurrentResolution (const std::string &videoDisplay, const std::string &resolution, bool persist = true, bool ignoreEdid = true)
            {
                return request ( "setCurrentResolution", {
                    {"videoDisplay", videoDisplay},
                    {"resolution", resolution},
                    {"persist", persist},
                    {"ignoreEdid", ignoreEdid}
                } );
            }

            jsonElement setEnableAudioPort ( const std::string &audioPort )
            {
                return request ( "setEnableAudioPort", {
                    {"audioPort", audioPort}
                } );
            }

            jsonElement setForceHDRMode (bool hdrMode)
            {
                return request ( "setForceHDRMode", {
                    {"hdr_mode", hdrMode},
                } );
            }
        };

        struct FrameRate: public Service<FrameRate>
        {
            static inline const std::string callsign = "org.rdk.FrameRate";

            jsonElement getDisplayFrameRate ()
            {
                return request ( "getDisplayFrameRate" );
            }
        };

        struct Network: public Service<Network>
        {
            static inline const std::string callsign = "org.rdk.Network";

            jsonElement getInterfaces ()
            {
                return request ( "getInterfaces" );
            }

            jsonElement getIPSettings ( const std::string &interface )
            {
                return request ( "getIPSettings", {
                    {"interface", interface}
                } );
            }
        };

        struct HdmiCec: public Service<HdmiCec>
        {
            static inline const std::string callsign = "org.rdk.HdmiCec_2";

            jsonElement getEnabled ()
            {
                return request ( "getEnabled" );
            }

            jsonElement setEnabled ( bool enabled )
            {
                return request ( "setEnabled" , {
                    {"enabled", enabled}
                } );
            }
        };

        struct UserPreferences: public Service<UserPreferences>
        {
            static inline const std::string callsign = "org.rdk.UserPreferences.1";

            jsonElement getUILanguage ()
            {
                return request ( "getUILanguage" );
            }

            jsonElement setUILanguage ( const std::string &language )
            {
                return request ( "setUILanguage", {
                    {"ui_language", language}
                } );
            }
        };

        struct TextToSpeech: public Service<TextToSpeech>
        {
            static inline const std::string callsign = "org.rdk.TextToSpeech";

            jsonElement isttsenabled ()
            {
                return request ( "isttsenabled" );
            }

            jsonElement enabletts ( bool enabletts )
            {
                return request ( "enabletts", {
                    {"enabletts", enabletts}
                } );
            }
        };

        struct VoiceControl: public Service<VoiceControl>
        {
            static inline const std::string callsign = "org.rdk.VoiceControl";

            jsonElement voiceStatus ()
            {
                return request ( "voiceStatus" );
            }

            jsonElement configureVoice ( bool enable, bool enablePtt )
            {
                return request ( "configureVoice", {
                    {"enable", enable},
                    {"ptt", {
                        {"enable", enablePtt}
                    }}
                } );
            }

            jsonElement voiceSessionRequest ( const std::optional<std::string> audioFile,
                const std::optional<std::string> transcription, const std::string &type )
            {
                jsonElement params {
                    {"type", type}
                };

                if ( audioFile.has_value () )
                {
                    params["audio_file"] = audioFile.value ();
                }

                if ( transcription.has_value() )
                {
                    params["transcription"] = transcription.value ();
                }

                return request ( "voiceSessionRequest", params );
            }
        };

        struct ScreenCapture: public Service<ScreenCapture>
        {
            static inline const std::string callsign = "org.rdk.ScreenCapture";

            jsonElement uploadScreenCapture ( const std::string &url, const std::optional<std::string> callGUID = {})
            {
                jsonElement params {
                    {"url", url},
                };

                if ( callGUID.has_value () )
                {
                    params["callGUID"] = callGUID.value ();
                }

                return request ( "uploadScreenCapture", params );
            }
        };

        template <class Service>
        Service & s()
        {
            if (!services.contains ( Service::callsign ))
            {
                services[Service::callsign] = std::make_unique<Service>( *this );
                static_cast <Service *> (services[Service::callsign].get ())->activate ();
            }

            return *static_cast <Service *> (services[Service::callsign].get ());
        }
    };

    class Adapter : public DAB::dabClient<Adapter>
    {
        typedef Interface RDK;

        Interface rdk;

    public:
        Adapter ( const std::string &deviceId, const std::string &ipAddress ) :
            dabClient ( deviceId, ipAddress ), rdk ( ipAddress ), RDK_KEYMAP ( createKeymap ( "/opt/dab_platform_keymap.json" ) )
        {
        }

        static bool isCompatible ( char const *ipAddress )
        {
            try {
                Interface rdk( ipAddress );

                auto deviceInfo = rdk.s<RDK::System>().getDeviceInfo ();

                return deviceInfo.has ( "success" ) && deviceInfo["success"];
            } catch ( ... )
            {
                return false;
            }
        }

        jsonElement appList ()
        {
            jsonElement types = rdk.s<RDK::RDKShell>().getAvailableTypes ()["types"];

            jsonElement rsp;
            rsp["applications"].makeArray ();

            for (auto it = types.cbeginArray (); it != types.cendArray (); ++it)
            {
                rsp["applications"].push_back ( jsonElement ( "appId", *it ) );
            }

            return rsp;
        }

        jsonElement appLaunch ( const std::string  &appId, const DAB::jsonElement &parameters )
        {
            const std::string state = appGetState ( appId )["state"];

            jsonElement params { "callsign", appId };

            if ( state == "STOPPED" )
            {
                if ( COBALT_APP_IDS.contains ( appId ) )
                {
                    params["type"] = "Cobalt";
                    params["configuration"] = jsonElement ( { "url", buildYoutubeUrl ( parameters ) } );
                }

                rdk.s<RDK::RDKShell>().launch ( params );
            } else
            {
                rdk.request ( appId + ".1.deeplink", jsonElement ( buildYoutubeUrl ( parameters ) ) );
                rdk.s<RDK::RDKShell>().setFocus ( appId );
            }

            if ( state == "BACKGROUND" )
            {
                rdk.s<RDK::RDKShell>().launch ( params );
                rdk.s<RDK::RDKShell>().setFocus ( appId );
            }

            waitForAppState ( appId, "FOREGROUND" );

            return {};
        }

        jsonElement appLaunchWithContent ( const std::string &appId, const std::string &contentId, const jsonElement &parameters )
        {
            if ( !COBALT_APP_IDS.contains ( appId ) )
            {
                throw std::pair ( 500, "This operator currently only supports Youtube" );
            }

            jsonElement params ( parameters );
            if ( !contentId.empty() )
            {
                params.push_back ("v=" + contentId);
            }

            return appLaunch (appId, params);
        }

        jsonElement appGetState ( const std::string &appId )
        {
            jsonElement state = rdk.s<RDK::RDKShell>().getState ()["state"];

            bool appCreated = false;
            bool isSuspended = false;

            jsonElement response;

            for ( auto it = state.cbeginArray (); it != state.cendArray (); ++it )
            {
                const jsonElement &app = *it;
                if ( std::string ( app["callsign"] ) == appId )
                {
                    response["state"] = ( std::string ( app["state"] ) == "suspended" ) ?
                            "BACKGROUND" : "FOREGROUND";
                    break;
                }
            }

            if ( !response.has ( "state" ) )
            {
                response [ "state" ] = "STOPPED";
            }

            return response;
        }

        jsonElement appExit ( std::string const &appId, bool background )
        {
            jsonElement state = appGetState ( appId );

            if ( std::string ( state["state"] ) != "STOPPED" )
            {
                std::string targetState;

                if ( background )
                {
                    rdk.s<RDK::RDKShell>().suspend ( appId );
                    targetState = "BACKGROUND";
                } else
                {
                    rdk.s<RDK::RDKShell>().destroy ( appId );
                    targetState = "STOPPED";
                }

                state = waitForAppState ( appId, targetState );
            }

            return state;
        }

        jsonElement deviceInfo ()
        {
            using namespace std::chrono;

            const jsonElement deviceInfo = rdk.s<RDK::System>().getDeviceInfo ();
            const jsonElement screenResolution = rdk.s<RDK::RDKShell>().getScreenResolution ();
            const jsonElement systeminfo = rdk.s<RDK::DeviceInfo>().systeminfo ();
            const jsonElement deviceIdentification = rdk.s<RDK::DeviceIdentification>().deviceidentification ();

            std::string firstDisplay;
            const jsonElement displays = rdk.s<RDK::DisplaySettings>().getConnectedVideoDisplays ();
            if ( displays.has ( "connectedVideoDisplays" ) ) {
                firstDisplay = std::string ( displays["connectedVideoDisplays"][0] );
            }

            const jsonElement rdkIfaces = rdk.s<RDK::Network>().getInterfaces ()["interfaces"];
            jsonElement networkInterfaces;
            networkInterfaces.makeArray ();
            for ( auto it = rdkIfaces.cbeginArray (); it != rdkIfaces.cendArray (); ++it )
            {
                const jsonElement &rdkIf = *it;
                jsonElement dabIf;

                const std::string &type = rdkIf["interface"];
                if (type == "ETHERNET")
                {
                    dabIf["type"] = "Ethernet";
                } else if (type == "WIFI")
                {
                    dabIf["type"] = "Wifi";
                } else
                {
                    dabIf["type"] = "Other";
                }

                dabIf["connected"] = rdkIf["connected"];
                dabIf["macAddress"] = rdkIf["macAddress"];

                if ( dabIf["connected"].isBool() && dabIf["connected"] )
                {
                    const jsonElement &ipSettings = rdk.s<RDK::Network>().getIPSettings (type);

                    dabIf["ipAddress"] = ipSettings["ipaddr"];

                    for ( auto dnsparam: { "primarydns", "secondarydns" } )
                    {
                        const std::string &dns = ipSettings[dnsparam];
                        if ( !dns.empty () )
                        {
                            dabIf["dns"].push_back (dns);
                        }
                    }
                }

                networkInterfaces.push_back ( dabIf );
            }

            return {
                {"networkInterfaces", networkInterfaces},
                {"serialNumber", std::string ( systeminfo["serialnumber"] )},
                {"uptimeSince", ( duration_cast<seconds>(system_clock::now().time_since_epoch()).count() - int64_t ( systeminfo["uptime"] ) ) * 1000},
                {"manufacturer", std::string ( deviceInfo["make"] )},
                {"firmwareVersion", std::string ( deviceInfo["imageRevision"] )},
                {"firmwareBuild", std::string ( deviceInfo["imageVersion"] )},
                {"model", std::string ( deviceInfo["model_number"] )},
                {"chipset", std::string ( deviceIdentification["chipset"] )},
                {"screenWidthPixels", int64_t ( screenResolution["w"] )},
                {"screenHeightPixels", int64_t ( screenResolution["h"] )},
                {"displayType", firstDisplay.starts_with ( "HDMI" ) ? "External" : "Native"},
                {"deviceId", deviceId},
            };
        }

        jsonElement systemRestart ()
        {
            rdk.s<RDK::System>().reboot ( "DAB_REBOOT_REQUEST" );

            return {};
        }

        jsonElement systemSettingsList ()
        {
            return SystemSettingsDispatcher ( rdk ).list ();
        }

        jsonElement systemSettingsGet ()
        {
            return SystemSettingsDispatcher ( rdk ).getAll ();
        }

        jsonElement systemSettingsSet ( jsonElement const &request )
        {
            jsonElement reply;
            SystemSettingsDispatcher dispatcher ( rdk );

            for ( auto it = request.cbeginObject (); it != request.cendObject (); ++it )
            {
                reply[it->first] = dispatcher.set (it->first, it->second);
            }

            return reply;
        }

        jsonElement inputKeyList ()
        {
            auto keycodes = std::views::keys ( RDK_KEYMAP );
            return { "keyCodes", std::vector<std::string> { keycodes.begin (), keycodes.end () } };
        }

        jsonElement inputKeyPress ( std::string keyCode )
        {
            auto key = RDK_KEYMAP.find ( keyCode );
            if ( key == RDK_KEYMAP.end () )
            {
                throw std::pair ( 400, std::format ( "key code {} not found", keyCode ) );
            }

            rdk.s<RDK::RDKShell>().injectKey ( key->second );

            return {};
        }

        jsonElement inputKeyLongPress ( std::string keyCode, int64_t durationMs )
        {
            using namespace std::chrono;

            auto key = RDK_KEYMAP.find ( keyCode );
            if ( key == RDK_KEYMAP.end () )
            {
                throw std::pair ( 400, std::format ( "key code {} not found", keyCode ) );
            }

            static const milliseconds REQUEST_INTERVAL{50};
            const milliseconds duration{durationMs};
            milliseconds elapsedTime{0};

            while ( elapsedTime <  duration )
            {
                const auto start = steady_clock::now();

                rdk.s<RDK::RDKShell>().injectKey ( key->second );

                steady_clock::duration request_duration = steady_clock::now() - start;
                if ( request_duration < REQUEST_INTERVAL )
                {
                    std::this_thread::sleep_for( REQUEST_INTERVAL - request_duration );
                    request_duration = steady_clock::now() - start;
                }

                elapsedTime += duration_cast<milliseconds> ( request_duration );
            }

            return {};
        }

        jsonElement outputImage ()
        {
            static auto generateGUID = std::bind (
                std::uniform_int_distribution<uint32_t> ( 0, std::numeric_limits<uint32_t>::max () ),
                std::default_random_engine ( std::random_device{} () )
            );

            std::string guid = std::to_string ( generateGUID () );

            UploadServer server ( rdk.localAddress (), guid );

            rdk.s<RDK::ScreenCapture>().uploadScreenCapture ( server.url (), guid );

            auto imgData = server.receive ();

            using namespace boost::archive::iterators;
            using b64it = base64_from_binary<transform_width<std::vector<uint8_t>::const_iterator, 6, 8>>;

            std::string outputImage ( b64it ( imgData.cbegin () ), b64it ( imgData.cend () ) );
            outputImage.append ( -outputImage.size () % 4, '=' );

            return {
                {"outputImage", "data:image/png;base64," + outputImage}
            };
        }

        /*DAB::jsonElement deviceTelemetry ()
        {
            // example exception
            throw DAB::dabException{501, "unsupported"};
        }*/

        /*DAB::jsonElement appTelemetry ( std::string const &appId )
        {
            // example return
            return { "app-status:", std::string ( "all systems nominal for " ) + appId };
        }*/

        jsonElement healthCheckGet ()
        {
            return {
                {"healthy", true}
            };
        }

        jsonElement voiceList ()
        {
            jsonElement voiceSystems;
            voiceSystems.makeArray ();

            const jsonElement &voiceStatus = rdk.s<RDK::VoiceControl>().voiceStatus ();

            // Current Alexa solution is PTT & starts with protocol 'avs://'
            if ( std::string ( voiceStatus["urlPtt"] ).starts_with ( "avs://" ) )
            {
                voiceSystems.push_back ( {
                    { "name", "AmazonAlexa" },
                    { "enabled", std::string ( voiceStatus["ptt"]["status"] ) == "ready" }
                } );
            }

            return {
                {"voiceSystems", voiceSystems}
            };
        }

        jsonElement voiceSet ( jsonElement const &voiceSystem )
        {
            const std::string &voiceSystemName = voiceSystem["name"];

            if ( !getVoiceSystemState ( voiceSystemName ).has_value () )
            {
                throw std::pair ( 400, std::format ( "Unsupported voice system '{}'", voiceSystemName ) );
            }

            if ( voiceSystemName == "AmazonAlexa" )
            {
                bool enable = voiceSystem["enabled"];

                rdk.s<RDK::VoiceControl>().configureVoice ( enable, enable );

                return {
                    {"voiceSystem", getVoiceSystemState ( voiceSystemName ).value ()}
                };
            } else
            {
                throw std::pair ( 400, std::format ( "Can't configure voice system '{}'", voiceSystemName ) );
            }
        }

        jsonElement voiceSendAudio ( std::string const &fileLocation, std::string const &voiceSystem )
        {
            char voiceCommandFilename[] = "/tmp/dabvoicecommandXXXXXX";
            int voiceCommandFd = mkstemp ( voiceCommandFilename );

            DownloadClient ( fileLocation ).receive ( [voiceCommandFd] ( const uint8_t *data, size_t size ) {
                if ( write ( voiceCommandFd, data, size ) == -1 )
                {
                    throw std::pair ( 500, "Error writing to a file." );
                };
            } );

            rdk.s<RDK::VoiceControl>().voiceSessionRequest ( voiceCommandFilename, {}, "ptt_audio_file" );

            close (voiceCommandFd);
            std::remove ( voiceCommandFilename );

            return {};
        }

        jsonElement voiceSendText ( std::string const &requestText, std::string const &voiceSystem )
        {
            if ( voiceSystem != "AmazonAlexa" )
            {
                throw std::pair ( 400, std::format ( "Unsupported voice system '{}'", voiceSystem ) );
            }

            rdk.s<RDK::VoiceControl>().voiceSessionRequest ( {}, requestText, "ptt_transcription" );

            return {};
        }

    private:
        static const inline std::unordered_set<std::string> COBALT_APP_IDS { "Cobalt", "Youtube", "YouTube" };

        static std::string buildYoutubeUrl (const jsonElement &parameters)
        {
            std::string url = "https://www.youtube.com/tv?";

            if ( !parameters.isNull () )
            {
                for ( auto it = parameters.cbeginArray (); it != parameters.cendArray (); ++it )
                {
                    auto param = make_c_unique ( curl_easy_unescape ( nullptr, std::string ( *it ).c_str (), 0, nullptr ), curl_free );

                    if (it != parameters.cbeginArray ())
                    {
                        url += '&';
                    }
                    url += param.get();
                }
            }

            return url;
        }

        jsonElement waitForAppState ( const std::string &appId, const std::string &targetState )
        {
            // Wait at most 2 seconds for the application to change state.
            for ( unsigned i = 0; i != 20; ++i )
            {
                jsonElement state = appGetState ( appId );
                if ( std::string ( state["state"] ) == targetState )
                {
                    return state;
                }
                std::this_thread::sleep_for( std::chrono::milliseconds ( 100 ) );
            }

            throw std::pair ( 400, "Timeout waiting for application state change." );
        }

        struct SystemSettingsDispatcher
        {
            SystemSettingsDispatcher (Interface &rdk): rdk (rdk) {}

            jsonElement getAll ()
            {
                jsonElement result;

                for (const auto setting: SETTINGS)
                {
                    result[setting.first] = setting.second.get ();
                }

                return result;
            }

            jsonElement set (const std::string &name, const jsonElement &value)
            {
                auto setting = SETTINGS.find (name);

                if ( setting == SETTINGS.end()) {
                    throw std::pair (400, std::format ( "Unknown setting {}", name ) );
                }

                try {
                    try {
                        setting->second.set ( value );
                    } catch (rdkException &e)
                    {
                        std::string buf;
                        if ( !e.errorReply.isNull () )
                        {
                            e.errorReply.serialize ( buf, false );
                        }

                        std::cerr << "rdkException " << e.errorCode << ' ' << e.errorText << ' ' << buf << '\n';

                        throw;
                    }
                } catch ( std::bad_function_call& ) {
                    throw std::pair ( 400, std::format ( "Setting {} is not settable", name ) );
                } catch ( ... )
                {
                    std::string buf;

                    value.serialize ( buf, false );

                    throw std::pair ( 400, std::format ( "Setting {} does not support value {}", name, buf ) );
                }

                return setting->second.get ();
            }

            jsonElement list ()
            {
                jsonElement result;

                for (const auto setting: SETTINGS)
                {
                    result[setting.first] = setting.second.list ();
                }

                return result;
            }
        private:
            Interface &rdk;

            struct SystemSetting
            {
                std::function<jsonElement()> get;
                std::function<void(const jsonElement&)> set;
                std::function<jsonElement()> list;
            };

            const std::map<std::string, SystemSetting> SETTINGS {
                {"language", {
                    [this] ()
                    {
                        return rdk.s<RDK::UserPreferences>().getUILanguage ()["ui_language"];
                    },
                    [this] (const jsonElement &val)
                    {
                        rdk.s<RDK::UserPreferences>().setUILanguage ( val );
                    },
                    [this] ()
                    {
                        return jsonElement {DAB::jsonElement::array, "en-US"};
                    }
                }},
                {"outputResolution", {
                    [this] ()
                    {
                        using std::ranges::views::split;
                        using std::ranges::views::transform;

                        std::string framerate = rdk.s<RDK::FrameRate>().getDisplayFrameRate ()["framerate"];
                        if (framerate.ends_with(']'))
                        {
                            framerate.pop_back();
                        }

                        auto range = framerate | split('x') | transform ([](auto &&i) {
                            unsigned val;
                            std::from_chars( i.data(), i.data() + i.size(), val );
                            return val;
                        });

                        std::vector<unsigned> v(range.begin(), range.end());

                        return jsonElement {
                            {"width", v[0]},
                            {"height", v[1]},
                            {"frequency", v[2]},
                        };
                    },
                    [this] (const jsonElement &val)
                    {
                        std::string firstDisplay;
                        const jsonElement displays = rdk.s<RDK::DisplaySettings>().getConnectedVideoDisplays ();
                        if ( !displays.has ( "connectedVideoDisplays" ) || displays["connectedVideoDisplays"].size() == 0) {
                            throw std::pair ( 400, "Device doesn't have any connected video port" );
                        }

                        static const std::map<std::pair<uint32_t, uint32_t>, std::string_view> RESOLUTION_MAP = {
                            {{640, 480}, "480"},
                            {{720, 576}, "576"},
                            {{1280, 720}, "720"},
                            {{1920, 1080}, "1080"},
                            {{3840, 2160}, "2160"},
                        };

                        auto res = RESOLUTION_MAP.find ({int64_t ( val["width"] ), int64_t (val ["height"])});
                        if (res == RESOLUTION_MAP.end())
                        {
                            throw std::pair ( 500, "Unsupported video format" );
                        }

                        rdk.s<RDK::DisplaySettings>().setCurrentResolution (
                            std::string ( displays["connectedVideoDisplays"][0] ),
                            std::format ("{}p{}", res->second, int64_t ( val["frequency"] ))
                        );
                    },
                    [this] ()
                    {
                        jsonElement rdkResolutions = rdk.s<RDK::DisplaySettings>().getSupportedResolutions ()["supportedResolutions"];
                        jsonElement resolutions;
                        for ( auto it = rdkResolutions.cbeginArray (); it != rdkResolutions.cendArray (); ++it )
                        {
                            const std::string & res = *it;

                            const std::string resolution = res.substr (0, res.find ('p'));
                            const std::string framerateStr = res.substr (res.find ('p') + 1);

                            static const std::map<std::string, std::pair<int32_t, int32_t>> RDK_RESOLUTION_MAP {
                                    { "480", { 640, 480 } },
                                    { "576", { 720, 576 } },
                                    { "720", { 1280, 720 } },
                                    { "1080", { 1920, 1080 } },
                                    { "2160", { 3840, 2160 } },
                            };

                            if (auto it = RDK_RESOLUTION_MAP.find ( resolution ); it != std::end ( RDK_RESOLUTION_MAP ) )
                            {
                                int32_t framerate;

                                if (std::from_chars (framerateStr.c_str (), framerateStr.c_str () + framerateStr.size(), framerate).ec == std::errc{})
                                {
                                    resolutions.push_back ( {
                                        {"width", it->second.first },
                                        {"height", it->second.second },
                                        {"frequency", framerate },
                                    } );
                                }
                            }
                        }

                        return resolutions;
                    }
                }},
                {"audioVolume", {
                    [this] ()
                    {
                        std::string volumeLevel = rdk.s<RDK::DisplaySettings>().getVolumeLevel (rdkAudioPort ())["volumeLevel"];
                        unsigned audioVolume;
                        if ( std::from_chars ( volumeLevel.data(), volumeLevel.data() + volumeLevel.size(), audioVolume ).ec != std::errc{} )
                        {
                            throw std::pair ( 500, "Unable to parse volume level" );
                        }

                        return audioVolume;
                    },
                    [this] (const jsonElement &val)
                    {
                        rdk.s<RDK::DisplaySettings>().setVolumeLevel ( rdkAudioPort (), int64_t (val) );
                    },
                    [this] ()
                    {
                        return jsonElement {{"min", 0}, {"max", 100}};
                    }
                }},
                {"mute", {
                    [this] ()
                    {
                        return rdk.s<RDK::DisplaySettings>().getMuted (rdkAudioPort ())["muted"];
                    },
                    [this] (const jsonElement &val)
                    {
                        rdk.s<RDK::DisplaySettings>().setMuted ( rdkAudioPort (), val );
                    },
                    [this] ()
                    {
                        return true;
                    }
                }},
                {"cec", {
                    [this] ()
                    {
                        return rdk.s<RDK::HdmiCec>().getEnabled ()["enabled"];
                    },
                    [this] (const jsonElement &val)
                    {
                        rdk.s<RDK::HdmiCec>().setEnabled ( val );
                    },
                    [this] ()
                    {
                        return rdk.hasService ( RDK::TextToSpeech::callsign );
                    }
                }},
                {"memc", {
                    [this] ()
                    {
                        return false;
                    },
                    nullptr,
                    [this] ()
                    {
                        return false;
                    }
                }},
                {"lowLatencyMode", {
                    [this] ()
                    {
                        return false;
                    },
                    nullptr,
                    [this] ()
                    {
                        return false;
                    }
                }},
                {"matchContentFrameRate", {
                    [this] ()
                    {
                        return "EnabledAlways";
                    },
                    nullptr,
                    [this] ()
                    {
                        return jsonElement {DAB::jsonElement::array, "EnabledAlways"};
                    }
                }},
                {"hdrOutputMode", {
                    [this] ()
                    {
                        return ( rdk.s<RDK::DisplaySettings>().getSettopHDRSupport ()["supportsHDR"] &&
                            rdk.s<RDK::DisplaySettings>().getTvHDRSupport ()["supportsHDR"] ) ? "AlwaysHdr" : "DisableHdr";
                    },
                    [this] (const jsonElement &val)
                    {
                        bool hdrMode;

                        const std::string &mode = val;
                        if ( mode == "AlwaysHdr" )
                        {
                            hdrMode = true;
                        } else if ( mode == "DisableHdr" )
                        {
                            hdrMode = false;
                        } else {
                            throw std::pair ( 400, std::format ( "Mode {} is not supported", mode ) );
                        }

                        rdk.s<RDK::DisplaySettings>().setForceHDRMode ( hdrMode );
                    },
                    [this] ()
                    {
                        std::unordered_set<std::string> hdrOutputModes = { "DisableHdr" };
                        if ( rdk.s<RDK::DisplaySettings>().getSettopHDRSupport ()["supportsHDR"] &&
                             rdk.s<RDK::DisplaySettings>().getTvHDRSupport ()["supportsHDR"])
                        {
                            hdrOutputModes.insert ( "AlwaysHdr" );
                        }

                        return hdrOutputModes;
                    }
                }},
                {"pictureMode", {
                    [this] ()
                    {
                        return "Standard";
                    },
                    nullptr,
                    [this] ()
                    {
                        return jsonElement {DAB::jsonElement::array, "Standard"};
                    }
                }},
                {"audioOutputMode", {
                    [this] ()
                    {
                        const std::string soundMode = rdk.s<RDK::DisplaySettings>().getSoundMode ( rdkAudioPort () )["soundMode"];
                        const auto audioOutputMode = rdkSoundModeToDab ( soundMode );
                        if ( !audioOutputMode.has_value () )
                        {
                            throw std::pair ( 500, "Unknown RDK sound mode " + soundMode );
                        }

                        return audioOutputMode.value ();
                    },
                    [this] (const jsonElement &val)
                    {
                        std::string rdkMode;
                        const std::string &dabMode = val;
                        if ( dabMode == "Stereo" )
                        {
                            rdkMode = "STEREO";
                        } else if ( dabMode == "PassThrough" )
                        {
                            rdkMode = "PASSTHRU";
                        } else if ( dabMode == "Auto" )
                        {
                            rdkMode = "AUTO";
                        } else if ( dabMode == "MultichannelPcm" )
                        {
                            jsonElement supportedAudioModes = rdk.s<RDK::DisplaySettings>().getSupportedAudioModes ( rdkAudioPort () )["supportedAudioModes"];
                            for (auto it = supportedAudioModes.cbeginArray (); it != supportedAudioModes.cendArray (); ++it)
                            {
                                const std::string &mode = *it;
                                if ( RDK_MULTICHANNEL_MODES.contains ( mode ) )
                                {
                                    rdkMode = mode;
                                    break;
                                }
                            }

                            if ( rdkMode.empty () )
                            {
                                throw std::pair ( 400, "Audio port doesn't support multichannel." );
                            }
                        } else
                        {
                            throw std::pair ( 400, std::format( "Unsupported output mode {}", rdkMode ) );
                        }

                        rdk.s<RDK::DisplaySettings>().setSoundMode ( rdkAudioPort (), rdkMode );
                    },
                    [this] ()
                    {
                        jsonElement supportedAudioModes = rdk.s<RDK::DisplaySettings>().getSupportedAudioModes ( rdkAudioPort () )["supportedAudioModes"];
                        std::unordered_set<std::string> audioOutputModes;
                        for (auto it = supportedAudioModes.cbeginArray (); it != supportedAudioModes.cendArray(); ++it)
                        {
                            const std::string &mode = *it;

                            if (auto dabSoundMode = rdkSoundModeToDab (mode); dabSoundMode.has_value ())
                            {
                                audioOutputModes.insert ( dabSoundMode.value () );
                            }
                        }

                        return audioOutputModes;
                    }
                }},
                {"audioOutputSource", {
                    [this] ()
                    {
                        auto audioPort = rdkAudioPort ();
                        auto audioOutputSource = rdkAudioPortToDab ( audioPort );
                        if ( !audioOutputSource.has_value() )
                        {
                            throw std::pair ( 500, "Unknown RDK port name " + audioPort);
                        }

                        return audioOutputSource.value ();
                    },
                    [this] (const jsonElement &val)
                    {
                        const std::string &outputSource = val;
                        auto audioPort = rdkAudioPortFromDab ( outputSource );
                        if ( !audioPort.has_value() )
                        {
                            throw std::pair ( 500, std::format ( "Unsupported output source {}", outputSource ) );
                        }

                        rdk.s<RDK::DisplaySettings>().setEnableAudioPort ( audioPort.value () );

                        _rdkAudioPort.reset ();
                    },
                    [this] ()
                    {
                        jsonElement supportedAudioPorts = rdk.s<RDK::DisplaySettings>().getSupportedAudioPorts ()["supportedAudioPorts"];
                        std::unordered_set<std::string> audioOutputSources;
                        for (auto it = supportedAudioPorts.cbeginArray(); it != supportedAudioPorts.cendArray(); ++it)
                        {
                            if ( auto opt = rdkAudioPortToDab ( *it ); opt.has_value () )
                            {
                                audioOutputSources.insert ( opt.value () );
                            }
                        }

                        return audioOutputSources;
                    }
                }},
                {"videoInputSource", {
                    [this] ()
                    {
                        return "Home";
                    },
                    nullptr,
                    [this] ()
                    {
                        return jsonElement {DAB::jsonElement::array, "Home"};
                    }
                }},
                {"textToSpeech", {
                    [this] ()
                    {
                        return rdk.s<RDK::TextToSpeech>().isttsenabled ()["isenabled"];
                    },
                    [this] (const jsonElement &val)
                    {
                        return rdk.s<RDK::TextToSpeech>().enabletts( val );
                    },
                    [this] ()
                    {
                        return rdk.hasService ( RDK::TextToSpeech::callsign );
                    }
                }},
            };

            std::optional<std::string> _rdkAudioPort;

            const std::string &rdkAudioPort ()
            {
                if ( !_rdkAudioPort.has_value () )
                {
                    _rdkAudioPort = rdk.s<RDK::DisplaySettings>().getConnectedAudioPorts ()["connectedAudioPorts"][0];
                }

                return _rdkAudioPort.value ();
            }

            static inline const std::unordered_set<std::string> RDK_MULTICHANNEL_MODES {
                "SURROUND", "DOLBYDIGITAL", "DOLBYDIGITALPLUS"
            };

            static const std::optional<std::string> rdkSoundModeToDab ( const std::string &mode )
            {
                if ( mode == "STEREO" )
                {
                    return "Stereo";
                } else if ( RDK_MULTICHANNEL_MODES.contains ( mode ) )
                {
                    return "MultichannelPcm";
                } else if ( mode == "PASSTHRU" )
                {
                    return "PassThrough";
                } else if ( mode.starts_with ( "AUTO" ) )
                {
                    return "Auto";
                } else {
                    return {};
                }
            }

            static const std::optional<std::string> rdkAudioPortToDab ( const std::string &port )
            {
                static const std::map<std::string, std::string> RDK_TO_DAB {
                    { "SPDIF0", "Optical" },
                    { "HDMI0", "HDMI" },
                };

                if ( auto it = RDK_TO_DAB.find ( port ); it != RDK_TO_DAB.end () )
                {
                    return it->second;
                }

                return {};
            }

            static const std::optional<std::string> rdkAudioPortFromDab ( const std::string &port )
            {
                static const std::map<std::string, std::string> DAB_TO_RDK {
                    { "Optical", "SPDIF0" },
                    { "HDMI", "HDMI0" },
                };

                if ( auto it = DAB_TO_RDK.find ( port ); it != DAB_TO_RDK.end () )
                {
                    return it->second;
                }

                return {};
            }
        };

        const std::map<std::string, uint16_t> RDK_KEYMAP;

        decltype (RDK_KEYMAP) createKeymap (const char * platformKeymapFile)
        {
            static decltype (RDK_KEYMAP) DEFAULT_KEYMAP {
                {"KEY_POWER", 116},
                {"KEY_HOME", 36},
                {"KEY_VOLUME_UP", 175},
                {"KEY_VOLUME_DOWN", 174},
                {"KEY_MUTE", 173},
                {"KEY_EXIT", 27},
                {"KEY_UP", 38},
                {"KEY_PAGE_UP", 33},
                {"KEY_PAGE_DOWN", 34},
                {"KEY_RIGHT", 39},
                {"KEY_DOWN", 40},
                {"KEY_LEFT", 37},
                {"KEY_ENTER", 13},
                {"KEY_BACK", 8},
                {"KEY_PLAY", 179},
                {"KEY_PLAY_PAUSE", 179},
                {"KEY_PAUSE", 179},
                {"KEY_STOP", 178},
                {"KEY_REWIND", 227},
                {"KEY_FAST_FORWARD", 228},
                {"KEY_SKIP_REWIND", 177},
                {"KEY_SKIP_FAST_FORWARD", 176},
                {"KEY_0", 48},
                {"KEY_1", 49},
                {"KEY_2", 50},
                {"KEY_3", 51},
                {"KEY_4", 52},
                {"KEY_5", 53},
                {"KEY_6", 54},
                {"KEY_7", 55},
                {"KEY_8", 56},
                {"KEY_9", 57},
            };

            std::remove_const_t<decltype (RDK_KEYMAP)> keymap;

            // Platform specific keymap file in the format:
            // {
            //     "KEY_CHANNEL_UP":104,
            //     "KEY_CHANNEL_DOWN":109,
            //     "KEY_MENU":408
            // }
            std::ifstream ifs ( platformKeymapFile );
            if ( ifs.rdstate() & std::ifstream::failbit )
            {
                std::cerr << "Could not open platform keymap " << platformKeymapFile << '\n';
            } else {
                std::stringstream stream;
                stream << ifs.rdbuf ();

                try {
                    jsonElement json = jsonParser ( stream.str ().c_str () );

                    if ( json.isObject () )
                    {
                        for (auto it = json.cbeginObject (); it != json.cendObject (); ++it)
                        {
                            if ( !it->second.isInteger () ) {
                                throw 0;
                            }

                            keymap[it->first] = int64_t ( it->second );
                        }
                    }
                } catch (...)
                {
                    keymap.clear ();
                    std::cerr << "Unable to read platform keymap " << platformKeymapFile << '\n';
                }
            }

            keymap.insert ( DEFAULT_KEYMAP.begin (), DEFAULT_KEYMAP.end () );

            return keymap;
        }

        std::optional<jsonElement> getVoiceSystemState ( const std::string &name )
        {
            const jsonElement voiceSystems = voiceList ()["voiceSystems"];

            for ( auto it = voiceSystems.cbeginArray (); it != voiceSystems.cendArray (); ++it )
            {
                if ( name == std::string ( ( *it )["name"] ) )
                {
                    return *it;
                }
            }

            return {};
        }
    };
}
}
