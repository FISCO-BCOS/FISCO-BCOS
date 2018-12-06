/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : keycenter for disk encrytion
 * @author: jimmyshi
 * @date: 2018-12-03
 */
#pragma once
#include "Common.h"
#include <memory>
#include <string>

namespace dev
{
class KeyCenter
{
public:
    using Ptr = std::shared_ptr<KeyCenter>;

    KeyCenter(){};

    virtual const dev::bytes getDataKey(const std::string& _cipherDataKey);
    virtual const std::string generateCipherDataKey();
    void setUrl(const std::string& _url = "");
    const std::string url() { return m_url; }

    static KeyCenter& instance();

private:
    std::string m_url;
};

#define g_keyCenter KeyCenter::instance()  // Only one keycenter in a node

}  // namespace dev