#ifndef XNET_HEADER
#define XNET_HEADER

#include "../XCLib/src/networking.h"

#include <future>
#include <functional>
#include <vector>
#include <unordered_set>
#include <string>
#include <string_view>
#include <array>
#include <algorithm>
#include <cassert>
#include <memory>
#include <cstring>

namespace xnet
{
    enum class severity
    {
        error,
        warning,
        info,
        trace,
    };
    class logmessage
    {
    public:
        virtual std::string format() const = 0;
        virtual int code() const = 0;
        virtual xnet::severity severity() const = 0;
    };
    namespace messages
    {
        class logerror : public logmessage { public: virtual xnet::severity severity() const override { return severity::error; } };
        class logwarning : public logmessage { public: virtual xnet::severity severity() const override { return severity::warning; } };
        class loginfo : public logmessage { public: virtual xnet::severity severity() const override { return severity::info; } };
        class logtrace : public logmessage { public: virtual xnet::severity severity() const override { return severity::trace; } };

        namespace errors
        {
            class failed_to_create_server : public logerror
            {
                const int log_code = -1;
            public:
                virtual std::string format() const override
                {
                    return "Failed to create socket for server to listen on.";
                }
                virtual int code() const override { return log_code; }
            };
            class failed_to_bind_server : public logerror
            {
                const int log_code = -3;
                int m_port;
            public:
                failed_to_bind_server(int port) : m_port(port) {}
                virtual std::string format() const override
                {
                    return "Failed to bind socket onto port.";
                }
                virtual int code() const override { return log_code; }
            };
            class failed_to_start_listening : public logerror
            {
                const int log_code = -3;
            public:
                virtual std::string format() const override
                {
                    return "Failed to start listening.";
                }
                virtual int code() const override { return log_code; }
            };
            class failed_to_listen_for_incomming_connections : public logerror
            {
                const int log_code = -3;
            public:
                virtual std::string format() const override
                {
                    return "Failed to listen for incomming connections.";
                }
                virtual int code() const override { return log_code; }
            };
        }
    }
    class server;
    class client
    {
        friend class server;
        SOCKET socket;
        SOCKETADDR address;
        bool is_open;
    public:

        // Attempts to read up to buffersize.
        // Will block until data is available for reading.
        // Returns the bytes read
        template<size_t buffersize = 1024>
        int read(std::array<char, buffersize>& buffer) const
        {
            auto res = recv(socket, buffer.data(), buffersize, 0);
            if (res == SOCKET_ERROR)
            {
                return -1;
            }
            return res;
        }
        // Attempts to read up to buffersize.
        // Will block until data is available for reading.
        // Returns the bytes read
        int read(char* buffer, size_t len) const
        {
            auto res = recv(socket, buffer, len, 0);
            if (res == SOCKET_ERROR)
            {
                return -1;
            }
            return res;
        }

        // Attempts to write the data to the client
        template<size_t buffersize = 1024>
        void write(const std::array<char, buffersize>& data) const
        {
            send(socket, data.data(), buffersize, 0);
        }
        // Attempts to write the data to the client
        void write(const char* data, size_t len) const
        {
            send(socket, data, len, 0);
        }
        bool is_closed() const { return !is_open; }
        void close()
        {
            is_open = false;
            networking_close(socket);
        }
    };
    client& operator<<(client& os, const std::string& str)
    {
        os.write(str.data(), str.length());
        return os;
    }
    client& operator<<(client& os, const std::string_view& str)
    {
        os.write(str.data(), str.length());
        return os;
    }
    client& operator<<(client& os, const char* str)
    {
        os.write(str, std::strlen(str));
        return os;
    }

    class server
    {
    private:
        std::thread* mp_listen_thread;
        bool m_stop_listen;
        std::promise<void> m_listen_promise;
        unsigned short m_port;
        int m_max_connections;
    private:
        static void thread_listen(server& srv)
        {
            SOCKET socket;
            if (networking_create_server(&socket))
            {
                srv.log(messages::errors::failed_to_create_server{});
                goto end;
            }
            if (networking_server_bind(&socket, srv.m_port))
            {
                srv.log(messages::errors::failed_to_bind_server{ srv.m_port });
                networking_close(socket);
                goto end;
            }
            if (networking_server_listen(&socket, srv.m_max_connections))
            {
                networking_close(socket);
                srv.log(messages::errors::failed_to_create_server{});
                goto end;
            }

            while (!srv.m_stop_listen)
            {
                xnet::client client;
                if (networking_server_accept_block(&socket, &client.socket, &client.address))
                {
                    networking_close(socket);
                    srv.log(messages::errors::failed_to_listen_for_incomming_connections{});
                    goto end;
                }
                client.is_open = true;
                srv.client_connected(client);
            }

            networking_close(socket);
        end:
            srv.m_stop_listen = false;
            delete srv.mp_listen_thread;
            srv.m_listen_promise.set_value();
        }
    protected:
        virtual void log(xnet::logmessage&& msg) const = 0;
        virtual void client_connected(xnet::client& client) const = 0;
    public:
        server(unsigned short port, int max_connections) :
            mp_listen_thread(nullptr),
            m_stop_listen(false),
            m_port(port),
            m_max_connections(max_connections)
        { }
        ~server() { if (mp_listen_thread) { stop(); mp_listen_thread->join(); delete mp_listen_thread; } }

