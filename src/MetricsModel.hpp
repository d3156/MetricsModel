#pragma once
#include "MetricUploader.hpp"
#include "Metrics.hpp"
#include "NotifierSystem.hpp"
#include <PluginCore/IModel>
#include <boost/thread.hpp>
#include <boost/asio.hpp>

/*
get in registerModels:
    MetricsModel::instance() = models.registerModel<MetricsModel>();
*/

class MetricsModel final : public d3156::PluginCore::IModel
{
    friend class Metrics::Metric;

public:
    /// Service interface
    static std::string name();
    int deleteOrder() override { return 10000; }
    void init() override;
    void postInit() override;
    void registerArgs(d3156::Args::Builder &bldr) override;
    /// Service interface

    void registerUploader(Metrics::Uploader *uploader);

    void unregisterUploader(Metrics::Uploader *uploader);

    void registerAlertProvider(NotifierSystem::NotifierProvider *alert_provider);

    void unregisterAlertProvider(NotifierSystem::NotifierProvider *alert_provider);

    static MetricsModel *&instance();

    virtual ~MetricsModel();

    boost::asio::io_context &getIO();

    struct MetricsConfig : public d3156::Config {
        MetricsConfig() : d3156::Config("") {}
        CONFIG_UINT(statisticInterval, 5);
        CONFIG_UINT(stopThreadTimeout, 200);
    } config;

private:
    boost::thread thread_; /// Метрики будут работать в отдельном потоке, чтобы не
                           /// терять данные при возможном зависании плагинов.
    std::set<Metrics::Uploader *> uploaders_;
    std::set<Metrics::Metric *> metrics_;
    std::mutex statistics_mutex_;

    boost::asio::io_context io_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> io_guard =
        boost::asio::make_work_guard(io_);

    std::atomic<bool> stopToken = false;

    boost::asio::steady_timer upload_timer_ = boost::asio::steady_timer(io_);
    void run();
    void timer_handler(const boost::system::error_code &ec);

    NotifierSystem::NotifyManager notifier_manager = {&config};
};
