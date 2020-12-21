/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file P2PMessage.h
 *  @author monan
 *  @date 20181112
 */

#pragma once

#include <libnetwork/Common.h>
#include <libprotocol/CommonProtocolType.h>
#include <libutilities/FixedBytes.h>
#include <boost/beast.hpp>
#include <memory>

namespace bcos
{
namespace p2p
{
class HttpMessage : public Message
{
public:
    typedef std::shared_ptr<HttpMessage> Ptr;

    HttpMessage() {}

    virtual ~HttpMessage() {}

    virtual uint32_t length() override { return m_length; }
    virtual void setLength(uint32_t _length) { m_length = _length; }

    virtual PROTOCOL_ID protocolID() { return m_protocolID; }
    virtual void setProtocolID(PROTOCOL_ID _protocolID) { m_protocolID = _protocolID; }

    virtual uint32_t seq() override { return m_seq; }
    virtual void setSeq(uint32_t _seq) { m_seq = _seq; }

    virtual bool isRequestPacket() override { return m_isRequest; }
    virtual void setIsRequestPacket(bool isRequest) { m_isRequest = isRequest; }

    std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> request()
    {
        return m_request;
    }
    void setRequest(
        std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> request)
    {
        m_request = request;
    }

    std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>> response()
    {
        return m_response;
    }
    void setResponse(
        std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>> response)
    {
        m_response = response;
    }

    virtual void encode(bytes& buffer) override
    {
        if (m_isRequest)
        {
            auto serializer = std::make_shared<
                boost::beast::http::request_serializer<boost::beast::http::string_body>>(
                *m_request);

            boost::system::error_code ec;
            do
            {
                serializer->next(
                    ec, [serializer, &buffer](boost::system::error_code& ec, auto const& buf) {
                        ec.assign(0, ec.category());
                        buffer.insert(buffer.end(), buf.begin(), buf.end());
                        serializer->consume(boost::asio::buffer_size(buf));
                    });
            } while (!ec && !serializer->is_done());

            if (ec)
            {
                LOG(ERROR) << "Encode message failed!";
            }
        }
        else
        {
            auto serializer = std::make_shared<
                boost::beast::http::response_serializer<boost::beast::http::string_body>>(
                *m_response);

            boost::system::error_code ec;
            do
            {
                serializer->next(
                    ec, [serializer, &buffer](boost::system::error_code& ec, auto const& buf) {
                        ec.assign(0, ec.category());
                        buffer.insert(buffer.end(), buf.begin(), buf.end());
                        serializer->consume(boost::asio::buffer_size(buf));
                    });
            } while (!ec && !serializer->is_done());

            if (ec)
            {
                LOG(ERROR) << "Encode message failed!";
            }
        }
    }

    /// < If the decoding is successful, the length of the decoded data is returned; otherwise, 0 is
    /// returned.
    virtual ssize_t decode(const byte* buffer, size_t size) override
    {
        if (m_isRequest)
        {
            m_request =
                std::make_shared<boost::beast::http::request<boost::beast::http::string_body>>();
            boost::beast::http::request_parser<boost::beast::http::string_body> parser(*m_request);

            boost::system::error_code ec;
            auto result = parser.put(boost::asio::const_buffer(buffer, size), ec);

            if (ec)
            {
                LOG(ERROR) << "Parse http request failed: " << ec << " "
                           << std::string(buffer, size);
                return PACKET_ERROR;
            }

            if (parser.is_done())
            {
                return result;
            }
            else
            {
                return PACKET_INCOMPLETE;
            }
        }
        else
        {
            m_response =
                std::make_shared<boost::beast::http::response<boost::beast::http::string_body>>();
            boost::beast::http::response_parser<boost::beast::http::string_body> parser(
                *m_response);

            boost::system::error_code ec;
            auto result = parser.put(boost::asio::const_buffer(buffer, size), ec);

            if (ec)
            {
                LOG(ERROR) << "Parse http request failed: " << ec << " "
                           << std::string(buffer, size);
                return PACKET_ERROR;
            }

            if (parser.is_done())
            {
                return result;
            }
            else
            {
                return PACKET_INCOMPLETE;
            }
        }
    }

private:
    uint32_t m_length = 0;         ///< m_length = HEADER_LENGTH + length(m_buffer)
    PROTOCOL_ID m_protocolID = 0;  ///< message type, the first two bytes of information, when
                                   ///< greater than 0 is the ID of the request package.
    PACKET_TYPE m_packetType = 0;  ///< message sub type, the second two bytes of information
    uint32_t m_seq = 0;            ///< the message identify

    bool m_isRequest = true;
    std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> m_request;
    std::shared_ptr<boost::beast::http::response<boost::beast::http::string_body>> m_response;
};

class HttpMessageFactory : public MessageFactory
{
public:
    virtual ~HttpMessageFactory() {}
    virtual Message::Ptr buildMessage() override
    {
        auto message = std::make_shared<HttpMessage>();

        return message;
    }

    virtual uint32_t newSeq()
    {
        uint32_t seq = ++m_seq;
        return seq;
    }

    std::atomic<uint32_t> m_seq = {1};
};

}  // namespace p2p
}  // namespace bcos