        // Blocking Call that listens until the server is asked to be shut-down
        void listen()
        {
            listen_async().wait();
        }

        // Starts listening until the server is asked to be shut-down.
        std::future<void> listen_async()
        {
            if (mp_listen_thread)
            {
                return m_listen_promise.get_future();
            }
            m_stop_listen = false;
            m_listen_promise = {};
            mp_listen_thread = new std::thread(thread_listen, std::ref(*this));
            mp_listen_thread->detach();
            return m_listen_promise.get_future();
        }
        bool is_listening() const { return mp_listen_thread != nullptr; }
        // If server is listening, it stops immediate
        void stop()
        {
            if (mp_listen_thread)
            {
                m_stop_listen = true;
            }
        }
    };
    namespace http
    {
        struct header
        {
            std::string name;
            std::string value;

            header() : name({}), value({}) {}
            header(std::string name) : name(name), value({}) {}
            header(std::string name, std::string value) : name(name), value(value) {}

            bool operator==(const header& other) const
            {
                return std::equal(name.begin(), name.end(), other.name.begin(), other.name.end(), [](auto l, auto r) { return std::toupper(l) == std::toupper(r); });
            }
            bool operator!=(const header& other) const { return !(*this == other); }
        };
        struct header_name_hash { std::size_t operator() (const header& l) const { return std::hash<std::string>()(l.name); } };
        using headerset = std::unordered_set<header, header_name_hash>;
        enum class operation
        {
            unknown,
            GET,
            POST,
            PUT
        };
        inline operation operation_parse(std::string_view view)
        {
            if (view == "GET")
            {
                return operation::GET;
            }
            else if (view == "POST")
            {
                return operation::GET;
            }
            else if (view == "PUT")
            {
                return operation::GET;
            }
            else
            {
                return operation::unknown;
            }
        }
        enum class rescode
        {
            Continue = 100, // Section 6.2.1
            SwitchingProtocols = 101, // Section 6.2.2
            OK = 200, // Section 6.3.1
            Created = 201, // Section 6.3.2
            Accepted = 202, // Section 6.3.3
            NonAuthoritativeInformation = 203, // Section 6.3.4
            NoContent = 204, // Section 6.3.5
            ResetContent = 205, // Section 6.3.6
            PartialContent = 206, // Section 4.1 of [RFC7233]
            MultipleChoices = 300, // Section 6.4.1
            MovedPermanently = 301, // Section 6.4.2
            Found = 302, // Section 6.4.3
            SeeOther = 303, // Section 6.4.4
            NotModified = 304, // Section 4.1 of [RFC7232]
            UseProxy = 305, // Section 6.4.5
            TemporaryRedirect = 307, // Section 6.4.7
            BadRequest = 400, // Section 6.5.1
            Unauthorized = 401, // Section 3.1 of [RFC7235]
            PaymentRequired = 402, // Section 6.5.2
            Forbidden = 403, // Section 6.5.3
            NotFound = 404, // Section 6.5.4
            MethodNotAllowed = 405, // Section 6.5.5
            NotAcceptable = 406, // Section 6.5.6
            ProxyAuthenticationRequired = 407, // Section 3.2 of [RFC7235]
            RequestTimeout = 408, // Section 6.5.7
            Conflict = 409, // Section 6.5.8
            Gone = 410, // Section 6.5.9
            LengthRequired = 411, // Section 6.5.10
            PreconditionFailed = 412, // Section 4.2 of [RFC7232]
            PayloadTooLarge = 413, // Section 6.5.11
            URITooLong = 414, // Section 6.5.12
            UnsupportedMediaType = 415, // Section 6.5.13
            RangeNotSatisfiable = 416, // Section 4.4 of [RFC7233]
            ExpectationFailed = 417, // Section 6.5.14
            UpgradeRequired = 426, // Section 6.5.15
            InternalServerError = 500, // Section 6.6.1
            NotImplemented = 501, // Section 6.6.2
            BadGateway = 502, // Section 6.6.3
            ServiceUnavailable = 503, // Section 6.6.4
            GatewayTimeout = 504, // Section 6.6.5
            HTTPVersionNotSupported = 505  // Section 6.6.6     
        };
        std::string_view to_string(rescode code)
        {
            using namespace std::string_view_literals;
            switch (code)
            {
            case ((rescode)100): return "Continue"sv;
            case ((rescode)101): return "Switching Protocols"sv;
            case ((rescode)200): return "OK"sv;
            case ((rescode)201): return "Created"sv;
            case ((rescode)202): return "Accepted"sv;
            case ((rescode)203): return "Non-Authoritative Information"sv;
            case ((rescode)204): return "No Content"sv;
            case ((rescode)205): return "Reset Content"sv;
            case ((rescode)206): return "Partial Content"sv;
            case ((rescode)300): return "Multiple Choices"sv;
            case ((rescode)301): return "Moved Permanently"sv;
            case ((rescode)302): return "Found"sv;
            case ((rescode)303): return "See Other"sv;
            case ((rescode)304): return "Not Modified"sv;
            case ((rescode)305): return "Use Proxy"sv;
            case ((rescode)307): return "Temporary Redirect"sv;
            case ((rescode)400): return "Bad Request"sv;
            case ((rescode)401): return "Unauthorized"sv;
            case ((rescode)402): return "Payment Required"sv;
            case ((rescode)403): return "Forbidden"sv;
            case ((rescode)404): return "Not Found"sv;
            case ((rescode)405): return "Method Not Allowed"sv;
            case ((rescode)406): return "Not Acceptable"sv;
            case ((rescode)407): return "Proxy Authentication Required"sv;
            case ((rescode)408): return "Request Timeout"sv;
            case ((rescode)409): return "Conflict"sv;
            case ((rescode)410): return "Gone"sv;
            case ((rescode)411): return "Length Required"sv;
            case ((rescode)412): return "Precondition Failed"sv;
            case ((rescode)413): return "Payload Too Large"sv;
            case ((rescode)414): return "URI Too Long"sv;
            case ((rescode)415): return "Unsupported Media Type"sv;
            case ((rescode)416): return "Range Not Satisfiable"sv;
            case ((rescode)417): return "Expectation Failed"sv;
            case ((rescode)426): return "Upgrade Required"sv;
            case ((rescode)500): return "Internal Server Error"sv;
            case ((rescode)501): return "Not Implemented"sv;
            case ((rescode)502): return "Bad Gateway"sv;
            case ((rescode)503): return "Service Unavailable"sv;
            case ((rescode)504): return "Gateway Timeout"sv;
            case ((rescode)505): return "HTTP Version Not Supported"sv;
            default: return "UNKNOWN"sv;
            }
        }
        struct response
        {
            // Wether to close the connection to the client afterwards
            bool keep_alive;
            // The protocol version to use
            std::string protocol_version;
            // The data to send.
            // If keep_alive is set to true, this will be ignored.
            // Otherwise:
            // - Will override Contents-Length header.
            // - Will set response_code to OK if NoContent is provided.
            std::vector<char> contents;
            // The set of headers to send
            headerset headers;
            // The response code to use
            rescode response_code;

