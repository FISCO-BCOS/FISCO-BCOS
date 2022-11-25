/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @author wheatli
 * @date 2018.8.27
 * @modify add CallbackCollectionHandler.h
 *
 */
#pragma once
#include "Common.h"
#include "DataConvertUtility.h"

namespace bcos
{
template <typename... Args>
class CallbackCollectionHandler
{
public:
    using Callback = std::function<void(Args...)>;

    class SingleCallback
    {
        friend class CallbackCollectionHandler;

    public:
        ~SingleCallback()
        {
            if (m_callbackCollectionHandler)
            {
                m_callbackCollectionHandler->m_callbackCollection.erase(m_iterator);
            }
        }
        void reset() { m_callbackCollectionHandler = nullptr; }
        void call(Args const&... _args) { m_callback(_args...); }

    private:
        SingleCallback(unsigned _iterator, CallbackCollectionHandler* _callbackCollectionHandler,
            Callback const& _callback)
          : m_iterator(_iterator),
            m_callbackCollectionHandler(_callbackCollectionHandler),
            m_callback(_callback)
        {}

        unsigned m_iterator = 0;
        CallbackCollectionHandler* m_callbackCollectionHandler = nullptr;
        Callback m_callback;
    };

    ~CallbackCollectionHandler()
    {
        for (auto const& item : m_callbackCollection)
        {
            if (auto callback = item.second.lock())
            {
                callback->reset();
            }
        }
    }

    std::shared_ptr<SingleCallback> add(Callback _callback)
    {
        auto iterator =
            m_callbackCollection.empty() ? 0 : (m_callbackCollection.rbegin()->first + 1);
        auto callback = std::shared_ptr<SingleCallback>(
            new SingleCallback(iterator, this, std::move(_callback)));
        m_callbackCollection[iterator] = callback;
        return callback;
    }

    void operator()(Args const&... _args)
    {
        auto callbackCollection = convertMapToVector(m_callbackCollection);
        for (auto const& singleCallback : *callbackCollection)
        {
            auto callback = singleCallback.lock();
            if (callback)
            {
                callback->call(_args...);
            }
        }
    }

private:
    std::map<unsigned, std::weak_ptr<typename CallbackCollectionHandler::SingleCallback>>
        m_callbackCollection;
};

template <class... Args>
using Handler = std::shared_ptr<typename CallbackCollectionHandler<Args...>::SingleCallback>;
}  // namespace bcos