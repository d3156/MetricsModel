#include "Metrics.hpp"
#include "MetricsModel.hpp"
#include "iostream"
namespace Metrics
{

    Metric::Metric(const std::string &name_, const std::vector<Tag> &tags_) : tags(tags_), name(name_)
    {
        if (MetricsModel::instance()) {
            parent = MetricsModel::instance();
            std::lock_guard<std::mutex> lock(parent->statistics_mutex_);
            parent->metrics_.insert(this);
        } else
            std::cout << R_MetricsModel << "MetricsModel::instance() is null! Can't register metrics " << name_ << "\n"
                      << R_MetricsModel
                      << "All plugins, used MetricsModel must load it in register model and initialisate "
                         "MetricsModel::instance()!!!";
    }

    Metric::~Metric()
    {
        if (parent) {
            std::lock_guard<std::mutex> lock(parent->statistics_mutex_);
            parent->metrics_.erase(this);
        }
    }

    std::string Metric::toString() const { return "\t" + name + "\t: " + std::to_string(value_) + "\n"; }

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
        if (value_) {
            std::cout << "[Metrics::Gauge]" << name << " in destructor value was't zero. Metric = " << value_;
        }
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

} // namespace Metrics