#include "MetricsModel.hpp"
#include "NotifierSystem.hpp"
#include <PluginCore/Logger/Log.hpp>
#include <exception>
#include <unistd.h>
#include <sys/prctl.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <filesystem>

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
    upload_timer_.expires_after(statisticInterval);
    upload_timer_.async_wait(std::bind(&MetricsModel::timer_handler, this, std::placeholders::_1));
    io_.run();
}

void MetricsModel::registerArgs(d3156::Args::Builder &bldr)
{
    bldr.setVersion(FULL_NAME).addOption(configPath, "MetricsModelPath", "path to config for MetricsModel.json");
}

MetricsModel::~MetricsModel()
{
    try {
        stopToken = true;
        io_guard.reset();
        G_LOG(1, "Io-context guard canceled");
        upload_timer_.cancel();
        G_LOG(1, "Metrics timer canceled");
        if (!thread_.joinable()) return;
        G_LOG(1, "Thread joinable, try join in " << stopThreadTimeout.count() << " milliseconds");
        if (thread_.timed_join(stopThreadTimeout)) return;
        Y_LOG(1, "Metrics upload thread was not terminated, attempting to force stop io_context...");
        io_.stop();
        if (thread_.timed_join(stopThreadTimeout)) {
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
    upload_timer_.expires_after(statisticInterval);
    upload_timer_.async_wait(std::bind(&MetricsModel::timer_handler, this, std::placeholders::_1));
}

void MetricsModel::postInit()
{
    parseSettings();
    thread_ = boost::thread([this]() { this->run(); });
}

void MetricsModel::init() {}

MetricsModel *&MetricsModel::instance()
{
    static MetricsModel *static_instance = nullptr;
    return static_instance;
}

using boost::property_tree::ptree;

namespace fs = std::filesystem;

void MetricsModel::parseSettings()
{
    if (!fs::exists(configPath)) {
        Y_LOG(1, "Config file " << configPath << " not found. Creating default config...");

        fs::create_directories(fs::path(configPath).parent_path());

        ptree pt;
        pt.put("statisticInterval", statisticInterval.count());
        pt.put("stopThreadTimeout", stopThreadTimeout.count());
        pt.add_child("notifiers", NotifierSystem::NotifyManager::getDefault());
        pt.add_child("report", NotifierSystem::NotifyManager::Report::getDefault());
        boost::property_tree::write_json(configPath, pt);

        G_LOG(1, " Default config created at " << configPath);
        return;
    }
    try {
        ptree pt;
        read_json(configPath, pt);

        statisticInterval = std::chrono::seconds(pt.get<std::uint32_t>("statisticInterval"));
        stopThreadTimeout = boost::chrono::milliseconds(pt.get<std::uint32_t>("stopThreadTimeout"));
        if (!notifier_manager.parseSettings(pt.get_child("notifiers", ptree{}))) {
            R_LOG(1, "Error on load notifiers from config " << configPath);
            return;
        };
        if (!notifier_manager.report.parseSettings(pt.get_child("report", ptree{}))) {
            R_LOG(1, "Error on load report from config " << configPath);
            return;
        };
        G_LOG(1, "Config loaded from " << configPath);
    } catch (std::exception e) {
        R_LOG(1, "Error on load config " << configPath << " " << e.what());
    }
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
