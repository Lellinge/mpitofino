#include <stdexcept>
#include "common/packet_headers.h"
#include "common/simple_types.h"
#include "common/com_utils.h"
#include "distributed_control_plane_agent.h"

#include <cstring>
#include <pstl/execution_defs.h>
#include <sys/socket.h>

#include "../build/sd/proto_out/common.pb.h"
#include "../common/packet_headers.h"

extern "C" {
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
}

using namespace std;


namespace distributed_control_plane_agent {

	
Client::Client(WrappedFD&& wfd, const struct sockaddr_in& addr)
	: wfd(move(wfd)), addr(addr)
{
}


Agent::Agent(
		StateRepository& st_repo, Epoll& epoll)
	:
		st_repo(st_repo), epoll(epoll)
{
	initialize_client_interface();
	if (not st_repo.is_root_switch()) {
		initialize_parent_interface();
	}
}


void Agent::initialize_client_interface()
{
	WrappedFD wfd;
	wfd.set_errno(socket(AF_INET, SOCK_STREAM, 0), "socket(tcp)");

	/* Set SO_REUSEADDR */
	int reuseaddr = 1;
	check_syscall(
		setsockopt(wfd.get_fd(), SOL_SOCKET, SO_REUSEADDR,
				   &reuseaddr, sizeof(reuseaddr)),
		"setsockopt");

	/* Bind and listen */
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(TCP_PORT_CTRL_COLL)
	};
	addr.sin_addr.s_addr = INADDR_ANY;

	check_syscall(
		bind(wfd.get_fd(), (struct sockaddr*) &addr, sizeof(addr)),
		"bind(tcp)");

	check_syscall(listen(wfd.get_fd(), 10), "listen(tcp)");

	/* Add to epoll instance */
	epoll.add_fd(wfd.get_fd(), EPOLLIN,
				 bind(&Agent::on_new_client, this,
					 placeholders::_1, placeholders::_2));

	client_listen_wfd = move(wfd);
/*
	WrappedFD broadcast_read_fd;
	broadcast_read_fd.set_errno(socket(AF_INET, SOCK_DGRAM, 0), "socket(udp) for listening to discovery broadcasts");

	struct sockaddr_in discovery_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(UDP_PORT_TDP)
	};

	discovery_addr.sin_addr.s_addr = INADDR_ANY;

	check_syscall(bind(broadcast_read_fd.get_fd(), (struct sockaddr*)&discovery_addr, sizeof(discovery_addr)),
		"bind(udp for the listening to discovery)");

	broadcast_read_fd = move(broadcast_read_fd);

	epoll.add_fd(this->switch_discovery_listener.get_fd(),
		EPOLLIN,
		bind(&Agent::, this, placeholders::_1, placeholders::_2));*/
}

void Agent::initialize_parent_interface() {
	WrappedFD wfd;
	wfd.set_errno(socket(AF_INET, SOCK_STREAM, 0), "socket(control plane)");

	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(TCP_PORT_CTRL_COLL)
	};

	memcpy(&addr.sin_addr.s_addr, st_repo.get_parent_control_ip_ptr(), sizeof(st_repo.get_parent_control_ip()));

	check_syscall(
		connect(
			wfd.get_fd(),
			(struct sockaddr*)&addr,
			sizeof(addr)), "connect(other switch)");

	epoll.add_fd(wfd.get_fd(), EPOLLIN | EPOLLHUP | EPOLLRDHUP,
		bind(&Agent::on_parent_fd, this, placeholders::_1, placeholders::_2));

	parent.wfd = move(wfd);

}

void Agent::on_parent_fd(int fd, uint32_t events) {
	// this is mostly based on NodeDaemon::on_switch_fd and Agent::on_client_fd
	bool disconnect = events & (EPOLLHUP | EPOLLRDHUP);

	if (events & EPOLLIN) {

	}
}


