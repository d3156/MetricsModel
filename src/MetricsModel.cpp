#include "MetricsModel.hpp"
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
    bldr.setVersion("MetricsModel " + std::string(METRICS_MODEL_VERSION))
        .addOption(configPath, "MetricsModelPath", "path to config for MetricsModel.json");
}

MetricsModel::~MetricsModel()
{
    try {
        stopToken = true;
        io_guard.reset();
        std::cout << G_MetricsModel << "Io-context guard canceled" << std::endl;
        upload_timer_.cancel();
        std::cout << G_MetricsModel << "Metrics timer canceled" << std::endl;
        if (!thread_.joinable()) return;
        std::cout << G_MetricsModel << "Thread joinable, try join in " << stopThreadTimeout.count() << " milliseconds"
                  << std::endl;
        if (thread_.timed_join(stopThreadTimeout)) return;
        std::cout << Y_MetricsModel
                  << "Metrics upload thread was not terminated, attempting to force stop io_context..." << std::endl;
        io_.stop();
        if (thread_.timed_join(stopThreadTimeout)) {
            std::cout << G_MetricsModel << "io_context force stopped successfully" << std::endl;
            return;
        }
        std::cout
            << R_MetricsModel
            << "WARNING: Metrics upload thread cannot be stopped. Thread will be detached (potential resource leak)"
            << std::endl;
        thread_.detach();
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        for (auto i : metrics_) i->parent = nullptr;
    } catch (std::exception &e) {
        std::cout << R_MetricsModel << "Exception throwed in exit: " << e.what() << std::endl;
    }
}

void MetricsModel::timer_handler(const boost::system::error_code &ec)
{
    if (ec && ec != boost::asio::error::operation_aborted) std::cout << R_MetricsModel << ec.message() << std::endl;
    if (ec || stopToken) return;
    try {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        for (auto &uploader : uploaders_)
            if (uploader) uploader->upload(metrics_);
    } catch (std::exception &e) {
        std::cout << R_MetricsModel << "Exception throwed in timer_handler: " << e.what() << std::endl;
    }
    if (stopToken) return;
    upload_timer_.expires_after(statisticInterval);
    upload_timer_.async_wait(std::bind(&MetricsModel::timer_handler, this, std::placeholders::_1));
}

void MetricsModel::postInit()
{
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
        std::cout << Y_MetricsModel << " Config file " << configPath << " not found. Creating default config...\n";

        fs::create_directories(fs::path(configPath).parent_path());

        ptree pt;
        pt.put("statisticInterval", statisticInterval.count());
        pt.put("stopThreadTimeout", stopThreadTimeout);

        boost::property_tree::write_json(configPath, pt);

        std::cout << G_MetricsModel << " Default config created at " << configPath << "\n";
        return;
    }
    try {
        ptree pt;
        read_json(configPath, pt);

        statisticInterval = std::chrono::seconds(pt.get<std::uint32_t>("statisticInterval"));
        stopThreadTimeout = boost::chrono::milliseconds(pt.get<std::uint32_t>("stopThreadTimeout"));
    } catch (std::exception e) {
        std::cout << R_MetricsModel << "error on load config " << configPath << " " << e.what() << std::endl;
    }
}
