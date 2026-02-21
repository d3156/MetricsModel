#pragma once
#include "Metrics.hpp"
#include <boost/property_tree/ptree_fwd.hpp>
#include <cstddef>
#include <map>
#include <set>
#include <chrono>
#include <BaseConfig>

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
        Condition(d3156::Config *parent)
            : text("condition", "", "string", parent), delta_mode("delta_mode", false, "bool", parent)
        {
        }
        d3156::ConfigString text;
        ConditionType type = ConditionType::Error;
        size_t value;     // для > < >= <= =
        size_t min_value; // для Range
        size_t max_value; // для Range

        d3156::ConfigBool delta_mode;
        size_t lastValue = 0;
        std::string tostring();

        void init();
    };

    struct Notify : public d3156::Config {
        Notify() : d3156::Config(""), condition(this) {}
        /// From config
        Condition condition;
        CONFIG_STRING(metric, "");
        CONFIG_UINT(alert_count, 0);     /// Количество повторов для срабатывания
        CONFIG_ARRAY(tags, std::string); // optional
        CONFIG_STRING(alertStartMessage, "Alert! {metric}:{value} {tags}");
        CONFIG_STRING(alertStoppedMessage, "Alert stopped! {metric}:{value} {tags}");

        std::chrono::time_point<std::chrono::steady_clock> start_;
        std::string formatAlertMessage(const std::string &tmpl, Metrics::Metric *metric);
        std::unique_ptr<Metrics::Counter> alert_count_in_period;
        std::map<Metrics::Metric *, std::pair<size_t, size_t>> alerts_count;
    };

    class NotifyManager
    {
        friend class ::MetricsModel;
        std::unordered_map<std::string, Notify> notifiers_map;
        std::set<NotifierProvider *> alert_providers;
        NotifyManager(d3156::Config *parent) : report(parent), notifiers("notifiers", parent) {}
        void upload(std::set<Metrics::Metric *> &statistics);
        void reporter();
        void init();

        struct Report : public d3156::Config {
            Report(d3156::Config *parent) : d3156::Config("report", parent) {}
            CONFIG_UINT(periodHours, 12);
            CONFIG_STRING(headText, "📝 Отчет за преиод {period}ч.:");
            CONFIG_STRING(conditionText, "⚠️ Количество срабатываний условий:");
            CONFIG_STRING(alertText, "🚨 Количество срабатываний оповещений:");
            CONFIG_BOOL(needSend, true);
            std::chrono::time_point<std::chrono::steady_clock> last_sended_report;
        } report;
        d3156::ConfigArray<Notify> notifiers;
    };
}