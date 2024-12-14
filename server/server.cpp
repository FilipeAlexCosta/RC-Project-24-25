#include "game.hpp"
#include "scoreboard.hpp" // TODO: change

#include <iostream>
#include <ctime>
#include <filesystem>
#include <fcntl.h>
#include <sys/wait.h>
#include <fstream>

#define DEFAULT_PORT "58016"

static bool exit_server = false;

//static ScoreBoard sb; // TODO: change

/* signal(SIGPIPE, SIG_IGN)
 * signal(SIGCHILD, SIG_IGN) ignorar estes 2 sinais
 *
 * select() -> bloqueia na chamada enquanto espera por msgs udp/tcp
 * setsockopt() -> coloca um temporizador na socket (util para UDP)
 *
 * while (n < MAX_RESEND) {
 * 	int ret = recvfrom(...)
 * 	if (ret < 0)
 * 		if (errno == EWOULDBLOCK || errno == EAGAIN)
 * 			timeout => Resend message
 * }
 */

/*struct Game { 
	int player_plid;
	int playtime;
	std::string secret_key;
	std::time_t start_time;
	int trial_num;

	Game(int plid, int playtime, const std::string key)
	 : player_plid(plid), playtime(playtime), secret_key(key),  start_time(std::time(nullptr)), trial_num(0) {};

	bool hasEnded() const {
		auto curr_time = std::time(nullptr);
		auto played_time = std::difftime(curr_time, start_time);
		return played_time >= playtime || trial_num > MAX_TRIALS;
	};
};

static std::unordered_map<int, Game> ongoing_games;

static bool has_ongoing_game(int plid) { // TODO: incorporate this in template ?
	return ongoing_games.find(plid) != ongoing_games.end();
}*/

using udp_action_map = net::action_map<
	net::udp_source,
	const net::udp_connection&,
	const net::other_address&
>;

using tcp_action_map = net::action_map<
	net::tcp_source,
	const net::tcp_connection&
>;

static net::action_status handle_udp(net::udp_connection& udp_conn, const udp_action_map& actions);
static net::action_status handle_tcp(net::tcp_server& tcp_sv, const tcp_action_map& actions);

