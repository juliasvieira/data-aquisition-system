#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

// ===== ESTRUTURA DE DADOS =====

// Estrutura do registro de log - definida conforme especificação
#pragma pack(push, 1)
struct LogRecord {
    char sensor_id[32];      // ID do sensor (máximo 32 caracteres)
    std::time_t timestamp;   // Timestamp UNIX
    double value;            // Valor da leitura do sensor
};
#pragma pack(pop)

// ===== FUNÇÕES UTILITÁRIAS =====

// Converte string de data/hora para timestamp UNIX
std::time_t string_to_time_t(const std::string& time_string) {
    std::tm tm = {};
    std::istringstream ss(time_string);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

// Converte timestamp UNIX para string de data/hora
std::string time_t_to_string(std::time_t time) {
    std::tm* tm = std::localtime(&time);
    std::ostringstream ss;
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

// Divide uma string usando um delimitador
std::vector<std::string> split_string(const std::string& str, char delimiter) {
    std::vector<std::string> parts;
    std::stringstream ss(str);
    std::string part;
    
    while (std::getline(ss, part, delimiter)) {
        parts.push_back(part);
    }
    
    return parts;
}

// ===== GERENCIADOR DE ARQUIVOS DE LOG =====

class LogFileManager {
private:
    std::mutex file_mutex_;  // Mutex para acesso thread-safe aos arquivos
    
public:
    // Escreve um registro de log no arquivo específico do sensor
    bool write_log_record(const LogRecord& record) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        
        std::string filename = "sensor_" + std::string(record.sensor_id) + ".bin";
        std::ofstream file(filename, std::ios::binary | std::ios::app);
        
        if (!file.is_open()) {
            std::cerr << "Erro ao abrir arquivo de log: " << filename << std::endl;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(&record), sizeof(record));
        file.close();
        
        std::cout << "Log gravado - Sensor: " << record.sensor_id 
                  << ", Valor: " << record.value 
                  << ", Timestamp: " << time_t_to_string(record.timestamp) << std::endl;
        
        return true;
    }
    
    // Lê os últimos N registros de um sensor específico
    std::vector<LogRecord> read_last_records(const std::string& sensor_id, int num_records) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        
        std::string filename = "sensor_" + sensor_id + ".bin";
        std::ifstream file(filename, std::ios::binary);
        
        std::vector<LogRecord> records;
        
        if (!file.is_open()) {
            std::cout << "Arquivo não encontrado para sensor: " << sensor_id << std::endl;
            return records;  // Retorna vetor vazio se arquivo não existe
        }
        
        // Lê todos os registros do arquivo
        LogRecord record;
        while (file.read(reinterpret_cast<char*>(&record), sizeof(record))) {
            records.push_back(record);
        }
        
        file.close();
        
        // Retorna apenas os últimos N registros
        if (records.size() > static_cast<size_t>(num_records)) {
            records.erase(records.begin(), records.end() - num_records);
        }
        
        std::cout << "Lidos " << records.size() << " registros para sensor: " << sensor_id << std::endl;
        
        return records;
    }
    
    // Verifica se um sensor existe (se há arquivo de log para ele)
    bool sensor_exists(const std::string& sensor_id) {
        std::string filename = "sensor_" + sensor_id + ".bin";
        std::ifstream file(filename);
        return file.good();
    }
};

// ===== CLASSE DE SESSÃO =====

class Session : public std::enable_shared_from_this<Session> {
private:
    tcp::socket socket_;
    boost::asio::streambuf buffer_;
    LogFileManager& log_manager_;
    
public:
    Session(tcp::socket socket, LogFileManager& log_manager)
        : socket_(std::move(socket)), log_manager_(log_manager) {
    }
    
    void start() {
        std::cout << "Nova conexão estabelecida" << std::endl;
        do_read();
    }
    
private:
    void do_read() {
        auto self(shared_from_this());
        
        // Lê dados assincronamente até encontrar \r\n
        boost::asio::async_read_until(socket_, buffer_, "\r\n",
            [this, self](boost::system::error_code ec, std::size_t length) {
                if (!ec) {
                    process_message();
                    do_read();  // Continua lendo mais mensagens
                } else {
                    std::cout << "Conexão encerrada: " << ec.message() << std::endl;
                }
            });
    }
    
    void process_message() {
        // Extrai a mensagem do buffer
        std::istream is(&buffer_);
        std::string message;
        std::getline(is, message);
        
        // Remove \r se presente
        if (!message.empty() && message.back() == '\r') {
            message.pop_back();
        }
        
        std::cout << "Mensagem recebida: " << message << std::endl;
        
        // Parse da mensagem usando delimitador |
        std::vector<std::string> parts = split_string(message, '|');
        
        if (parts.empty()) {
            std::cerr << "Mensagem vazia recebida" << std::endl;
            return;
        }
        
        // Processa diferentes tipos de mensagem
        if (parts[0] == "LOG") {
            handle_log_message(parts);
        } else if (parts[0] == "GET") {
            handle_get_message(parts);
        } else {
            std::cerr << "Tipo de mensagem desconhecido: " << parts[0] << std::endl;
        }
    }
    
