#ifndef SSF_SERVICES_COPY_FILE_FIBER_TO_FILE_FIBER_TO_FILE_H_
#define SSF_SERVICES_COPY_FILE_FIBER_TO_FILE_FIBER_TO_FILE_H_

#include <cstdint>

#include <memory>

#include <boost/system/error_code.hpp>

#include <ssf/log/log.h>

#include "core/factories/service_factory.h"
#include "services/admin/requests/create_service_request.h"
#include "services/copy_file/config.h"
#include "services/copy_file/fiber_to_file/fiber_to_ofstream_session.h"

namespace ssf {
namespace services {
namespace copy_file {
namespace fiber_to_file {

template <class Demux>
class FiberToFile : public BaseService<Demux> {
 private:
  using FiberToFilePtr = std::shared_ptr<FiberToFile>;
  using SessionManager = ItemManager<BaseSessionPtr>;
  using Parameters = typename ssf::BaseService<Demux>::Parameters;
  using fiber_port = typename Demux::local_port_type;
  using demux = typename ssf::BaseService<Demux>::demux;
  using fiber = typename ssf::BaseService<Demux>::fiber;
  using endpoint = typename ssf::BaseService<Demux>::endpoint;
  using fiber_acceptor = typename ssf::BaseService<Demux>::fiber_acceptor;

 public:
  enum { kFactoryId = 7, kServicePort = 40 + ((1 << 17) + 1) };

 public:
  // Factory method to create the service
  static FiberToFilePtr Create(boost::asio::io_service& io_service,
                               demux& fiber_demux, Parameters parameters) {
    return FiberToFilePtr(new FiberToFile(io_service, fiber_demux));
  }

  static void RegisterToServiceFactory(
      std::shared_ptr<ServiceFactory<demux>> p_factory, const Config& config) {
    if (!config.enabled()) {
      // service factory is not enabled
      return;
    }

    p_factory->RegisterServiceCreator(kFactoryId, &FiberToFile::Create);
  }

  static ssf::services::admin::CreateServiceRequest<demux> GetCreateRequest() {
    ssf::services::admin::CreateServiceRequest<demux> create_req(kFactoryId);

    return create_req;
  }

 public:
  // Start service and listen new fiber on demux port kServicePort
  void start(boost::system::error_code& ec) override {
    endpoint ep(this->get_demux(), kServicePort);
    SSF_LOG(kLogInfo)
        << "microservice[fiber to file]: start accept on fiber port "
        << kServicePort;
    fiber_acceptor_.bind(ep, ec);
    fiber_acceptor_.listen(boost::asio::socket_base::max_connections, ec);
    if (ec) {
      return;
    }
    StartAccept();
  }

  // Stop service
  void stop(boost::system::error_code& ec) override {
    SSF_LOG(kLogInfo) << "microservice[fiber to file]: stopping";
    manager_.stop_all();
    fiber_acceptor_.close(ec);
    fiber_.close(ec);
  }

  uint32_t service_type_id() override { return kFactoryId; }

 private:
  FiberToFile(boost::asio::io_service& io_service, demux& fiber_demux)
      : BaseService<Demux>(io_service, fiber_demux),
        fiber_acceptor_(io_service),
        fiber_(io_service) {}

  void StartAccept() {
    if (fiber_acceptor_.is_open()) {
      fiber_acceptor_.async_accept(
          fiber_, Then(&FiberToFile::StartDataForwarderSessionHandler,
                       this->SelfFromThis()));
    }
  }

  // Create a session to transmit files for the new connection
  void StartDataForwarderSessionHandler(const boost::system::error_code& ec) {
    if (ec) {
      SSF_LOG(kLogInfo) << "microservice[fiber to file]: fail accept fiber";
      return;
    }

    auto p_session = FiberToOstreamSession<fiber, std::ofstream>::Create(
        &manager_, std::move(fiber_));
    boost::system::error_code start_ec;
    manager_.start(p_session, start_ec);
    StartAccept();
  }

  template <typename Handler, typename This>
  auto Then(Handler handler,
            This me) -> decltype(boost::bind(handler, me->SelfFromThis(), _1)) {
    return boost::bind(handler, me->SelfFromThis(), _1);
  }

  std::shared_ptr<FiberToFile> SelfFromThis() {
    return std::static_pointer_cast<FiberToFile>(this->shared_from_this());
  }

 private:
  bool accept_;
  fiber_acceptor fiber_acceptor_;
  fiber fiber_;
  SessionManager manager_;
};

}  // fiber_to_file
}  // copy_file
}  // services
}  // ssf

#endif  // SSF_SERVICES_COPY_FILE_FIBER_TO_FILE_FIBER_TO_FILE_H_