            response() : keep_alive(false), protocol_version("HTTP/1.1"), response_code(rescode::NoContent) {}
        };
        response& operator<<(response& os, const std::string& str) { os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, const std::string_view& str) { os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, const char* str) { os.contents.insert(os.contents.end(), str, str + std::strlen(str)); return os; }
        response& operator<<(response& os, uint8_t value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, uint16_t value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, uint32_t value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, uint64_t value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, int8_t value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, int16_t value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, int32_t value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, int64_t value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, float value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }
        response& operator<<(response& os, double value) { auto str = std::to_string(value); os.contents.insert(os.contents.end(), str.begin(), str.end()); return os; }

        class httpserver;
        class request
        {
        private:
            xnet::client client;
            bool response_sent;
            friend class httpserver;
        public:
            operation op;
            std::string path;
            std::string protocol_version;

            headerset headers;
            std::vector<char> contents;

            // Sends a response to the client
            void send_response(response& r)
            {
                if (response_sent) { return; }
                response_sent = true;


                if (!r.keep_alive)
                {
                    r.headers.insert({ "Content-Length", std::to_string(r.contents.size()) });
                    if (r.response_code == rescode::NoContent)
                    {
                        r.response_code = rescode::OK;
                    }
                }

                const char* nl = "\r\n";
                client << r.protocol_version << " " << std::to_string((int)r.response_code) << " " << to_string(r.response_code) << nl;
                for (auto h : r.headers)
                {
                    client << h.name << ": " << h.value << nl;
                }
                client << nl;
                if (!r.keep_alive)
                {
                    client.write(r.contents.data(), r.contents.size());
                    client.close();
                }
            }
        private:
            request() : client({}), response_sent(false), op(operation::unknown) {}
        };
        class httpserver : public server
        {
        private:
            std::function<void(xnet::logmessage&&)> m_log;
            std::function<void(xnet::http::request&)> m_handle_request;
            const char* delimiter = "\r\n";
        protected:
            virtual void log(xnet::logmessage&& msg) const override { m_log(std::move(msg)); }
            virtual void client_connected(xnet::client& client) const
            {
                const size_t buffsize = 1024;
                auto buff = std::make_unique<std::array<char, buffsize + 1>>(); // + 1 allowing to always allow safe peaking one ahead
                request req;
                req.client = client;
                // Implement mini-statemachine for reading in the request
                enum estate
                {
                    // Start State. Also indicates \r\n reached
                    EMPTY,
                    // Reads in the request head
                    HEAD_OP,
                    HEAD_PATH,
                    HEAD_VERSION,
                    // Indicates that we are currently reading a header
                    HEADER_NAME,
                    // Indicates that we are currently reading a header value
                    HEADER_VALUE,
                    HEADER_VALUE_WS_SKIP,
                    CONTENT,
                    // Parsing is done
                    DONE
                };
                estate state = HEAD_OP;
                char* off;
                header hed;
                while (state != DONE)
                {
                    int res;
                    res = client.read(buff->data(), buffsize);
                    if (res == 0)
                    {
                        goto exit;
                    }
                    off = buff->data();
                    for (char* it = buff->data(); it < buff->data() + res; ++it)
                    {
                        char c = *it;
                        char la = *(it + 1);
                        switch (state)
                        {
                        case EMPTY: {
                            if (c == '\r' && la == '\n')
                            {
                                auto content_length_header = req.headers.find({ "Content-Length" });
                                if (content_length_header == req.headers.end())
                                { // No content available as Content-Length header is missing
                                    goto exit;
                                }
                                state = CONTENT;
                                ++it;
                            }
                            else
                            {
                                state = HEADER_NAME;
                                off = it;
                            }
                        } break;
                        case HEAD_OP: {
                            if (c == '\r' && la == '\n')
                            {
                                req.op = operation_parse(std::string_view(off, it - off));
                                state = EMPTY;
                                ++it;
                                off = it + 1;
                            }
                            else if (c == ' ')
                            {
                                req.op = operation_parse(std::string_view(off, it - off));
                                state = HEAD_PATH;
                                off = it + 1;
                            }
                        } break;
                        case HEAD_PATH: {
                            if (c == '\r' && la == '\n')
                            {
                                req.path = std::string(off, it);
                                state = EMPTY;
                                ++it;
                                off = it + 1;
                            }
                            else if (c == ' ')
                            {
                                req.path = std::string(off, it);
                                state = HEAD_VERSION;
                                off = it + 1;
                            }
                        } break;
                        case HEAD_VERSION: {
                            if (c == '\r' && la == '\n')
                            {
                                req.protocol_version = std::string(off, it);
                                state = EMPTY;
                                ++it;
                                off = it + 1;
                            }
                        } break;
                        case HEADER_NAME: {
                            if (c == ':')
                            {
                                state = HEADER_VALUE_WS_SKIP;
                                hed.name = std::string(off, it);
                                off = it + 1;
                            }
                        } break;
                        case HEADER_VALUE_WS_SKIP:
                            if (c == ' ') { off = it + 1; break; }
                            else { state = HEADER_VALUE; }
                        case HEADER_VALUE: {
                            if (c == '\r' && la == '\n')
                            {
                                state = EMPTY;
                                hed.value = std::string(off, it);
                                req.headers.insert(hed);
                                ++it;
                            }
                        } break;
                        case CONTENT: {
                            auto content_length_header = req.headers.find({ "Content-Length" });
                            if (content_length_header == req.headers.end())
                            { // No content available as Content-Length header is missing
                                goto exit;
                            }
                            auto content_length = std::strtoull(content_length_header->value.c_str(), nullptr, 10);
                            req.contents.reserve(content_length);

                            // get remaining data
                            auto loc_rem = content_length - (res - (it - buff->data()));
                            content_length -= loc_rem;
                            // Insert all remaining data
                            req.contents.insert(req.contents.end(), it, it + loc_rem);
                            while (content_length > 0)
                            {
                                res = client.read(buff->data(), min((unsigned long long)buffsize, content_length));
                                content_length -= res;
                                req.contents.insert(req.contents.end(), buff->data(), buff->data() + res);
                            }

                            goto exit;
                        } break;
                        }
                    }
                }
            exit:
                m_handle_request(req);
                if (!req.response_sent)
                {
                    xnet::http::response r;
                    req.send_response(r);
                }
                return;
            }
        public:
            httpserver(unsigned short port, int max_connections, std::function<void(xnet::logmessage&&)> log, std::function<void(xnet::http::request&)> handle_request) : server(port, max_connections),
                m_log(log),
                m_handle_request(handle_request)
            { }


        };
    }
}

#endif