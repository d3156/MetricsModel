#include "NotifierSystem.hpp"
#include <boost/property_tree/ptree.hpp>
#include <PluginCore/Logger/Log.hpp>
#include <string>

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
                Notify notify;
                notify.metric              = n.second.get<std::string>("metric", "");
                notify.alertStartMessage   = n.second.get<std::string>("alertStartMessage", " ");
                notify.alertStoppedMessage = n.second.get<std::string>("alertStoppedMessage", "");

                if (notify.metric.empty()) continue;
                notify.condition = parse_condition(n.second.get<std::string>("condition", ""));
                LOG(5, notify.condition.tostring());
                if (notify.condition.type == ConditionType::Error) {
                    R_LOG(1, " invalid condition in notifier for metric " << n.second.get<std::string>("metric", ""));
                    continue;
                }
                notify.condition.delta_mode = n.second.get<bool>("delta_mode", false);
                notify.alert_count          = std::max<size_t>(1, n.second.get<size_t>("alert_count", 1));
                for (auto &t : n.second.get_child("tags", ptree{}))
                    notify.tags.insert(t.second.get_value<std::string>());
                notifiers[notify.metric] = std::move(notify);
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
        notifier.add_child("tags", tags);
        notifier.put("alertStartMessage", "Alert! {metric}:{value} {tags}");
        notifier.put("alertStoppedMessage", "Alert stopped! {metric}:{value} {tags}");
        notifiers.push_back(std::make_pair("", notifier));
        return notifiers;
    }

    void NotifyManager::upload(std::set<Metrics::Metric *> &statistics)
    {
        if (!alert_providers.empty()) return;
        std::vector<std::string> alerts;
        if (alerts_count.size() == 0)
            for (auto metric : statistics) alerts_count[metric] = 0;
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
            size_t &current_count = alerts_count[metric];

            if (check_condition(notifier->second.condition, metric->value_)) {
                current_count++;

                Y_LOG(100, "condition checked: " << notifier->second.condition.tostring() << "alert count "
                                                 << current_count);
                if (current_count == notifier->second.alert_count) {
                    alerts.emplace_back(
                        notifier->second.formatAlertMessage(notifier->second.alertStartMessage, metric));
                    notifier->second.start_ = std::chrono::steady_clock::now();
                }
            } else {
                if (current_count >= notifier->second.alert_count) {
                    alerts.emplace_back(
                        notifier->second.formatAlertMessage(notifier->second.alertStoppedMessage, metric));
                }
                current_count = 0;
            }
        }
        for (auto &alert : alerts)
            for (auto &provider : alert_providers) provider->alert(alert);
    }
}