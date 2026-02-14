#include "NotifierSystem.hpp"
#include "Metrics.hpp"
#include <boost/property_tree/ptree.hpp>
#include <PluginCore/Logger/Log.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <iomanip>

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
        auto res = " condition:" + text + " delta_mode: " + std::to_string(delta_mode);
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
        size_t pos      = msg.find("{metric}");
        if (pos != std::string::npos) msg.replace(pos, 8, metric->name);
        pos = msg.find("{duraion}");

        if (pos != std::string::npos) msg.replace(pos, 9, format_duration(std::chrono::steady_clock::now() - start_));
        pos = msg.find("{value}");
        if (pos != std::string::npos) msg.replace(pos, 7, std::to_string(metric->value_));
        pos = msg.find("{tags}");
        if (pos != std::string::npos) {
            std::string tags;
            for (const auto &t : metric->tags) {
                if (!tags.empty()) tags += ",";
                tags += t.first + "=" + t.second;
            }
            msg.replace(pos, 6, tags);
        }
        return msg;
    }

    Condition parse_condition(const std::string &s)
    {
        if (s.empty()) return {.type = ConditionType::Error};
        if (s.front() == '[' && s.back() == ']') {
            Condition c;
            auto sep = s.find(';');
            if (sep == std::string::npos) return {.type = ConditionType::Error};
            try {
                c.min_value = std::stod(s.substr(1, sep - 1));
                c.max_value = std::stod(s.substr(sep + 1, s.size() - sep - 2));
                if (c.min_value > c.max_value) return {.type = ConditionType::Error};
                c.type = ConditionType::Range;
            } catch (...) {
                c.type = ConditionType::Error;
            }
            c.text = s;
            return c;
        }
        if (s.starts_with(">="))
            return {.text = s, .type = ConditionType::GreaterEqual, .value = std::stoul(s.substr(2))};
        if (s.starts_with("<=")) return {.text = s, .type = ConditionType::LessEqual, .value = std::stoul(s.substr(2))};
        if (s.starts_with(">")) return {.text = s, .type = ConditionType::Greater, .value = std::stoul(s.substr(1))};
        if (s.starts_with("<")) return {.text = s, .type = ConditionType::Less, .value = std::stoul(s.substr(1))};
        if (s.starts_with("=")) return {.text = s, .type = ConditionType::Equal, .value = std::stoul(s.substr(1))};
        return {.type = ConditionType::Error};
    }

    bool NotifyManager::parseSettings(const ptree &ntfs)
    {
        try {
            for (auto &n : ntfs) {
                auto name = n.second.get<std::string>("metric", "");
                if (name.empty()) continue;
                auto condition = parse_condition(n.second.get<std::string>("condition", ""));
                LOG(5, condition.tostring());
                if (condition.type == ConditionType::Error) {
                    R_LOG(1, " invalid condition in notifier for metric " << n.second.get<std::string>("metric", ""));
                    continue;
                }
                condition.delta_mode    = n.second.get<bool>("delta_mode", false);
                std::string tags_joined = "";
                std::set<std::string> tags;
                for (auto &t : n.second.get_child("tags", ptree{})) {
                    auto tag = t.second.get_value<std::string>();
                    if (tags_joined.size()) tags_joined += ", ";
                    tags_joined += tag;
                    tags.insert(tag);
                }
                notifiers[name] = Notify{
                    .metric                = name,
                    .alert_count           = std::max<size_t>(1, n.second.get<size_t>("alert_count", 1)),
                    .condition             = condition,
                    .tags                  = tags,
                    .alertStartMessage     = n.second.get<std::string>("alertStartMessage", " "),
                    .alertStoppedMessage   = n.second.get<std::string>("alertStoppedMessage", ""),
                    .alert_count_in_period = std::make_unique<Metrics::Counter>(
                        "Notify_count_in_period", std::vector<Metrics::Tag>{{"metric", name}, {"tags", tags_joined}})};
            }
        } catch (const std::exception &e) {
            R_LOG(1, "NotifyManager error on load config " << e.what());
            return false;
        }
        return true;
    }

    boost::property_tree::ptree NotifyManager::getDefault()
    {
        ptree notifiers, notifier, tags;
        notifier.put("metric", "");
        notifier.put("alert_count", 1);
        notifier.put("condition", "");
        notifier.put("delta_mode", false);
        notifier.add_child("tags", tags);
        notifier.put("alertStartMessage", "Alert! {metric}:{value} {tags}");
        notifier.put("alertStoppedMessage", "Alert stopped! {metric}:{value} {tags}");
        notifiers.push_back(std::make_pair("", notifier));
        return notifiers;
    }

    void NotifyManager::upload(std::set<Metrics::Metric *> &statistics)
    {
        if (alert_providers.empty()) return;
        std::vector<std::string> alerts;
        if (alerts_count.size() == 0)
            for (auto metric : statistics) alerts_count[metric] = {0, 0};
        for (auto metric : statistics) {
            auto notifier = notifiers.find(metric->name);
            if (notifier == notifiers.end()) continue;
            if (!notifier->second.tags.empty() &&
                !std::all_of(notifier->second.tags.begin(), notifier->second.tags.end(), [&](const std::string &nt) {
                    return std::any_of(metric->tags.begin(), metric->tags.end(),
                                       [&](const Metrics::Tag &mt) { return mt.second == nt; });
                })) {
                continue;
            }
            auto &[current_count, current_total_count] = alerts_count[metric];

            if (check_condition(notifier->second.condition, metric->value_)) {
                current_count++;
                current_total_count++;

                Y_LOG(100, "condition checked: " << notifier->second.condition.tostring() << "alert count "
                                                 << current_count << " for metric: " << metric->toString(false));
                if (current_count == notifier->second.alert_count) {
                    Y_LOG(100, "alert start : " << notifier->second.condition.tostring()
                                                << " for metric: " << metric->toString(false));
                    alerts.emplace_back(
                        notifier->second.formatAlertMessage(notifier->second.alertStartMessage, metric));
                    notifier->second.start_ = std::chrono::steady_clock::now();
                    (*notifier->second.alert_count_in_period)++;
                }
            } else {
                if (current_count >= notifier->second.alert_count) {
                    Y_LOG(100, "alert stop : " << notifier->second.condition.tostring()
                                               << " for metric: " << metric->toString(false));
                    alerts.emplace_back(
                        notifier->second.formatAlertMessage(notifier->second.alertStoppedMessage, metric));
                }
                current_count = 0;
            }
        }
        for (auto &alert : alerts)
            for (auto &provider : alert_providers) provider->alert(alert);
        reporter();
    }

    boost::property_tree::ptree NotifyManager::Report::getDefault()
    {
        ptree report;
        report.put("periodHours", 12);
        report.put("headText", "📝 Report for period {period}h :");
        report.put("conditionText", "⚠️ Count conditionls alert:");
        report.put("alertText", "🚨 Count notifies sended:");
        report.put("needSend", false);
        return report;
    }

    bool NotifyManager::Report::parseSettings(const boost::property_tree::ptree &report)
    {
        periodHours   = report.get("periodHours", periodHours);
        headText      = report.get("headText", headText);
        conditionText = report.get("conditionText", conditionText);
        alertText     = report.get("alertText", alertText);
        needSend      = report.get("needSend", needSend);
        size_t pos    = headText.find("{period}");
        if (pos != std::string::npos) headText.replace(pos, 8, std::to_string(periodHours));
        last_sended_report = std::chrono::steady_clock::now() - std::chrono::hours(periodHours);
        return true;
    }

    void NotifyManager::reporter()
    {
        if (!report.needSend) return;
        G_LOG(50, "Report check");
        if (std::chrono::steady_clock::now() - report.last_sended_report < std::chrono::hours(report.periodHours))
            return;
        report.last_sended_report = std::chrono::steady_clock::now();
        std::string conditions = "", alerts = "";
        for (auto &alert : notifiers) {
            alerts += "\n        " + std::to_string(*alert.second.alert_count_in_period) + " : " + alert.second.metric +
                      " " + alert.second.condition.tostring();
            alert.second.alert_count_in_period->exchange();
        }
        for (auto &[metric, condition_count] : alerts_count)
            if (condition_count.second) {
                conditions += "\n        " + std::to_string(condition_count.second) + " : " + metric->toString(false);
                condition_count.second = 0;
            }
        auto report_text =
            report.headText + "\n" + report.alertText + alerts + "\n" + report.conditionText + conditions;
        for (auto &provider : alert_providers) provider->alert(report_text);
        G_LOG(50, "Report:" << report_text);
    }

}