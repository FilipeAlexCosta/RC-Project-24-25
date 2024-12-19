#include "except.hpp"

using namespace net;

system_error::system_error(const std::string& what)
	: std::runtime_error{what} {}

corruption_error::corruption_error(const std::string& what)
	: std::runtime_error{what} {}

bad_response::bad_response(const std::string& what)
	: std::runtime_error{what} {}

game_error::game_error(const std::string& what)
	: std::runtime_error{what} {}

interaction_error::interaction_error(const std::string& what)
	: std::runtime_error{what} {}

syntax_error::syntax_error(const std::string& what)
	: interaction_error{what} {}

formatting_error::formatting_error(const std::string& what)
	: interaction_error{what} {}

missing_eom::missing_eom()
	: formatting_error{"Missing EOM"} {}

io_error::io_error(const std::string& what)
	: std::runtime_error{what} {}

socket_error::socket_error(const std::string& what)
	: std::runtime_error{what} {}

conn_error::conn_error(const std::string& what)
	: socket_error{what} {}

socket_closed_error::socket_closed_error(const std::string& what)
	: socket_error{what} {}
