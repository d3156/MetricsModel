#pragma once
#include "Metrics.hpp"
#include <boost/asio/io_context.hpp>
#include <set>

namespace Metrics {

class Uploader {
public:
  boost::asio::io_context* io;
  Uploader();
  virtual void upload(std::set<Metrics::Metric *> &statistics) = 0;
};

} // namespace Metrics