void Agent::on_new_client(int, uint32_t)
{
	WrappedFD wfd;
	struct sockaddr_in addr{};
	socklen_t addrlen = sizeof(addr);
	
	wfd.set_errno(
		accept4(client_listen_wfd.get_fd(), (struct sockaddr*) &addr, &addrlen, 0),
		"accept4(client)");

	printf("New client connected from %s\n", to_string(addr).c_str());
	clients.emplace_back(move(wfd), addr);

	/* Add fd to epoll instance */
	try
	{
		epoll.add_fd(clients.back().wfd.get_fd(),
					 EPOLLIN | EPOLLHUP | EPOLLRDHUP,
					 bind(&Agent::on_client_fd, this, &clients.back(),
						 placeholders::_1, placeholders::_2));
	}
	catch (...)
	{
		clients.pop_back();
		throw;
	}
}


void Agent::on_client_fd(Client* client, int fd, uint32_t events)
{
	bool disconnect = events & (EPOLLHUP | EPOLLRDHUP);


	if (events & EPOLLIN)
	{
		/* Receive message */
		/* IMPROVE: This blocks if not all data of the message has
		been received yet. Maybe use a receive buffer if this becomes
		a bottleneck. */
		auto msg = recv_protobuf_message_simple_stream<proto::ctrl_sd::NdRequest>(fd);
		if (msg)
		{
			switch (msg->messages_case())
			{
			case proto::ctrl_sd::NdRequest::kGetChannel:
				on_client_get_channel(client, msg->get_channel());
				break;

			case proto::ctrl_sd::NdRequest::kUnrefChannel:
				on_client_unref_channel(client, msg->unref_channel());
				break;
				case proto::ctrl_sd::NdRequest::kGetChannelS2S:
				std::cout << "recieved a getchannelS2S message" << std::endl;
				on_client_get_channel_s2s(client, msg->get_channel_s2s());
				break;

			default:
				throw runtime_error("Unsupported message from client");
			}
		}
		else
		{
			disconnect = true;
		}
	}


	if (disconnect)
	{
		auto i = find_if(clients.begin(), clients.end(),
						 [=](auto& c){ return &c == client; });

		if (i != clients.end())
		{
			printf("Disconnecting client from %s\n",
				   to_string(client->addr).c_str());

			epoll.remove_fd(client->wfd.get_fd());

			/* Check if any channels need to be removed */
			set<uint64_t> channels = move(client->channels);
			check_remove_channels(channels);
			
			clients.erase(i);
		}
	}
}


void Agent::on_client_get_channel(Client* client, const proto::ctrl_sd::GetChannel& msg)
{
	/* Check if the channel exists already */
	auto ch = st_repo.get_channel(msg.tag());

	/* If not, allocate it */
	if (!ch)
	{
		CollectiveChannel c;
		if (not st_repo.is_root_switch()) {
			// TODO actually implement this stuff.
			c.is_root = true;
			c.upstream_port = st_repo.get_upstream_port();
			// TODO talk to the the parent
			parent_create_channel(msg);
		}

		c.tag = msg.tag();
		c.fabric_ip = st_repo.get_collectives_module_ip_addr();
		c.fabric_qp_common = st_repo.get_free_coll_qp_common();
		c.fabric_mac = st_repo.get_collectives_module_mac_addr();

		c.agg_unit = st_repo.get_free_agg_unit();

		for (auto cid : msg.agg_group_client_ids())
		{
			CollectiveChannel::Participant p;
			p.client_id = cid;
			c.participants.insert({cid, p});
		}

		c.type = msg.type();

		st_repo.add_channel(c);
		ch = st_repo.get_channel(c.tag);


		/* Clear any pending responses */
		auto pi = pending_get_channel_responses.find(ch->tag);
		if (pi != pending_get_channel_responses.end())
			pending_get_channel_responses.erase(pi);
	}

	/* Declare ownership */
	client->channels.insert(ch->tag);

	/* Update client parameters */
	auto _client_ip = msg.client_ip();
	auto client_mac = msg.client_mac();

	if (msg.switch_port() < 0 || msg.switch_port() >= 128*4)
		throw runtime_error("Switch port received from client out of range");

	auto client_ip = *reinterpret_cast<IPv4Addr*>(&_client_ip);
	auto fabric_qp = get_next_fabric_qp(ch, client_ip);

	st_repo.update_channel_participant(
		msg.tag(), msg.client_id(),
		client_ip, msg.client_qp(),
		*reinterpret_cast<MacAddr*>(&client_mac), msg.switch_port(),
		fabric_qp);

	/* Return channel parameters. Note that the ASIC has already been
	updated synchronously during the st_repo calls above. */
	proto::ctrl_sd::GetChannelResponse resp;
	resp.set_client_id(msg.client_id());
	resp.set_tag(ch->tag);
	resp.set_fabric_ip(*reinterpret_cast<const uint32_t*>(&ch->fabric_ip));
	resp.set_fabric_qp(fabric_qp);

	/* Hold reply until all clients are known s.t. the ASIC has been
	fully configured to handle all incoming datagrams correctly
	(i.e. all full-bitmaps are set correctly, which requires knowledge
	about the nodes' switch ports. IMPROVE this could be done
	immediately with the TM once implemented.) */
	auto [pi,p_inserted] = pending_get_channel_responses.try_emplace(ch->tag);
	auto& pm = pi->second;
	pm.push_back({client, resp});

	/* Check if all clients have responded and if yes, forward replies */
	if (pm.size() == ch->participants.size())
	{
		for (auto& [pc, presp] : pm)
			send_protobuf_message_simple_stream(pc->wfd.get_fd(), presp);

		pending_get_channel_responses.erase(pi);
	}
}

