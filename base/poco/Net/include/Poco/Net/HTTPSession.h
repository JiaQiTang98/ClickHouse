//
// HTTPSession.h
//
// Library: Net
// Package: HTTP
// Module:  HTTPSession
//
// Definition of the HTTPSession class.
//
// Copyright (c) 2005-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// SPDX-License-Identifier:	BSL-1.0
//


#ifndef Net_HTTPSession_INCLUDED
#define Net_HTTPSession_INCLUDED


#include <ios>
#include <memory>
#include <functional>
#include "Poco/Any.h"
#include "Poco/Buffer.h"
#include "Poco/Exception.h"
#include "Poco/Net/Net.h"
#include "Poco/Net/StreamSocket.h"
#include "Poco/Timespan.h"
#include "Poco/Net/Throttler.h"


namespace Poco
{
namespace Net
{


    class IHTTPSessionDataHooks
    /// Interface to control stream of data bytes being sent or received though socket by HTTPSession
    /// It allows to monitor, throttle and schedule data streams with syscall granulatrity
    {
    public:
        virtual ~IHTTPSessionDataHooks() = default;

        virtual void atStart(int bytes) = 0;
        /// Called before sending/receiving data `bytes` to/from socket.

        virtual void atFinish(int bytes) = 0;
        /// Called when sending/receiving of data `bytes` is successfully finished.

        virtual void atFail() = 0;
        /// If an error occurred during send/receive `fail()` is called instead of `finish()`.
    };


    using HTTPSessionDataHooksPtr = std::shared_ptr<IHTTPSessionDataHooks>;


