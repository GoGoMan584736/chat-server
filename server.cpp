#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cctype>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/time.h>

const int PORT = 3000;

void log_server(const std::string& msg) {
    std::cout << "[SERVER LOG] " << msg << std::endl;
}

void trim(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.length() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
        start++;
    }
    if (start > 0) s = s.substr(start);
}

std::vector<std::string> read_lines(const std::string& filename) {
    std::vector<std::string> lines;
    std::ifstream file(filename);
    if (!file.is_open()) {
        log_server("File system: File '" + filename + "' does not exist yet.");
        return lines;
    }
    std::string line;
    while (std::getline(file, line)) {
        trim(line);
        if (!line.empty()) lines.push_back(line);
    }
    log_server("File system: Read " + std::to_string(lines.size()) + " lines from '" + filename + "'");
    return lines;
}

void write_line_if_missing(const std::string& filename, const std::string& value) {
    auto existing = read_lines(filename);
    if (std::find(existing.begin(), existing.end(), value) == existing.end()) {
        std::ofstream file(filename, std::ios::app);
        file << value << "\n";
        log_server("File system: Added entry '" + value + "' -> '" + filename + "'");
    } else {
        log_server("File system: Entry '" + value + "' already exists in '" + filename + "'");
    }
}

void remove_line(const std::string& filename, const std::string& value) {
    auto lines = read_lines(filename);
    std::ofstream file(filename, std::ios::trunc);
    int count = 0;
    for (const auto& l : lines) {
        if (l != value) {
            file << l << "\n";
        } else {
            count++;
        }
    }
    log_server("File system: Removed " + std::to_string(count) + " instance(s) of '" + value + "' from '" + filename + "'");
}

bool is_user_in_room(const std::string& uid, const std::string& room) {
    if (uid.empty() || room.empty()) return false;
    auto allowed = read_lines("allowed_" + room + ".txt");
    bool result = std::find(allowed.begin(), allowed.end(), uid) != allowed.end();
    log_server("Auth check: User '" + uid + "' allowed in room '" + room + "'? -> " + (result ? "YES" : "NO"));
    return result;
}

bool is_room_owner(const std::string& uid, const std::string& room) {
    if (uid.empty() || room.empty()) return false;
    auto owners = read_lines("owner_" + room + ".txt");
    bool result = std::find(owners.begin(), owners.end(), uid) != owners.end();
    log_server("Admin check: User '" + uid + "' owner of room '" + room + "'? -> " + (result ? "YES" : "NO"));
    return result;
}

std::string extract_header(const std::string& request, const std::string& header_name) {
    std::string req_lower = request;
    std::string name_lower = header_name;
    std::transform(req_lower.begin(), req_lower.end(), req_lower.begin(), ::tolower);
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

    size_t pos = req_lower.find(name_lower + ":");
    if (pos == std::string::npos) return "";

    size_t start = pos + name_lower.length() + 1;
    size_t end = request.find("\n", start);
    if (end == std::string::npos) end = request.length();

    std::string result = request.substr(start, end - start);
    trim(result);
    return result;
}

std::string get_url_path(const std::string& request) {
    size_t start = request.find(' ');
    if (start == std::string::npos) return "/";
    size_t end = request.find(' ', start + 1);
    return (end == std::string::npos) ? "/" : request.substr(start + 1, end - start - 1);
}

std::string extract_body(const std::string& request) {
    size_t body_pos = request.find("\r\n\r\n");
    if (body_pos != std::string::npos) return request.substr(body_pos + 4);

    body_pos = request.find("\n\n");
    if (body_pos != std::string::npos) return request.substr(body_pos + 2);

    return "";
}

