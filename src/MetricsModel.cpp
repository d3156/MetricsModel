#include "MetricsModel.hpp"
#include "NotifierSystem.hpp"
#include <PluginCore/Logger/Log>
#include <sys/prctl.h>
#include <chrono>

void MetricsModel::unregisterUploader(Metrics::Uploader *uploader)
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    uploaders_.erase(uploader);
}

void MetricsModel::registerUploader(Metrics::Uploader *uploader)
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    uploaders_.insert(uploader);
    uploader->io = &io_;
}

void MetricsModel::run()
{
    prctl(PR_SET_NAME, "MetricsModel", 0, 0, 0);
    upload_timer_.expires_after(std::chrono::seconds(config.statisticInterval.value));
    upload_timer_.async_wait(std::bind(&MetricsModel::timer_handler, this, std::placeholders::_1));
    io_.run();
}

void MetricsModel::registerArgs(d3156::Args::Builder &bldr) { bldr.setVersion(FULL_NAME); }

MetricsModel::~MetricsModel()
{
    try {
        stopToken = true;
        io_guard.reset();
        G_LOG(1, "Io-context guard canceled");
        upload_timer_.cancel();
        G_LOG(1, "Metrics timer canceled");
        if (!thread_.joinable()) return;
        G_LOG(1, "Thread joinable, try join in " << config.stopThreadTimeout << " milliseconds");
        if (thread_.timed_join(boost::chrono::milliseconds(config.stopThreadTimeout.value))) return;
        Y_LOG(1, "Metrics upload thread was not terminated, attempting to force stop io_context...");
        io_.stop();
        if (thread_.timed_join(boost::chrono::milliseconds(config.stopThreadTimeout.value))) {
            G_LOG(1, "io_context force stopped successfully");
            return;
        }
        R_LOG(1, "WARNING: Metrics upload thread cannot be stopped. Thread will be detached (potential resource leak)");
        thread_.detach();
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        for (auto i : metrics_) i->parent = nullptr;
    } catch (std::exception &e) {
        R_LOG(1, "Exception throwed in exit: " << e.what());
    }
}

void MetricsModel::timer_handler(const boost::system::error_code &ec)
{
    if (ec && ec != boost::asio::error::operation_aborted) R_LOG(1, ec.message());
    if (ec || stopToken) return;
    try {
        {
            std::lock_guard<std::mutex> lock(statistics_mutex_);
            for (auto &uploader : uploaders_)
                if (uploader) uploader->upload(metrics_);
        }
        notifier_manager.upload(metrics_);
    } catch (std::exception &e) {
        R_LOG(1, "Exception throwed in timer_handler: " << e.what());
    }
    if (stopToken) return;
    upload_timer_.expires_after(std::chrono::seconds(config.statisticInterval.value));
    upload_timer_.async_wait(std::bind(&MetricsModel::timer_handler, this, std::placeholders::_1));
}

void MetricsModel::postInit()
{
    notifier_manager.init();
    thread_ = boost::thread([this]() { this->run(); });
}

void MetricsModel::init() {
    instance() = this;
}

MetricsModel *&MetricsModel::instance()
{
    static MetricsModel *static_instance = nullptr;
    return static_instance;
}

boost::asio::io_context &MetricsModel::getIO() { return io_; }

void MetricsModel::registerAlertProvider(NotifierSystem::NotifierProvider *alert_provider)
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    notifier_manager.alert_providers.insert(alert_provider);
}

void MetricsModel::unregisterAlertProvider(NotifierSystem::NotifierProvider *alert_provider)
{
    std::lock_guard<std::mutex> lock(statistics_mutex_);
    notifier_manager.alert_providers.erase(alert_provider);
}

std::string MetricsModel::name() { return FULL_NAME; }
