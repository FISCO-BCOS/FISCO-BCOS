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
 */
/**
 * @brief : Trie for critical fields analysing
 * @author: jimmyshi
 * @date: 2022-1-14
 */


#pragma once
#include <map>
#include <memory>
#include <vector>

namespace bcos
{
namespace executor
{

template <typename K, typename V>
class TrieNode
{
public:
    using Ptr = std::shared_ptr<TrieNode>;

private:
    std::map<K, TrieNode<K, V>::Ptr> m_children;
    std::optional<V> m_value;

public:
    void set(std::vector<K> const& _path, size_t _depth, V _value)
    {
        if (_path.size() <= _depth)
        {  // is myself
            m_value = std::make_optional(_value);
            m_children.clear();
        }
        else
        {  // is children
            auto childName = _path[_depth];
            TrieNode<K, V>::Ptr child = m_children[childName];
            if (child == nullptr)
            {
                child = std::make_shared<TrieNode<K, V>>();
                m_children[childName] = child;
            }
            child->set(_path, _depth + 1, _value);
        }
    }

    std::vector<V> get(std::vector<K> const& _path, size_t _depth)
    {
        if (_depth < _path.size())
        {
            // path is deeper than node trie record,
            // return the most deep node who has value

            TrieNode<K, V>::Ptr child = m_children[_path[_depth]];
            std::vector<V> res;
            if (child == nullptr)
            {
                if (m_value.has_value())
                {
                    res.push_back(m_value.value());
                }
            }
            else
            {
                res = std::move(child->get(_path, _depth + 1));
                if (res.empty() && m_value.has_value())
                {
                    res.push_back(m_value.value());
                }
            }
            return res;
        }
        else
        {
            // path is not deeper than node trie
            // return all node's value under this path
            std::vector<V> res;
            if (m_value.has_value())
            {
                res.push_back(m_value.value());
            }
            for (auto itr : m_children)
            {
                auto child = itr.second;
                for (auto value : child->get(_path, _depth + 1))
                {
                    res.push_back(value);
                }
            }
            return res;
        }
    }
};

template <typename K, typename V>
class TrieSet
{
public:
    // set a value into a path
    void set(std::vector<K> const& _path, V _value) { m_root.set(_path, 0, _value); }

    // get all values under a path which has the same prefix _path
    std::vector<V> get(std::vector<K> const& _path) { return m_root.get(_path, 0); }

private:
    TrieNode<K, V> m_root;
};

}  // namespace executor
}  // namespace bcos