void Agent::on_client_get_channel_s2s(Client *client, const proto::ctrl_sd::GetChannelS2S &msg) {
	// TODO which parts are not necessary for this?
	auto ch = st_repo.get_channel(msg.tag());

	if (!ch) {
		CollectiveChannel c;
		if (st_repo.is_root_switch()) {
			// TODO implement this
		}
		// TODO get and set all the other stuff. Even if this client might not need it, we need to be able to mix nodes and S2S

		c.tag = msg.tag();
		//c.fabric_ip = st_repo.get_collectives_module_ip_addr();
		c.fabric_ip = IPv4Addr("10.10.127.2");
		c.fabric_qp_common = st_repo.get_free_coll_qp_common();
		c.fabric_mac = st_repo.get_collectives_module_mac_addr();

		c.agg_unit = st_repo.get_free_agg_unit();

		for (auto cid : msg.agg_group_client_ids()) {
			CollectiveChannel::Participant p;
			p.is_s2s = true;
			p.client_id = cid;
			c.participants.insert( {cid, p});
		}
		// TODO right now only allreduce_int32 is supported anyway, so this is fine but this needs to be refactored longterm
		c.type = ALLREDUCE_INT32;

		st_repo.add_channel(c);
		ch = st_repo.get_channel(c.tag);

		auto pi = pending_get_channel_responses.find(ch->tag);
		if (pi != pending_get_channel_responses.end()) {
			pending_get_channel_responses.erase(pi);
		}
	}

	client->channels.insert(ch->tag);

	auto _client_ip = msg.client_ip();
	auto client_mac = msg.client_mac();

	if (msg.switch_port() < 0 || msg.switch_port() >= 128 * 4) {
		throw runtime_error("Switch port received from client out of range");
	}

	//auto client_ip = *reinterpret_cast<IPv4Addr*>(&_client_ip);
	auto client_ip = IPv4Addr("10.10.127.1");
	auto fabric_qp = get_next_fabric_qp(ch, client_ip);

	st_repo.update_channel_participant(msg.tag(), msg.client_id(),
		client_ip, msg.client_qp(),
		*reinterpret_cast<MacAddr*>(&client_mac), msg.switch_port(),
		fabric_qp);

	proto::ctrl_sd::GetChannelResponse resp;
	resp.set_client_id(msg.client_id());
	resp.set_tag(ch->tag);
	resp.set_fabric_ip(*reinterpret_cast<const uint32_t*>(&ch->fabric_ip));
	resp.set_fabric_qp(fabric_qp);

	auto [pi, p_inserted] = pending_get_channel_responses.try_emplace(ch->tag);
	auto& pm = pi->second;
	pm.push_back({client, resp});

	if (pm.size() == ch->participants.size()) {
		for (auto& [pc, presp] : pm) {
			send_protobuf_message_simple_stream(pc->wfd.get_fd(), presp);
		}
		pending_get_channel_responses.erase(pi);
	}

}

