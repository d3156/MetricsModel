#pragma once
#include <atomic>
#include <cstddef>
#include <string>
#include <termios.h>
#include <vector>

class MetricsModel;
namespace Metrics
{

    using Tag = std::pair<std::string, std::string>;

    class Metric
    {
        friend class ::MetricsModel;
        MetricsModel *parent = nullptr;
        std::string metrics_key;

    public:
        Metric(const std::string &name, const std::vector<Tag> &tags = {});
        std::string toString(bool with_value = true) const;
        virtual ~Metric();
        size_t value_ = 0;
        std::vector<Tag> tags;
        std::string name;
        bool imported = false; // Для метрик, импортированных из другого хранилища метрик
    };

    class Bool : protected Metric
    {
    public:
        Bool(const std::string &name, const std::vector<Tag> &tags = {});

        Bool &operator=(bool val);
        operator bool() const;
    };

    class Counter : protected Metric
    {
    public:
        Counter(const std::string &name, const std::vector<Tag> &tags = {});

        Counter &operator++(int);
        Counter &operator+=(size_t val);
        size_t exchange(size_t val = 0);
        operator size_t() const;
    };

    class Gauge : protected Metric
    {
    public:
        Gauge(const std::string &name, const std::vector<Tag> &tags = {});
        ~Gauge();

        Gauge &operator=(size_t val);
        Gauge &operator--(int);
        Gauge &operator-=(size_t val);
        Gauge &operator++(int);
        Gauge &operator+=(size_t val);
        operator size_t() const;

    protected:
        friend class Guard;
    };

    class CounterGauge
    {
    public:
        CounterGauge(const std::string &name, const std::vector<Tag> &tags = {});

        CounterGauge &operator--(int);
        CounterGauge &operator-=(size_t val);
        CounterGauge &operator++(int);
        CounterGauge &operator+=(size_t val);

        Counter &getCounter();
        Gauge &getGauge();

    protected:
        friend class MetricGuard;
        Counter counter_;
        Gauge gauge_;
    };

    class MetricGuard
    {
        std::atomic<Metrics::Gauge *> gauge     = nullptr;
        std::atomic<Metrics::Counter *> counter = nullptr;
        friend class Gauge;

    public:
        MetricGuard(Gauge &parent_);
        MetricGuard(CounterGauge &parent_);
        ~MetricGuard();
    };

} // namespace Metrics
