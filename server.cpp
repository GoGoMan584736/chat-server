#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

void save_username(const std::string& uid, const std::string& uname) {
    if (uid.empty() || uname.empty()) return;
    std::vector<std::string> lines;
    std::ifstream file_in("usernames.txt");
    std::string line;
    bool updated = false;
    while (std::getline(file_in, line)) {
        if (line.rfind(uid + "=", 0) == 0) {
            lines.push_back(uid + "=" + uname);
            updated = true;
        } else if (!line.empty()) {
            lines.push_back(line);
        }
    }
    file_in.close();
    if (!updated) lines.push_back(uid + "=" + uname);

    std::ofstream file_out("usernames.txt", std::ios::trunc);
    for (const auto& l : lines) file_out << l << "\n";
}

std::string get_username(const std::string& uid) {
    std::ifstream file("usernames.txt");
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind(uid + "=", 0) == 0) {
            return line.substr(uid.length() + 1);
        }
    }
    return uid;
}

std::vector<std::string> read_lines(const std::string& filename) {
    std::vector<std::string> lines;
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

std::string extract_header(const std::string& req, const std::string& header_name) {
    std::string key = header_name + ": ";
    size_t pos = req.find(key);
    if (pos == std::string::npos) return "";
    size_t end = req.find("\r\n", pos);
    if (end == std::string::npos) end = req.find("\n", pos);
    return req.substr(pos + key.length(), end - (pos + key.length()));
}

int main() {
    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 3000;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);

    std::cout << "Server listening on port " << port << std::endl;

    while (true) {
        int new_socket = accept(server_fd, nullptr, nullptr);
        if (new_socket < 0) continue;

        char buffer[4096] = {0};
        read(new_socket, buffer, sizeof(buffer));
        std::string req(buffer);

        std::stringstream ss(req);
        std::string method, url;
        ss >> method >> url;

        std::string uid = extract_header(req, "X-User-ID");
        std::string uname = extract_header(req, "X-User-Name");
        std::string room = extract_header(req, "X-Room-Name");
        std::string target_id = extract_header(req, "X-Target-ID");

        if (!method.empty() && !url.empty()) {
            std::cout << "Incoming Request: " << method << " " << url << " (User: " << uid << ")" << std::endl;
        }

        if (!uid.empty() && !uname.empty()) save_username(uid, uname);

        size_t body_pos = req.find("\r\n\r\n");
        std::string body = (body_pos != std::string::npos) ? req.substr(body_pos + 4) : "";

        std::string resp_body = "";

        if (url == "/create" && method == "POST") {
            if (!room.empty() && !uid.empty()) {
                std::ofstream f_allowed("allowed_" + room + ".txt", std::ios::app);
                f_allowed << uid << "\n";
                std::ofstream f_idx("room_index.txt", std::ios::app);
                f_idx << room << "\n";
                std::ofstream f_msg("messages_" + room + ".txt", std::ios::app);
                f_msg << "System|Room '" + room + "' created by " + get_username(uid) + "\n";
                resp_body = "OK";
            }
        } else if (url == "/rooms" && method == "GET") {
            auto rooms = read_lines("room_index.txt");
            for (const auto& r : rooms) {
                auto allowed = read_lines("allowed_" + r + ".txt");
                if (std::find(allowed.begin(), allowed.end(), uid) != allowed.end()) {
                    resp_body += r + "\n";
                }
            }
        } else if (url == "/members" && method == "GET") {
            if (!room.empty()) {
                auto allowed = read_lines("allowed_" + room + ".txt");
                for (size_t i = 0; i < allowed.size(); ++i) {
                    resp_body += get_username(allowed[i]);
                    if (i == 0) resp_body += " (Admin)";
                    if (i + 1 < allowed.size()) resp_body += ", ";
                }
            }
        } else if (url == "/add_member" && method == "POST") {
            if (!room.empty() && !target_id.empty()) {
                std::ofstream f("allowed_" + room + ".txt", std::ios::app);
                f << target_id << "\n";
                resp_body = "OK";
            }
        } else if (url == "/leave" && method == "POST") {
            if (!room.empty() && !uid.empty()) {
                auto allowed = read_lines("allowed_" + room + ".txt");
                std::ofstream f("allowed_" + room + ".txt", std::ios::trunc);
                for (const auto& m : allowed) {
                    if (m != uid) f << m << "\n";
                }
                resp_body = "OK";
            }
        } else if (url == "/" && method == "POST") {
            if (!room.empty() && !body.empty()) {
                std::ofstream f("messages_" + room + ".txt", std::ios::app);
                f << body << "\n";
                resp_body = "OK";
            }
        } else if (url == "/" && method == "GET") {
            if (!room.empty()) {
                std::ifstream f("messages_" + room + ".txt");
                std::stringstream buffer_msg;
                buffer_msg << f.rdbuf();
                resp_body = buffer_msg.str();
            }
        }

        std::string http_resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n" + resp_body;
        send(new_socket, http_resp.c_str(), http_resp.length(), 0);
        close(new_socket);
    }
    return 0;
}