static net::action_status start_new_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static net::action_status end_game(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static net::action_status start_new_game_debug(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static net::action_status do_try(
	net::stream<net::udp_source>& req,
	const net::udp_connection& udp_conn,
	const net::other_address& client_addr
);

static net::action_status show_trials(
	net::stream<net::tcp_source>& req,
	const net::tcp_connection& tcp_conn
);

/*static net::action_status show_scoreboard(
	net::stream<net::tcp_source>& req,
	const net::tcp_connection& tcp_conn, score_board& sb
);*/

int main() {
	if (game::setup_directories() != 0) {
		std::cout << "Failed to setup the " << DEFAULT_GAME_DIR << " directory.\n";
		std::cout << "Shutting down.\n";
	}
	net::udp_connection udp_conn{{DEFAULT_PORT, SOCK_DGRAM}};
	if (!udp_conn.valid()) {
		std::cout << "Failed to open udp connection at " << DEFAULT_PORT << ".\n";
		return 1;
	}
	net::tcp_server tcp_sv{{DEFAULT_PORT, SOCK_STREAM}};
	if (!tcp_sv.valid()) {
		std::cout << "Failed to open tcp connection at " << DEFAULT_PORT << ".\n";
		return 1;
	}

	std::srand(std::time(nullptr));
	udp_action_map udp_actions;
	udp_actions.add_action("SNG", start_new_game);
	udp_actions.add_action("QUT", end_game);
	udp_actions.add_action("DBG", start_new_game_debug);
	udp_actions.add_action("TRY", do_try);
	tcp_action_map tcp_actions;
	/*tcp_actions.add_action("STR", show_trials);*/
	//tcp_actions.add_action("SSB", show_scoreboard);

	fd_set r_fds;
	int max_fd = udp_conn.get_fildes();
	if (tcp_sv.get_fildes() > max_fd)
		max_fd = tcp_sv.get_fildes();
	while (!exit_server) {
		FD_ZERO(&r_fds);
		FD_SET(udp_conn.get_fildes(), &r_fds);
		FD_SET(tcp_sv.get_fildes(), &r_fds);
		int counter = select(max_fd + 1, &r_fds, nullptr, nullptr, nullptr);
		if (counter <= 0) {
			std::cout << "Failed to select the udp/tcp connection.\n";
			return 1;
		}
		net::action_status status[2] = {net::action_status::OK, net::action_status::OK};
		if (FD_ISSET(udp_conn.get_fildes(), &r_fds))
			status[0] = handle_udp(udp_conn, udp_actions);
		if (FD_ISSET(tcp_sv.get_fildes(), &r_fds))
			status[1] = handle_tcp(tcp_sv, tcp_actions);
		for (auto stat : status)
			if (stat != net::action_status::OK)
				std::cerr << net::status_to_message(stat) << '.' << std::endl;
	}
	//udp_main(DEFAULT_PORT);
//	tcp_main(DEFAULT_PORT);
	return 0;
}

static net::action_status handle_udp(net::udp_connection& udp_conn, const udp_action_map& actions) {
	net::other_address client_addr;
	auto [status, request] = udp_conn.listen(client_addr);
	if (status != net::action_status::OK)
		return status;
	status = actions.execute(request, udp_conn, client_addr);
	if (status == net::action_status::UNK_ACTION) {
		net::out_stream out;
		out.write("ERR").prime();
		return udp_conn.answer(out, client_addr);
	}
	return status;
}

static net::action_status handle_tcp(net::tcp_server& tcp_sv, const tcp_action_map& actions) {
	net::other_address client_addr;
	auto [status, tcp_conn] = tcp_sv.accept_client(client_addr);
	if (status != net::action_status::OK)
		return status;
	// TODO: fork
	net::stream<net::tcp_source> request = tcp_conn.to_stream();
	status = actions.execute(request, tcp_conn);
	if (status == net::action_status::UNK_ACTION) {
		net::out_stream out;
		out.write("ERR").prime();
		return tcp_conn.answer(out);
	}
	return status;
}

static net::action_status start_new_game(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RSG");
	auto [status, fields] = req.read({{4, 6}, {1, 3}});
	if (status != net::action_status::OK || (status = req.check_strict_end()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	
	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto res = game::find_active(fields[0]);
	if (res.first != net::action_status::NOT_IN_GAME) {
		out_strm.write("NOK").prime(); // what to send on server failure?
		std::cout << out_strm.view();
		if (res.first == net::action_status::OK)
			return udp_conn.answer(out_strm, client_addr);
		udp_conn.answer(out_strm, client_addr);
		return res.first;
	}

	res = game::create(fields[0], std::stoul(fields[1]));
	if (res.first != net::action_status::OK) {
		out_strm.write("NOK").prime(); // what to send in case of failure?
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return res.first;
	}
	out_strm.write("OK").prime();
	std::cout << out_strm.view();
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status end_game(net::stream<net::udp_source>& req,
								   const net::udp_connection& udp_conn,
								   const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RQT");
	auto [status, plid] = req.read(6, 6);
	if (status != net::action_status::OK
		|| (status = req.check_strict_end()) != net::action_status::OK
		|| (status = net::is_valid_plid(plid)) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto [gm_status, gm] = game::find_active(plid);
	if (gm_status != net::action_status::OK) {
		out_strm.write("NOK").prime(); // what to send on fs failure?
		std::cout << out_strm.view();
		if (gm_status == net::action_status::NOT_IN_GAME)
			return udp_conn.answer(out_strm, client_addr);
		udp_conn.answer(out_strm, client_addr);
		return gm_status;
	}
	gm_status = gm.quit();
	if (gm_status != net::action_status::OK) {
		out_strm.write("NOK").prime(); // what to send in case of sv failure?
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return gm_status;
	}
	out_strm.write("OK");
	for (int i = 0; i < GUESS_SIZE; i++)
		out_strm.write(gm.secret_key()[i]);
	out_strm.prime();
	std::cout << out_strm.view();
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status start_new_game_debug(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RDB");
	auto [status, fields] = req.read({{4, 6}, {1, 3}});
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	status = net::is_valid_plid(fields[0]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	
	status = net::is_valid_max_playtime(fields[1]);
	if (status != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	char secret_key[GUESS_SIZE];
	for (int i = 0; i< GUESS_SIZE; i++) {
		auto [stat, col] = req.read(1, 1);
		if (stat != net::action_status::OK
			|| (stat = net::is_valid_color(col)) != net::action_status::OK) {
			status = stat;
			break;
		}
		secret_key[i] = col[0];
	}
	if ((status != net::action_status::OK)
		|| (status = req.check_strict_end()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto res = game::find_active(fields[0]);
	if (res.first != net::action_status::NOT_IN_GAME) {
		out_strm.write("NOK").prime(); // what to send on server failure?
		std::cout << out_strm.view();
		if (res.first == net::action_status::OK)
			return udp_conn.answer(out_strm, client_addr);
		udp_conn.answer(out_strm, client_addr);
		return res.first;
	}

	res = game::create(fields[0], std::stoul(fields[1]), secret_key);
	if (res.first != net::action_status::OK) {
		out_strm.write("NOK").prime(); // what to send in case of failure?
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return res.first;
	}
	out_strm.write("OK").prime();
	std::cout << out_strm.view();
	return udp_conn.answer(out_strm, client_addr);
}

static net::action_status do_try(net::stream<net::udp_source>& req,
										 const net::udp_connection& udp_conn,
										 const net::other_address& client_addr) {
	net::out_stream out_strm;
	out_strm.write("RTR");
	auto res = req.read(6, 6);
	if (res.first != net::action_status::OK
		|| (res.first = net::is_valid_plid(res.second)) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	std::string plid{std::move(res.second)};

	char play[GUESS_SIZE];
	for (int i = 0; i< GUESS_SIZE; i++) {
		res = req.read(1, 1);
		if (res.first != net::action_status::OK
			|| (res.first = net::is_valid_color(res.second)) != net::action_status::OK)
			break;
		play[i] = res.second[0];
	}
	if (res.first != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	char trial;
	res = req.read(1, 1);
	if (res.first != net::action_status::OK
		|| (res.first = req.check_strict_end()) != net::action_status::OK) {
		out_strm.write("ERR").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	trial = res.second[0];
	auto gm = game::find_active(plid);
	if (gm.first != net::action_status::OK) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		if (gm.first == net::action_status::NOT_IN_GAME)
			return udp_conn.answer(out_strm, client_addr);
		udp_conn.answer(out_strm, client_addr);
		return gm.first;
	}
	char duplicate_at = gm.second.is_duplicate(play);
	if (trial != gm.second.current_trial() + 1) {
		if (trial == gm.second.current_trial() && duplicate_at == gm.second.current_trial()) {
			out_strm.write("OK");
			out_strm.write(gm.second.current_trial());
			out_strm.write(gm.second.last_trial()->nB + '0');
			out_strm.write(gm.second.last_trial()->nW + '0').prime();
			std::cout << out_strm.view();
			return udp_conn.answer(out_strm, client_addr);
		}

		out_strm.write("INV").prime();
		std::cout << out_strm.view();
		// TODO: persist game? forcibly remove game?
		return udp_conn.answer(out_strm, client_addr);
	}

	if (duplicate_at != MAX_TRIALS + 1) {
		out_strm.write("DUP").prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}

	auto [play_status, play_res] = gm.second.guess(plid, play);
	if (play_status != net::action_status::OK) {
		out_strm.write("INV").prime(); // TODO: unsure if it should return INV in this case
		std::cout << out_strm.view();
		udp_conn.answer(out_strm, client_addr);
		return play_status; // return first error
	}
	if (play_res == game::result::LOST_TIME || play_res == game::result::LOST_TRIES) {
		if (play_res == game::result::LOST_TIME)
			out_strm.write("ETM");
		else
			out_strm.write("ENT");
		for (int i = 0; i < GUESS_SIZE; i++)
			out_strm.write(gm.second.secret_key()[i]);
		out_strm.prime();
		std::cout << out_strm.view();
		return udp_conn.answer(out_strm, client_addr);
	}
	out_strm.write("OK");
	out_strm.write(gm.second.current_trial());
	out_strm.write(gm.second.last_trial()->nB + '0');
	out_strm.write(gm.second.last_trial()->nW + '0').prime();
	std::cout << out_strm.view();
	// Testing sb
	// if (gm->second.has_ended() == game::result::WON) {
	// 	sb.add_game(gm->second);
	// 	sb.print_sb_test();
	// }
	return udp_conn.answer(out_strm, client_addr);
}

/*static net::action_status show_trials(net::stream<net::tcp_source>& req,
									  const net::tcp_connection& tcp_conn) {
	auto [status, plid] = req.read(PLID_SIZE, PLID_SIZE);
	net::out_stream out_strm;
	out_strm.write("RST");
	if (status != net::action_status::OK ||
		(status = req.check_strict_end()) != net::action_status::OK ||
		(status = net::is_valid_plid(plid)) != net::action_status::OK) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return tcp_conn.answer(out_strm);
	}

	int game_fd = open(game::get_active_path(plid).c_str(), O_RDONLY);
	if (game_fd == -1 && errno != ENOENT) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return tcp_conn.answer(out_strm);
	}

	if (game_fd != -1) {
		out_strm.write("ACT");
		net::stream<net::file_source> source{{game_fd}};
		std::string sent_file = game::prepare_trial_file(source);
		close(game_fd); // TODO: check if sent_file is good + check if close was sucessful
		out_strm.write(game::get_fname(plid));
		out_strm.write(std::to_string(sent_file.size())); // TODO: excp?
		out_strm.write(sent_file).prime();
		return tcp_conn.answer(out_strm);
	}

	std::string path;
	try {
		path = game::get_latest_path(plid);
	} catch (...) { // TODO: finer exceptions
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		tcp_conn.answer(out_strm);
		return net::action_status::PERSIST_ERR;
	}

	if (path.empty()) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		return tcp_conn.answer(out_strm);
	}
	if ((game_fd = open(path.c_str(), O_RDONLY)) == -1) {
		out_strm.write("NOK").prime();
		std::cout << out_strm.view();
		tcp_conn.answer(out_strm);
		return net::action_status::PERSIST_ERR;
	}
	out_strm.write("FIN");
	net::stream<net::file_source> source{{game_fd}};
	std::string sent_file = game::prepare_trial_file(source);
	close(game_fd); // TODO: check if sent_file is good + check if close was sucessful
	out_strm.write(game::get_fname(plid));
	out_strm.write(std::to_string(sent_file.size())); // TODO: excep?
	out_strm.write(sent_file).prime();
	return tcp_conn.answer(out_strm);
}*/

/*static net::action_status show_scoreboard(net::stream<net::tcp_source>& req,
										  const net::tcp_connection& tcp_conn, score_board& sb) {
	net::out_stream out_strm;
	out_strm.write("RSS");
	
	if (sb.is_empty()) {
	out_strm.write("EMPTY").prime();
	std::cout << "Sent: " << out_strm.view() << '\n';
	return tcp_conn.answer(out_strm);
	}

	out_strm.write("OK");
	std::string filename = "TOPSCORES_XXXXXXX.txt";
	std::filesystem::create_directories("sv"); // testing TO DELETE LATER 
    auto path = "sv/" + filename;
	std::ofstream file(path, std::ios::out | std::ios::trunc);
	if (!file.is_open())
		return net::action_status::PERSIST_ERR;
	
	// header
	file << "-------------------------------- TOP 10 SCORES --------------------------------\n\n";
	file << std::setw(18) << "SCORE" << std::setw(10) << "PLAYER" << std::setw(10)
		 << "CODE" << std::setw(12) << "NO TRIALS" << std::setw(10) << "MODE\n\n";

	int rank = 1;
    for (const auto& g: sb) { // TODO: Needs formating
        file << std::setw(13) << rank << " - "
             << std::setw(6) << g.score() << "  "    // TODO: test values
             << std::setw(10) << "123456" << "  "     // TODO: get plid
             << std::setw(10);
		for (int i = 0; i < GUESS_SIZE; i++)
			file << g.secret_key()[i];
        std::cout << "  ";
        file<< std::setw(12) << g.current_trial() << "  ";
        std::string mode;
		if (g.is_debug())
			mode = "DEBUG";           
		else
			mode = "PLAY";
			 
		file << std::setw(10) << mode << "\n";
        rank++;
        if (rank > MAX_TOP_SCORES) break;
    }

    file.close();


	std::ifstream file_size_stream(path, std::ios::binary | std::ios::ate);
    size_t file_size = file_size_stream.tellg();

	if (file_size > 1024) { //TODO: do something here
        return net::action_status::PERSIST_ERR;
    }

	out_strm.write(filename).write(std::to_string(file_size));
	std::ifstream file_content_stream(path, std::ios::binary);
    std::string file_content((std::istreambuf_iterator<char> (file_content_stream)),
							 std::istreambuf_iterator<char>());
	file_content_stream.close();						  

	out_strm.write(file_content).prime();

    file_size_stream.close();
		std::cout << "Sent: " << out_strm.view() << '\n';
	return tcp_conn.answer(out_strm);
}*/
