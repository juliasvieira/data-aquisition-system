/*#include <iostream>
#include <boost/asio.hpp>

int main(int argc, char* argv[]) {
    return 0;
}
*/

#include <iostream>
#include <boost/asio.hpp>
#include <fstream>
#include <ctime>
#include <vector>
#include <map>
#include <mutex>
#include <algorithm>

using boost::asio::ip::tcp;

// Definição da estrutura do registro de log
#pragma pack(push, 1)
struct LogRecord {
    char sensor_id[32];      // ID do sensor (máx 32 caracteres)
    std::time_t timestamp;   // Timestamp UNIX
    double value;            // Valor da leitura
};
#pragma pack(pop)

// Conversão de string para time_t
std::time_t string_to_time_t(const std::string& time_string) {
    std::tm tm = {};
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

// Conversão de time_t para string
std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

// Classe para gerenciar sessões de conexão
class session : public std::enable_shared_from_this<session> {
public:
    session(tcp::socket socket) : socket_(std::move(socket)) {}
    
    void start() {
        do_read();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    process_message(length);
                    do_read();
                }
            });
    }

    void process_message(std::size_t length) {
        std::string message(data_, length);
        
        // Verifica se a mensagem termina com \r\n
        if (message.size() < 2 || message.substr(message.size()-2) != "\r\n") {
            // Mensagem inválida - ignorar ou enviar erro
            return;
        }
        
        // Remove \r\n do final
        message = message.substr(0, message.size()-2);
        
        // Divide a mensagem em partes
        std::vector<std::string> parts;
        size_t pos = 0;
        while ((pos = message.find('|')) != std::string::npos) {
            parts.push_back(message.substr(0, pos));
            message.erase(0, pos + 1);
        }
        parts.push_back(message);
        
        if (parts.empty()) return;
        
        if (parts[0] == "LOG") {
            // Processar mensagem de log
            if (parts.size() == 4) {
                handle_log_message(parts[1], parts[2], parts[3]);
            }
        } else if (parts[0] == "GET") {
            // Processar solicitação de dados
            if (parts.size() == 3) {
                handle_get_message(parts[1], std::stoi(parts[2]));
            }
        }
    }
    
    void handle_log_message(const std::string& sensor_id, const std::string& timestamp, const std::string& value) {
        try {
            // Criar registro de log
            LogRecord record;
            strncpy(record.sensor_id, sensor_id.c_str(), sizeof(record.sensor_id));
            record.timestamp = string_to_time_t(timestamp);
            record.value = std::stod(value);
            
            // Escrever no arquivo de log
            std::string filename = "sensor_" + sensor_id + ".bin";
            std::ofstream file(filename, std::ios::binary | std::ios::app);
            if (file) {
                file.write(reinterpret_cast<char*>(&record), sizeof(record));
            }
        } catch (...) {
            // Tratar erros de conversão
        }
    }
    
    void handle_get_message(const std::string& sensor_id, int num_records) {
        std::string filename = "sensor_" + sensor_id + ".bin";
        std::ifstream file(filename, std::ios::binary);
        
        if (!file) {
            // Sensor não encontrado
            std::string response = "ERROR|INVALID_SENSOR_ID\r\n";
            boost::asio::write(socket_, boost::asio::buffer(response));
            return;
        }
        
        // Ler todos os registros
        std::vector<LogRecord> records;
        LogRecord record;
        while (file.read(reinterpret_cast<char*>(&record), sizeof(record))) {
            records.push_back(record);
        }
        
        // Determinar quantos registros retornar
        int records_to_return = std::min(num_records, static_cast<int>(records.size()));
        if (records_to_return <= 0) {
            std::string response = "0;\r\n";
            boost::asio::write(socket_, boost::asio::buffer(response));
            return;
        }
        
        // Construir resposta
        std::ostringstream response;
        response << records_to_return << ";";
        
        // Pegar os últimos N registros
        auto start = records.end() - records_to_return;
        for (auto it = start; it != records.end(); ++it) {
            if (it != start) response << ";";
            response << time_t_to_string(it->timestamp) << "|" << it->value;
        }
        
        response << "\r\n";
        
        // Enviar resposta
        boost::asio::write(socket_, boost::asio::buffer(response.str()));
    }

    tcp::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
};

// Classe do servidor
class server {
public:
    server(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        do_accept();
    }

private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    std::make_shared<session>(std::move(socket))->start();
                }
                do_accept();
            });
    }

    tcp::acceptor acceptor_;
};

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Uso: " << argv[0] << " <porta>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        server s(io_context, std::atoi(argv[1]));

        std::cout << "Servidor iniciado na porta " << argv[1] << std::endl;
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exceção: " << e.what() << "\n";
    }

    return 0;
}
