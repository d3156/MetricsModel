#pragma once
#include "Metrics.hpp"
#include <boost/property_tree/ptree_fwd.hpp>
#include <map>
#include <set>
#include <chrono>

namespace NotifierSystem
{

    class NotifierProvider
    {
    public:
        virtual void alert(const std::string &) = 0;
        virtual ~NotifierProvider()             = default;
    };

    enum class ConditionType { Greater, Less, GreaterEqual, LessEqual, Equal, Range, Error };

    struct Condition {
        std::string text = "";
        ConditionType type;
        size_t value;     // для > < >= <= =
        size_t min_value; // для Range
        size_t max_value; // для Range

        bool delta_mode  = false;
        size_t lastValue = 0;
        std::string tostring();
    };

    struct Notify {
        /// From config
        std::string metric = "";
        size_t alert_count = 0; /// Количество повторов для срабатывания
        Condition condition;
        std::set<std::string> tags = {}; // optional

        /// Runtime
        std::string alertStartMessage   = "Alert! {metric}:{value} {tags}";
        std::string alertStoppedMessage = "Alert stopped! {metric}:{value} {tags}";

        std::chrono::time_point<std::chrono::steady_clock> start_;
        std::string formatAlertMessage(const std::string &tmpl, Metrics::Metric *metric);
    };

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