    void handle_log_message(const std::vector<std::string>& parts) {
        // Formato esperado: LOG|SENSOR_ID|DATA_HORA|LEITURA
        if (parts.size() != 4) {
            std::cerr << "Formato inválido para mensagem LOG" << std::endl;
            return;
        }
        
        try {
            // Cria o registro de log
            LogRecord record;
            
            // Limpa o buffer e copia o ID do sensor (máximo 31 chars + null terminator)
            memset(record.sensor_id, 0, sizeof(record.sensor_id));
            strncpy(record.sensor_id, parts[1].c_str(), sizeof(record.sensor_id) - 1);
            
            // Converte timestamp
            record.timestamp = string_to_time_t(parts[2]);
            
            // Converte valor
            record.value = std::stod(parts[3]);
            
            // Salva o registro
            log_manager_.write_log_record(record);
            
        } catch (const std::exception& e) {
            std::cerr << "Erro ao processar mensagem LOG: " << e.what() << std::endl;
        }
    }
    
    void handle_get_message(const std::vector<std::string>& parts) {
        // Formato esperado: GET|SENSOR_ID|NUMERO_DE_REGISTROS
        if (parts.size() != 3) {
            std::cerr << "Formato inválido para mensagem GET" << std::endl;
            send_error_response("INVALID_FORMAT");
            return;
        }
        
        try {
            std::string sensor_id = parts[1];
            int num_records = std::stoi(parts[2]);
            
            // Verifica se o sensor existe
            if (!log_manager_.sensor_exists(sensor_id)) {
                send_error_response("INVALID_SENSOR_ID");
                return;
            }
            
            // Lê os registros
            std::vector<LogRecord> records = log_manager_.read_last_records(sensor_id, num_records);
            
            // Envia a resposta
            send_records_response(records);
            
        } catch (const std::exception& e) {
            std::cerr << "Erro ao processar mensagem GET: " << e.what() << std::endl;
            send_error_response("PROCESSING_ERROR");
        }
    }
    
    void send_error_response(const std::string& error_type) {
        std::string response = "ERROR|" + error_type + "\r\n";
        
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(response),
            [this, self, response](boost::system::error_code ec, std::size_t /*length*/) {
                if (ec) {
                    std::cerr << "Erro ao enviar resposta de erro: " << ec.message() << std::endl;
                } else {
                    std::cout << "Resposta de erro enviada: " << response;
                }
            });
    }
    
    void send_records_response(const std::vector<LogRecord>& records) {
        // Formato: NUM_REGISTROS;DATA_HORA|LEITURA;...;DATA_HORA|LEITURA\r\n
        std::ostringstream response;
        response << records.size();
        
        for (const auto& record : records) {
            response << ";" << time_t_to_string(record.timestamp) << "|" << record.value;
        }
        
        response << "\r\n";
        
        auto self(shared_from_this());
        std::string response_str = response.str();
        
        boost::asio::async_write(socket_, boost::asio::buffer(response_str),
            [this, self, response_str](boost::system::error_code ec, std::size_t /*length*/) {
                if (ec) {
                    std::cerr << "Erro ao enviar resposta: " << ec.message() << std::endl;
                } else {
                    std::cout << "Resposta enviada: " << response_str;
                }
            });
    }
};

// ===== CLASSE DO SERVIDOR =====

class DataAcquisitionServer {
private:
    tcp::acceptor acceptor_;
    LogFileManager log_manager_;
    
public:
    DataAcquisitionServer(boost::asio::io_context& io_context, short port)
        : acceptor_(io_context, tcp::endpoint(tcp::v4(), port)) {
        std::cout << "Servidor iniciado na porta " << port << std::endl;
        do_accept();
    }
    
private:
    void do_accept() {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket) {
                if (!ec) {
                    // Cria nova sessão para cada conexão
                    std::make_shared<Session>(std::move(socket), log_manager_)->start();
                } else {
                    std::cerr << "Erro ao aceitar conexão: " << ec.message() << std::endl;
                }
                
                // Continua aceitando novas conexões
                do_accept();
            });
    }
};

// ===== FUNÇÃO PRINCIPAL =====

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            std::cerr << "Uso: " << argv[0] << " <porta>" << std::endl;
            std::cerr << "Exemplo: " << argv[0] << " 9000" << std::endl;
            return 1;
        }
        
        // Converte a porta para inteiro
        int port = std::atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            std::cerr << "Porta inválida: " << port << std::endl;
            return 1;
        }
        
        // Cria o contexto de I/O assíncrono
        boost::asio::io_context io_context;
        
        // Cria e inicia o servidor
        DataAcquisitionServer server(io_context, port);
        
        std::cout << "Sistema de Aquisição de Dados iniciado!" << std::endl;
        std::cout << "Aguardando conexões de sensores..." << std::endl;
        std::cout << "Pressione Ctrl+C para encerrar" << std::endl;
        
        // Executa o loop de eventos assíncrono
        io_context.run();
        
    } catch (std::exception& e) {
        std::cerr << "Erro: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
