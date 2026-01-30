#pragma once
#include "Metrics.hpp"
#include <boost/property_tree/ptree_fwd.hpp>
#include <map>
#include <set>
#include <vector>

namespace NotifierSystem
{

    class NotifierProvider
    {
    public:
        virtual void alert(const std::string &) = 0;
        virtual ~NotifierProvider()             = default;
    };

    struct Notify;
    class NotifyManager
    {
        friend class ::MetricsModel;
        std::map<Metrics::Metric *, size_t> alerts_count;
        std::unordered_map<std::string, Notify> notifiers;
        std::set<NotifierProvider *> alert_providers;
        NotifyManager() {}
        static boost::property_tree::ptree getDefault();
        bool parseSettings(const boost::property_tree::ptree &notifiers);
        void upload(std::set<Metrics::Metric *> &statistics);

    public:
    };
}