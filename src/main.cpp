#include <iostream>
#include <iomanip>
#include "xnet.h"


int main(int argc, char** argv)
{
	networking_init();
	xnet::http::httpserver server(
		8080,
		20,
		[](auto&& msg) { std::cout << "[" << std::setw(5) << msg.code() << "]    " << msg.format(); },
		[](xnet::http::request& request)
		{
			xnet::http::response res;
			res << "<html>"
					<< "<body>"
						<< "<p>" << "Test Response" << "</p>"
					<< "</body>"
				<< "</html>";
			request.send_response(res);
		});
	server.listen();
	networking_cleanup();
}