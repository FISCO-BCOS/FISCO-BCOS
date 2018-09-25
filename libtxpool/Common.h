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
 * @brief : Common functions of transaction pool
 * @author: yujiechen
 * @date: 2018-09-23
 */
#include <libdevcore/Common.h>
namespace dev
{
namespace txpool
{
struct TxPoolStatus
{
    size_t current;
    size_t future;
    size_t unverified;
    size_t dropped;
};
struct Limits
{
    size_t current;
    size_t future;
};
}  // namespace txpool
}  // namespace dev
