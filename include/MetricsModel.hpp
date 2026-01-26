#pragma once
#include "MetricUploader.hpp"
#include "Metrics.hpp"
#include <PluginCore/IModel.hpp>
#include <atomic>
#include <boost/asio/io_context.hpp>
#include <chrono>
#include <set>
#include <boost/thread.hpp>
#include <boost/asio.hpp>

/*
get in registerModels:
    MetricsModel::instance() = RegisterModel("MetricsModel", new MetricsModel(), MetricsModel);
*/

class MetricsModel final : public d3156::PluginCore::IModel
{
    friend class Metrics::Metric;

public:
    d3156::PluginCore::model_name name() override { return "MetricsModel"; }

    /// Service interface
    int deleteOrder() override { return 10000; }

    void init() override;

    void postInit() override;

    void registerArgs(d3156::Args::Builder &bldr) override;

    void registerUploader(Metrics::Uploader *uploader);

    void unregisterUploader(Metrics::Uploader *uploader);
    /// Service interface

    static MetricsModel *&instance();

    virtual ~MetricsModel();

    void parseSettings();

private:
    std::string configPath       = "./configs/MetricsModel.json";

    std::chrono::seconds statisticInterval        = std::chrono::seconds(5);
    boost::chrono::milliseconds stopThreadTimeout = boost::chrono::milliseconds(200);

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
};
