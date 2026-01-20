#include "MetricsModel.hpp"
#include <exception>
#include <unistd.h>
#include <sys/prctl.h>

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

MetricsModel::~MetricsModel()
{
    try {
        io_guard.reset();
        upload_timer_.cancel();
        if (!thread_.joinable()) return;
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
    } catch (std::exception &e) {
        std::cout << R_MetricsModel << "Exception throwed in exit: " << e.what() << std::endl;
    }
}

void MetricsModel::timer_handler(const boost::system::error_code &ec)
{
    if (ec && ec != boost::asio::error::operation_aborted) std::cout << R_MetricsModel << ec.message() << std::endl;
    if (ec || !io_guard.owns_work()) return;
    try {
        std::lock_guard<std::mutex> lock(statistics_mutex_);
        for (auto &uploader : uploaders_)
            if (uploader) uploader->upload(metrics_);
    } catch (std::exception &e) {
        std::cout << R_MetricsModel << "Exception throwed in timer_handler: " << e.what() << std::endl;
    }
    if (!io_guard.owns_work()) return;
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
