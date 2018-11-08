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
/** @file CommonInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "CommonInitializer.h"

using namespace dev;
using namespace dev::initializer;

void CommonInitializer::initConfig(boost::property_tree::ptree const& _pt)
{
    m_dataPath = _pt.get<std::string>("common.data_path", "");
    m_logConfig = _pt.get<std::string>("common.log_config", "${DATAPATH}/log.conf");
    INITIALIZER_LOG(DEBUG) << "[#CommonInitializer::initConfig] [dataPath/logConfig]: "
                           << m_dataPath << "/" << m_logConfig;
    if (m_dataPath.empty())
    {
        INITIALIZER_LOG(ERROR) << "[#CommonInitializer::initConfig] data path unspecified.";
        BOOST_THROW_EXCEPTION(DataPathUnspecified());
    }
}

std::string CommonInitializer::dataPath()
{
    return m_dataPath;
}

std::string CommonInitializer::logConfig()
{
    return m_logConfig;
}