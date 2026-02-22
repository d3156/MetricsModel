#include "NotifierSystem.hpp"
#include "Metrics.hpp"
#include <algorithm>
#include <boost/algorithm/string/replace.hpp>
#include <boost/property_tree/ptree.hpp>
#include <PluginCore/Logger/Log>
#include <chrono>
#include <memory>
#include <string>
#include <iomanip>

#define LOG_NAME "NotifierSystem"

using boost::property_tree::ptree;
namespace NotifierSystem
{

    bool check_condition(Condition &c, size_t metric_value)
    {
        auto value = metric_value;
        if (c.delta_mode) {
            if (c.lastValue)
                value = metric_value - c.lastValue;
            else
                value = 0;
            Y_LOG(100, " metric_value:" << metric_value << " value:" << value << c.tostring());
            c.lastValue = metric_value;
        } else
            Y_LOG(100, " value:" << value << c.tostring());

        switch (c.type) {
            case ConditionType::Greater: return value > c.value;
            case ConditionType::Less: return value < c.value;
            case ConditionType::Equal: return value == c.value;
            case ConditionType::Range: return value >= c.min_value && value <= c.max_value;
            case ConditionType::GreaterEqual: return value >= c.value;
            case ConditionType::LessEqual: return value <= c.value;
            case ConditionType::Error: break;
        }
        return false;
    }

    std::string Condition::tostring()
    {
        auto res = " condition:" + text.value + " delta_mode: " + std::to_string(delta_mode.value);
        if (lastValue) res += " lastValue:" + std::to_string(lastValue);
        return res;
    }