std::string read_full_http_request(int sock) {
    std::string request = "";
    char buffer[1024];

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    while (true) {
        int bytes_read = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) break;
        buffer[bytes_read] = '\0';
        request.append(buffer, bytes_read);

        size_t header_end = request.find("\r\n\r\n");
        if (header_end == std::string::npos) header_end = request.find("\n\n");

        if (header_end != std::string::npos) {
            std::string cl_header = extract_header(request, "Content-Length");
            if (!cl_header.empty()) {
                int content_len = std::stoi(cl_header);
                size_t body_start = header_end + (request.find("\r\n\r\n") != std::string::npos ? 4 : 2);
                if (request.length() - body_start >= (size_t)content_len) break;
            } else {
                if (request.rfind("GET", 0) == 0) break;
            }
        }
    }
    return request;
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);

    std::cout << "===========================================" << std::endl;
    std::cout << " FULL-LOGGING SERVER READY ON PORT " << PORT << std::endl;
    std::cout << "===========================================" << std::endl;

    int request_counter = 0;

    while (true) {
        int new_socket = accept(server_fd, nullptr, nullptr);
        if (new_socket < 0) continue;

        request_counter++;
        log_server("-------------------------------------------");
        log_server("Incoming connection #" + std::to_string(request_counter) + " accepted on socket FD " + std::to_string(new_socket));

        std::string request = read_full_http_request(new_socket);
        if (request.empty()) {
            log_server("WARNING: Empty request payload or timeout on socket FD " + std::to_string(new_socket));
            close(new_socket);
            continue;
        }

        std::string method = request.substr(0, request.find(' '));
        std::string url = get_url_path(request);
        std::string client_uid = extract_header(request, "X-User-ID");
        std::string room = extract_header(request, "X-Room-Name");
        std::string body = extract_body(request);
        trim(body);

        log_server("HTTP Method:   [" + method + "]");
        log_server("HTTP Target:   [" + url + "]");
        log_server("X-User-ID:     [" + client_uid + "]");
        log_server("X-Room-Name:   [" + room + "]");
        log_server("Extracted Body:[" + body + "]");

        // Route: POST /create
        if (url == "/create") {
            log_server("Route hit: POST /create");
            if (!room.empty() && !client_uid.empty()) {
                write_line_if_missing("room_index.txt", room);
                write_line_if_missing("allowed_" + room + ".txt", client_uid);
                write_line_if_missing("owner_" + room + ".txt", client_uid);

                std::ofstream msg_file("messages_" + room + ".txt", std::ios::app);
                msg_file << "[SYSTEM]|Group '" << room << "' created by Admin " << client_uid << "\n";
                log_server("SUCCESS: Room '" + room + "' registered for Admin '" + client_uid + "'");
            } else {
                log_server("ERROR: Cannot create room. Missing Room-Name or User-ID");
            }
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK";
            send(new_socket, resp.c_str(), resp.length(), 0);
            close(new_socket);
            continue;
        }

        // Route: GET /rooms
        if (url == "/rooms") {
            log_server("Route hit: GET /rooms");
            std::vector<std::string> user_rooms;
            auto all_rooms = read_lines("room_index.txt");
            for (const auto& r : all_rooms) {
                if (is_user_in_room(client_uid, r)) {
                    user_rooms.push_back(r);
                }
            }
            std::string resp_body = "";
            for (const auto& r : user_rooms) resp_body += r + "\n";

            log_server("Returning " + std::to_string(user_rooms.size()) + " room(s) to User '" + client_uid + "'");
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n" + resp_body;
            send(new_socket, resp.c_str(), resp.length(), 0);
            close(new_socket);
            continue;
        }

        // Route: GET /members
        if (url == "/members") {
            log_server("Route hit: GET /members");
            std::string resp_body = "";
            if (is_user_in_room(client_uid, room)) {
                auto members = read_lines("allowed_" + room + ".txt");
                for (const auto& m : members) {
                    resp_body += m + (is_room_owner(m, room) ? " (Admin)" : "") + ", ";
                }
                if (!resp_body.empty()) { resp_body.pop_back(); resp_body.pop_back(); }
            } else {
                log_server("Denied member list access to user '" + client_uid + "' for room '" + room + "'");
            }
            log_server("Members list payload: [" + resp_body + "]");
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n" + resp_body;
            send(new_socket, resp.c_str(), resp.length(), 0);
            close(new_socket);
            continue;
        }

        // Route: POST /add_member
        if (url == "/add_member") {
            log_server("Route hit: POST /add_member");
            std::string target_uid = extract_header(request, "X-Target-ID");
            log_server("Target UID to add: [" + target_uid + "]");

            if (is_room_owner(client_uid, room) && !target_uid.empty()) {
                write_line_if_missing("allowed_" + room + ".txt", target_uid);
                std::ofstream msg_file("messages_" + room + ".txt", std::ios::app);
                msg_file << "[SYSTEM]|User " << target_uid << " was added by Admin.\n";
                log_server("SUCCESS: Added user '" + target_uid + "' to room '" + room + "'");
            } else {
                log_server("ERROR: /add_member failed. Requester is not owner or target ID is empty.");
            }
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK";
            send(new_socket, resp.c_str(), resp.length(), 0);
            close(new_socket);
            continue;
        }

        // Route: POST /leave
        if (url == "/leave") {
            log_server("Route hit: POST /leave");
            if (!room.empty()) {
                remove_line("allowed_" + room + ".txt", client_uid);
                remove_line("owner_" + room + ".txt", client_uid);
                log_server("User '" + client_uid + "' left room '" + room + "'");
            }
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK";
            send(new_socket, resp.c_str(), resp.length(), 0);
            close(new_socket);
            continue;
        }

        // Room Access Permission Check
        if (room.empty() || !is_user_in_room(client_uid, room)) {
            log_server("ACCESS DENIED: User '" + client_uid + "' requested forbidden access to room '" + room + "'");
            std::string forbidden = "HTTP/1.1 403 Forbidden\r\nContent-Type: text/plain\r\n\r\n[SYSTEM]: Access Denied to room.\n";
            send(new_socket, forbidden.c_str(), forbidden.length(), 0);
            close(new_socket);
            continue;
        }

        // Processing Message GET/POST
        if (method == "POST") {
            log_server("Route hit: POST / (Message Send)");
            if (!body.empty()) {
                std::ofstream msg_file("messages_" + room + ".txt", std::ios::app);
                msg_file << body << "\n";
                log_server("FILE SAVED: Appended message to 'messages_" + room + ".txt': [" + body + "]");
            } else {
                log_server("WARNING: POST request received but body was empty!");
            }
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nOK";
            send(new_socket, resp.c_str(), resp.length(), 0);
        } else {
            log_server("Route hit: GET / (Message Read)");
            std::ifstream msg_file("messages_" + room + ".txt");
            std::stringstream ss;
            ss << msg_file.rdbuf();
            std::string content = ss.str();

            log_server("Fetched " + std::to_string(content.length()) + " bytes of chat logs for room '" + room + "'");
            std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n" + content;
            send(new_socket, resp.c_str(), resp.length(), 0);
        }

        close(new_socket);
    }
    return 0;
}