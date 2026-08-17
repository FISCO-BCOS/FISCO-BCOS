#include "Common.h"
#include <algorithm>

bcos::amop::TopicItem::TopicItem() = default;

bcos::amop::TopicItem::TopicItem(const std::string& _topicName) : m_topicName(_topicName) {}

std::string bcos::amop::TopicItem::topicName() const
{
    return m_topicName;
}

void bcos::amop::TopicItem::setTopicName(const std::string& _topicName)
{
    m_topicName = _topicName;
}

bool bcos::amop::operator<(const TopicItem& _topicItem0, const TopicItem& _topicItem1)
{
    return _topicItem0.topicName() < _topicItem1.topicName();
}

std::string bcos::amop::randomChoose(std::vector<std::string> _datas)
{
    auto seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine engine(seed);
    std::shuffle(_datas.begin(), _datas.end(), engine);
    return *(_datas.begin());
}

std::string bcos::amop::shortHex(std::string const& _nodeID)
{
    return _nodeID.substr(0, 8);
}