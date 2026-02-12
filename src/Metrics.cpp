#include "Metrics.hpp"
#include "MetricsModel.hpp"
#include "iostream"
#include <PluginCore/Logger/Log.hpp>
namespace Metrics
{

    Metric::Metric(const std::string &name_, const std::vector<Tag> &tags_) : tags(tags_), name(name_)
    {
        if (MetricsModel::instance()) {
            parent = MetricsModel::instance();
            std::lock_guard<std::mutex> lock(parent->statistics_mutex_);
            parent->metrics_.insert(this);
            for (int i = 0; i < tags.size(); i++) {
                metrics_key += tags[i].first + "=" + tags[i].second;
                if (i != tags.size() - 1) metrics_key += ", ";
            }
            G_LOG(1, "Created metric :" << metrics_key);
        } else {
            R_LOG(1, "MetricsModel::instance() is null! Can't register metrics " << name_);
            R_LOG(1, "All plugins, used MetricsModel must load it in register model and initialisate "
                     "MetricsModel::instance()!!!");
        }
    }

    Metric::~Metric()
    {
        if (parent) {
            std::lock_guard<std::mutex> lock(parent->statistics_mutex_);
            parent->metrics_.erase(this);
        }
    }

    std::string Metric::toString(bool with_value) const
    {
        return with_value ? metrics_key + ": " + std::to_string(value_) : metrics_key;
    }

    Bool::Bool(const std::string &name, const std::vector<Tag> &tags) : Metric(name, tags) {}

    Counter::Counter(const std::string &name, const std::vector<Tag> &tags) : Metric(name + "_counter", tags) {}

    Gauge::Gauge(const std::string &name, const std::vector<Tag> &tags) : Metric(name + "_gauge", tags) {}

    CounterGauge::CounterGauge(const std::string &name, const std::vector<Tag> &tags)
        : counter_(name, tags), gauge_(name, tags)
    {
    }

    MetricGuard::~MetricGuard()
    {
        if (gauge) (*gauge)--;
    }

    MetricGuard::MetricGuard(CounterGauge &parent_)
    {
        parent_++;
        gauge   = &parent_.gauge_;
        counter = &parent_.counter_;
    }

    MetricGuard::MetricGuard(Gauge &parent_)
    {
        parent_++;
        gauge = &parent_;
    }

    Gauge &Gauge::operator=(size_t val)
    {
        value_ = val;
        return *this;
    }

    Gauge &Gauge::operator--(int)
    {
        if (value_) value_--;
        return *this;
    }

    Gauge &Gauge::operator-=(size_t val)
    {
        if (value_ > val)
            value_ -= val;
        else
            value_ = 0;
        return *this;
    }

    Gauge &Gauge::operator++(int)
    {
        value_++;
        return *this;
    }

    Gauge &Gauge::operator+=(size_t val)
    {
        value_ += val;
        return *this;
    }

    Gauge::~Gauge()
    {
        if (value_) R_LOG(1, "[Metrics::Gauge]" << name << " in destructor value was't zero. Metric = " << value_);
    }

    Counter &Counter::operator++(int)
    {
        value_++;
        return *this;
    }

    Counter &Counter::operator+=(size_t val)
    {
        value_ += val;
        return *this;
    }

    CounterGauge &CounterGauge::operator--(int)
    {
        gauge_--;
        return *this; // возвращаем ссылку на текущий объект
    }

    CounterGauge &CounterGauge::operator-=(size_t val)
    {
        gauge_ -= val;
        return *this; // возвращаем ссылку на текущий объект
    }

    CounterGauge &CounterGauge::operator++(int)
    {
        counter_++;
        gauge_++;
        return *this; // возвращаем ссылку на текущий объект
    }

    CounterGauge &CounterGauge::operator+=(size_t val)
    {
        counter_ += val;
        gauge_ += val;
        return *this; // возвращаем ссылку на текущий объект
    }

    Bool &Bool::operator=(bool val)
    {
        value_ = val ? 1 : 0;
        return *this;
    }

    Bool::operator bool() const { return value_; }

    Gauge::operator size_t() const { return value_; }

    Counter::operator size_t() const { return value_; }

    Counter &CounterGauge::getCounter() { return counter_; }

    Gauge &CounterGauge::getGauge() { return gauge_; }

    size_t Counter::exchange(size_t val)
    {
        auto tmp = value_;
        value_   = val;
        return tmp;
    }
} // namespace Metrics