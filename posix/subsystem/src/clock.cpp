
#include <async/jump.hpp>
#include <protocols/mbus/client.hpp>

#include "clock.hpp"
#include "clock.pb.h"

namespace clk {

namespace {

async::jump foundTracker;

helix::UniqueLane trackerLane;
helix::UniqueDescriptor globalTrackerPageMemory;

COFIBER_ROUTINE(cofiber::no_future, fetchTrackerPage(), ([=] {
	helix::Offer offer;
	helix::SendBuffer send_req;
	helix::RecvInline recv_resp;
	helix::PullDescriptor pull_memory;

	managarm::clock::CntRequest req;
	req.set_req_type(managarm::clock::CntReqType::ACCESS_PAGE);

	auto ser = req.SerializeAsString();
	auto &&transmit = helix::submitAsync(trackerLane, helix::Dispatcher::global(),
			helix::action(&offer, kHelItemAncillary),
			helix::action(&send_req, ser.data(), ser.size(), kHelItemChain),
			helix::action(&recv_resp, kHelItemChain),
			helix::action(&pull_memory));
	COFIBER_AWAIT transmit.async_wait();
	HEL_CHECK(offer.error());
	HEL_CHECK(send_req.error());
	HEL_CHECK(recv_resp.error());
	HEL_CHECK(pull_memory.error());

	managarm::clock::SvrResponse resp;
	resp.ParseFromArray(recv_resp.data(), recv_resp.length());
	assert(resp.error() == managarm::clock::Error::SUCCESS);
	globalTrackerPageMemory = pull_memory.descriptor();
	
	foundTracker.trigger();
}))

} // anonymous namespace

helix::BorrowedDescriptor trackerPageMemory() {
	return globalTrackerPageMemory;
}

COFIBER_ROUTINE(async::result<void>, enumerateTracker(), ([] {
	auto root = COFIBER_AWAIT mbus::Instance::global().getRoot();

	auto filter = mbus::Conjunction({
		mbus::EqualsFilter("class", "clocktracker")
	});
	
	auto handler = mbus::ObserverHandler{}
	.withAttach([] (mbus::Entity entity, mbus::Properties properties) {
		std::cout << "POSIX: Found clocktracker" << std::endl;

		trackerLane = helix::UniqueLane(COFIBER_AWAIT entity.bind());
		fetchTrackerPage();
	});

	COFIBER_AWAIT root.linkObserver(std::move(filter), std::move(handler));
	COFIBER_AWAIT foundTracker.async_wait();
	COFIBER_RETURN();
}))

} // namespace clk