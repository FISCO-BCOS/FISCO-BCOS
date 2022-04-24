/** @file P2PSession.cpp
 *  @author monan
 *  @date 20181112
 */

#include <bcos-gateway/Gateway.h>
#include <bcos-gateway/libp2p/P2PMessage.h>
#include <bcos-gateway/libp2p/P2PSession.h>
#include <bcos-gateway/libp2p/Service.h>

#include <bcos-utilities/Common.h>
#include <boost/algorithm/string.hpp>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::boostssl;

// explicit P2PSession::P2PSession(std::string _moduleName) : WsSession(_moduleName)
// {
//     P2PSESSION_LOG(INFO) << "[P2PSession::P2PSession] this=" << this;
// }

P2PSession::~P2PSession()
{
    P2PSESSION_LOG(INFO) << "[P2PSession::~P2PSession] this=" << this;
}

void P2PSession::start()
{
    if (!m_run)
    {
        m_run = true;
    }
}