void Agent::parent_create_channel(const proto::ctrl_sd::GetChannel& msg) {
	// adopted from node_daemon.cc
	proto::ctrl_sd::NdRequest other_switch_req;
	auto mutable_get_s2s = other_switch_req.mutable_get_channel_s2s();
	// TODO this isn't the ideal way to but it should work for now.
	mutable_get_s2s->set_client_id(st_repo.get_switch_id() + 8192);
	// pass throught the tag from the lower levels so that the tag for a specific aggregation is constant
	mutable_get_s2s->set_tag(msg.tag());
	// TODO right now get_channel_s2s does not have a type field, since there is only one valid option. This might change in the future
	// (iirc Sydney is working on some floating point stuff, although I dont know if I that will look different to the switches)
	//mutable_get_s2s->set_type(msg.type());
	uint64_t s2s_src_mac = 0;
	memcpy(&s2s_src_mac, st_repo.get_switch_to_switch_src_mac_ptr(), sizeof(st_repo.get_switch_to_switch_src_mac()));

	mutable_get_s2s->set_client_mac(s2s_src_mac);
	mutable_get_s2s->set_client_qp(msg.client_qp());
	mutable_get_s2s->set_client_ip(msg.client_ip());
	// TODO this is hardcoded. Ideally this would use the results read from the discovery packets, but thats not implemented right now
	mutable_get_s2s->set_switch_port(st_repo.get_s2s_dst_switch_port());

	mutable_get_s2s->add_agg_group_client_ids(st_repo.get_switch_id() + 8192);

	send_protobuf_message_simple_stream(parent.wfd.get_fd(), other_switch_req);

	proto::ctrl_sd::GetChannelResponse reply;
	parent.pending_get_channel_responses.try_emplace({msg.client_id(), msg.tag()}, reply);
}


void Agent::on_client_unref_channel(Client* client, const proto::ctrl_sd::UnrefChannel& msg)
{
	auto i = client->channels.find(msg.tag());
	if (i == client->channels.end())
	{
		fprintf(stderr,
				"WARNING: Client unrefernced channel which it does not own (id: %lu)\n",
				(unsigned long) msg.tag());

		return;
	}
	
	client->channels.erase(i);
	check_remove_channel(msg.tag());
}


void Agent::check_remove_channel(uint64_t tag)
{
	size_t owners = 0;
	for (auto& oc : clients)
	{
		if (oc.channels.find(tag) != oc.channels.end())
			owners++;
	}

	if (owners == 0)
	{
		printf("Removing collective channel with tag %lu\n",
				(unsigned long) tag);

		st_repo.remove_channel(tag);
	}
}

void Agent::check_remove_channels(const set<uint64_t>& channels)
{
	/* Check if any channels need to be removed
	   IMPROVE: use an index instead of O(n**2) search */
	for (auto ch_tag : channels)
		check_remove_channel(ch_tag);
}


uint32_t Agent::get_next_fabric_qp(const CollectiveChannel* ch, IPv4Addr client_ip)
{
	/* IMPROVE: do not rebuild this map on every invocation */
	map<IPv4Addr, uint8_t> cnts;

	/* Count occurances of IPs */
	for (auto& [c,p] : ch->participants)
	{
		if (p.ip.is_0000())
			continue;

		cnts.insert({p.ip, 0}).first->second++;
	}

	auto i = cnts.find(client_ip);
	return ((uint32_t) ch->fabric_qp_common << 8) | (i != cnts.end() ? i->second : 0);
}


}