    std::string format_duration(std::chrono::steady_clock::duration d)
    {
        auto days = std::chrono::duration_cast<std::chrono::days>(d);
        d -= days;
        auto hours = std::chrono::duration_cast<std::chrono::hours>(d);
        d -= hours;
        auto minutes = std::chrono::duration_cast<std::chrono::minutes>(d);
        d -= minutes;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(d);
        d -= seconds;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d);
        std::ostringstream oss;
        oss << std::setfill('0');
        if (days.count() > 0) oss << days.count() << "д ";
        if (hours.count()) oss << hours.count() << "ч ";
        if (minutes.count()) oss << minutes.count() << "м ";
        if (seconds.count()) oss << seconds.count() << "с ";
        if (ms.count()) oss << ms.count() << "мс";
        return oss.str();
    }

    std::string Notify::formatAlertMessage(const std::string &tmpl, Metrics::Metric *metric)
    {
        std::string msg = tmpl;
        boost::replace_all(msg, "{metric}", metric->name);
        boost::replace_all(msg, "{duration}", format_duration(std::chrono::steady_clock::now() - start_));
        boost::replace_all(msg, "{value}", std::to_string(metric->value_));
        size_t pos = msg.find("{tags}");
        if (pos != std::string::npos) {
            std::string tags;
            for (const auto &t : metric->tags) {
                if (!tags.empty()) tags += ",";
                tags += t.first + "=" + t.second;
            }
            msg.replace(pos, 6, tags);
        }
        pos = 0;
        while ((pos = msg.find("{tag:", pos)) != std::string::npos) {
            size_t pos_end = msg.find("}", pos);
            if (pos_end == std::string::npos) break;
            std::string key = msg.substr(pos + 5, pos_end - (pos + 5));
            auto res = std::ranges::find_if(metric->tags, [=](const Metrics::Tag &val) { return val.first == key; });
            if (res != metric->tags.end()) {
                msg.replace(pos, pos_end - pos + 1, res->second);
                pos += res->second.length(); // Продолжаем поиск после замены
            } else
                pos = pos_end + 1; // Пропускаем если тег не найден
        }
        return msg;
    }

    void Condition::init()
    {
        auto &s = text.value;
        if (s.empty()) return;
        if (s.front() == '[' && s.back() == ']') {
            auto sep = s.find(';');
            if (sep == std::string::npos) return;
            try {
                min_value = std::stod(s.substr(1, sep - 1));
                max_value = std::stod(s.substr(sep + 1, s.size() - sep - 2));
                if (min_value > max_value) return;
                type = ConditionType::Range;
            } catch (...) {
            }
            return;
        }
        if (s.starts_with(">=")) {
            type  = ConditionType::GreaterEqual;
            value = std::stoul(s.substr(2));
            return;
        }
        if (s.starts_with("<=")) {
            type  = ConditionType::LessEqual;
            value = std::stoul(s.substr(2));
            return;
        };
        if (s.starts_with(">")) {
            type  = ConditionType::Greater;
            value = std::stoul(s.substr(1));
            return;
        };
        if (s.starts_with("<")) {
            type  = ConditionType::Less;
            value = std::stoul(s.substr(1));
            return;
        };
        if (s.starts_with("=")) {
            type  = ConditionType::Equal;
            value = std::stoul(s.substr(1));
            return;
        };
        return;
    }

    void NotifyManager::upload(std::set<Metrics::Metric *> &statistics)
    {
        if (alert_providers.empty()) return;
        std::vector<std::string> alerts;
        for (auto metric : statistics) {
            auto notifier = notifiers_map.find(metric->name);
            if (notifier == notifiers_map.end()) continue;
            if (!notifier->second->tags.items.empty() &&
                !std::all_of(notifier->second->tags.items.begin(), notifier->second->tags.items.end(),
                             [&](const auto &nt) {
                                 return std::any_of(metric->tags.begin(), metric->tags.end(),
                                                    [&](const Metrics::Tag &mt) { return mt.second == *nt; });
                             })) {
                continue;
            }
            auto &[current_count, current_total_count] = notifier->second->alerts_count[metric];

            if (check_condition(notifier->second->condition, metric->value_)) {
                current_count++;
                current_total_count++;

                Y_LOG(100, "condition checked: " << notifier->second->condition.tostring() << "alert count "
                                                 << current_count << " for metric: " << metric->toString(false));
                if (current_count == notifier->second->alert_count) {
                    Y_LOG(100, "alert start : " << notifier->second->condition.tostring()
                                                << " for metric: " << metric->toString(false));
                    alerts.emplace_back(
                        notifier->second->formatAlertMessage(notifier->second->alertStartMessage, metric));
                    notifier->second->start_ = std::chrono::steady_clock::now();
                    (*notifier->second->alert_count_in_period)++;
                }
            } else {
                if (current_count >= notifier->second->alert_count) {
                    Y_LOG(100, "alert stop : " << notifier->second->condition.tostring()
                                               << " for metric: " << metric->toString(false));
                    alerts.emplace_back(
                        notifier->second->formatAlertMessage(notifier->second->alertStoppedMessage, metric));
                }
                current_count = 0;
            }
        }
        for (auto &alert : alerts)
            for (auto &provider : alert_providers) provider->alert(alert);
        reporter();
    }

    void NotifyManager::init()
    {
        try {
            for (auto &n : notifiers.items) {
                if (n->metric.value.empty()) continue;
                n->condition.init();
                LOG(5, n->condition.tostring());
                if (n->condition.type == ConditionType::Error) {
                    R_LOG(1, " invalid condition in notifier for metric " << n->metric.value);
                    continue;
                }
                std::string tags_joined = "";
                for (auto &t : n->tags.items) tags_joined += (tags_joined.size() ? ", " : "") + *t;
                n->alert_count_in_period = std::make_unique<Metrics::Counter>(
                    "Notify_count_in_period",
                    std::vector<Metrics::Tag>{{"metric", n->metric.value}, {"tags", tags_joined}});
                notifiers_map.emplace(n->metric.value, std::move(n));
            }
            notifiers.items.clear();
            notifiers.name.clear();
        } catch (const std::exception &e) {
            R_LOG(1, "NotifyManager error on load config " << e.what());
        }
        size_t pos = report.headText.value.find("{period}");
        if (pos != std::string::npos) report.headText.value.replace(pos, 8, std::to_string(report.periodHours.value));
        report.last_sended_report = std::chrono::steady_clock::now() - std::chrono::hours(report.periodHours.value);
    }

    void NotifyManager::reporter()
    {
        if (!report.needSend) return;
        G_LOG(50, "Report check");
        if (std::chrono::steady_clock::now() - report.last_sended_report < std::chrono::hours(report.periodHours.value))
            return;
        report.last_sended_report = std::chrono::steady_clock::now();
        std::string conditions = "", alerts = "";
        for (auto &alert : notifiers_map) {
            alerts += "\n        " + std::to_string(*alert.second->alert_count_in_period) + " : " +
                      alert.second->metric.value + " " + alert.second->condition.tostring();
            alert.second->alert_count_in_period->exchange();
            for (auto &[metric, condition_count] : alert.second->alerts_count)
                if (condition_count.second) {
                    conditions += "\n        " + std::to_string(condition_count.second) + " : " +
                                  alert.second->formatAlertMessage(alert.second->alertStartMessage, metric);
                    condition_count.second = 0;
                }
        }
        auto report_text = report.headText.value + "\n" + report.alertText.value + alerts + "\n" +
                           report.conditionText.value + conditions;
        for (auto &provider : alert_providers) provider->alert(report_text);
        G_LOG(50, "Report:" << report_text);
    }
}