    class Net_API HTTPSession
    /// HTTPSession implements basic HTTP session management
    /// for both HTTP clients and HTTP servers.
    ///
    /// HTTPSession implements buffering for HTTP connections, as well
    /// as specific support for the various HTTP stream classes.
    ///
    /// This class can not be instantiated. HTTPClientSession or
    /// HTTPServerSession must be used instead.
    {
    public:
        void setKeepAlive(bool keepAlive);
        /// Sets the keep-alive flag for this session.
        ///
        /// If the keep-alive flag is enabled, persistent
        /// HTTP/1.1 connections are supported.

        bool getKeepAlive() const;
        /// Returns the value of the keep-alive flag for
        /// this session.

        void setTimeout(const Poco::Timespan & timeout);
        /// Sets the timeout for the HTTP session.

        void
        setTimeout(const Poco::Timespan & connectionTimeout, const Poco::Timespan & sendTimeout, const Poco::Timespan & receiveTimeout);
        /// Sets different timeouts for the HTTP session.

        void setReceiveThrottler(const ThrottlerPtr & throttler = {});
        /// Sets the throttler that is used to limit the speed of data received through the socket.

        void setSendThrottler(const ThrottlerPtr & throttler = {});
        /// Sets the throttler that is used to limit the speed of data sent through the socket.

        Poco::Timespan getTimeout() const;
        /// Returns the timeout for the HTTP session.

        Poco::Timespan getConnectionTimeout() const;
        /// Returns connection timeout for the HTTP session.

        Poco::Timespan getSendTimeout() const;
        /// Returns send timeout for the HTTP session.

        Poco::Timespan getReceiveTimeout() const;
        /// Returns receive timeout for the HTTP session.

        void setSendDataHooks(const HTTPSessionDataHooksPtr & sendDataHooks = {});
        /// Sets data hooks that will be called on every sent to the socket.

        void setReceiveDataHooks(const HTTPSessionDataHooksPtr & receiveDataHooks = {});
        /// Sets data hooks that will be called on every receive from the socket.

        bool connected() const;
        /// Returns true if the underlying socket is connected.

        virtual void abort();
        /// Aborts a session in progress by shutting down
        /// and closing the underlying socket.

        const Poco::Exception * networkException() const;
        /// If sending or receiving data over the underlying
        /// socket connection resulted in an exception, a
        /// pointer to this exception is returned.
        ///
        /// Otherwise, NULL is returned.

        void attachSessionData(const Poco::Any & data);
        /// Allows to attach an application-specific data
        /// item to the session.
        ///
        /// On the server side, this can be used to manage
        /// data that must be maintained over the entire
        /// lifetime of a persistent connection (that is,
        /// multiple requests sent over the same connection).

        const Poco::Any & sessionData() const;
        /// Returns the data attached with attachSessionData(),
        /// or an empty Poco::Any if no user data has been
        /// attached.

        enum
        {
            HTTP_PORT = 80
        };

        StreamSocket detachSocket();
        /// Detaches the socket from the session.
        ///
        /// The socket is returned, and a new, uninitialized socket is
        /// attached to the session.

        StreamSocket & socket();
        /// Returns a reference to the underlying socket.

        void drainBuffer(Poco::Buffer<char> & buffer);
        /// Copies all bytes remaining in the internal buffer to the
        /// given Poco::Buffer, resizing it as necessary.
        ///
        /// This is usually used together with detachSocket() to
        /// obtain any data already read from the socket, but not
        /// yet processed.

    protected:
        HTTPSession();
        /// Creates a HTTP session using an
        /// unconnected stream socket.

        HTTPSession(const StreamSocket & socket);
        /// Creates a HTTP session using the
        /// given socket. The session takes ownership
        /// of the socket and closes it when it's no
        /// longer used.

        HTTPSession(const StreamSocket & socket, bool keepAlive);
        /// Creates a HTTP session using the
        /// given socket. The session takes ownership
        /// of the socket and closes it when it's no
        /// longer used.

        virtual ~HTTPSession();
        /// Destroys the HTTPSession and closes the
        /// underlying socket.

        int get();
        /// Returns the next byte in the buffer.
        /// Reads more data from the socket if there are
        /// no bytes left in the buffer.

        int peek();
        /// Peeks at the next character in the buffer.
        /// Reads more data from the socket if there are
        /// no bytes left in the buffer.

        virtual int read(char * buffer, std::streamsize length);
        /// Reads up to length bytes.
        ///
        /// If there is data in the buffer, this data
        /// is returned. Otherwise, data is read from
        /// the socket to avoid unnecessary buffering.

        virtual int write(const char * buffer, std::streamsize length);
        /// Writes data to the socket.

        int receive(char * buffer, int length);
        /// Reads up to length bytes.

        int buffered() const;
        /// Returns the number of bytes in the buffer.

        void refill();
        /// Refills the internal buffer.

        virtual void connect(const SocketAddress & address);
        /// Connects the underlying socket to the given address
        /// and sets the socket's receive timeout.

        void attachSocket(const StreamSocket & socket);
        /// Attaches a socket to the session, replacing the
        /// previously attached socket.

        void close();
        /// Closes the underlying socket.

        void setException(const Poco::Exception & exc);
        /// Stores a clone of the exception.

        void clearException();
        /// Clears the stored exception.

    private:
        enum
        {
            HTTP_DEFAULT_TIMEOUT = 60000000,
            HTTP_DEFAULT_CONNECTION_TIMEOUT = 30000000
        };

        HTTPSession(const HTTPSession &);
        HTTPSession & operator=(const HTTPSession &);

        StreamSocket _socket;
        std::unique_ptr<char[]> _pBuffer;
        char * _pCurrent;
        char * _pEnd;
        bool _keepAlive;
        Poco::Timespan _connectionTimeout;
        Poco::Timespan _receiveTimeout;
        Poco::Timespan _sendTimeout;
        ThrottlerPtr _receiveThrottler;
        ThrottlerPtr _sendThrottler;
        Poco::Exception * _pException;
        Poco::Any _data;

        // Data hooks
        HTTPSessionDataHooksPtr _sendDataHooks;
        HTTPSessionDataHooksPtr _receiveDataHooks;

        friend class HTTPStreamBuf;
        friend class HTTPHeaderStreamBuf;
        friend class HTTPFixedLengthStreamBuf;
        friend class HTTPChunkedStreamBuf;
    };


    //
    // inlines
    //
    inline bool HTTPSession::getKeepAlive() const
    {
        return _keepAlive;
    }

    inline Poco::Timespan HTTPSession::getTimeout() const
    {
        return _receiveTimeout;
    }

    inline Poco::Timespan HTTPSession::getConnectionTimeout() const
    {
        return _connectionTimeout;
    }

    inline Poco::Timespan HTTPSession::getSendTimeout() const
    {
        return _sendTimeout;
    }

    inline Poco::Timespan HTTPSession::getReceiveTimeout() const
    {
        return _receiveTimeout;
    }

    inline void HTTPSession::setSendDataHooks(const HTTPSessionDataHooksPtr & sendDataHooks)
    {
        _sendDataHooks = sendDataHooks;
    }

    inline void HTTPSession::setReceiveDataHooks(const HTTPSessionDataHooksPtr & receiveDataHooks)
    {
        _receiveDataHooks = receiveDataHooks;
    }

    inline StreamSocket & HTTPSession::socket()
    {
        return _socket;
    }


    inline const Poco::Exception * HTTPSession::networkException() const
    {
        return _pException;
    }


    inline int HTTPSession::buffered() const
    {
        return static_cast<int>(_pEnd - _pCurrent);
    }


    inline const Poco::Any & HTTPSession::sessionData() const
    {
        return _data;
    }


}
} // namespace Poco::Net


#endif // Net_HTTPSession_INCLUDED
