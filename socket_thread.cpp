#include <iostream>
#include <cpprest/ws_client.h>
#include <cpprest/json.h>
#include <pthread.h>
#include <mqueue.h>
#include <iomanip>

using namespace std;
using namespace web;
using namespace web::websockets::client;

websocket_client client;

const char* msgqueue_ReceiveRemote = "/receiveRemote";
mqd_t QueuereceiveRemote;

const char* msgqueue_sendRemote = "/sendRemote";
mqd_t QueuesendRemote;

void SetupThread(int prio, pthread_attr_t *pthread_attr, struct sched_param *pthread_param);
int initThreads(pthread_t &receiveFromServerID, pthread_t &receiveFromLocalID);
void* tReceiveFromServer(void* args);
void* tReceiveFromLocal(void* args);
void processData(const string& utf8_content);
float define_buffer[4];

void SetupThread(int prio, pthread_attr_t *pthread_attr, struct sched_param *pthread_param){
    
    using namespace std;
    int rr_min_priority, rr_max_priority;

    //retornam os valores minimo e maximo de prioridade disponivel para o escalonamento
    rr_min_priority = sched_get_priority_min (SCHED_RR);
    rr_max_priority = sched_get_priority_max (SCHED_RR);

    pthread_attr_init(pthread_attr); //inicializa atributos da thread com valores default
    
    pthread_attr_setinheritsched (pthread_attr, PTHREAD_EXPLICIT_SCHED);  //configura atributos da thread para que a política de escalonamento seja 
    //determinada pelos atributos, e não herde a política do processo principal

    pthread_attr_setschedpolicy (pthread_attr, SCHED_RR);  //define processo de escalonamento como o Roud Robin (RR)

    //define o valor da prioridade da struct
    pthread_param -> sched_priority = prio;
    std::cout << "SCHED_RR priority range is " << rr_min_priority << " to " 
         << rr_max_priority << ": using " << pthread_param -> sched_priority << endl;

    //parâmetros de escalonamento são associados à thread (prioridade e tipo de esclonamento (RR) por exemplo)
    pthread_attr_setschedparam (pthread_attr, pthread_param);
}


int initThreads(pthread_t &receiveFromServerID, pthread_t &receiveFromLocalID){

    pthread_attr_t attr;
    pthread_attr_t attr_2;
    struct sched_param param;
    struct sched_param param2;

    SetupThread(1, &attr, &param);
    SetupThread(1, &attr_2, &param2);

    int rFromServer = pthread_create(&receiveFromServerID, NULL, tReceiveFromServer, NULL);
    int rFromLocal  = pthread_create(&receiveFromLocalID, NULL, tReceiveFromLocal, NULL);

    if (rFromServer != 0 || rFromLocal != 0) {
        cerr << ("Error creating threads!");
        EXIT_FAILURE;
    } else {
        EXIT_SUCCESS;
    }

    return 0;
}

void processData(const string& content){

    try {
        // JSON parse
        web::json::value jsonValue = web::json::value::parse(content);

        utility::string_t action = jsonValue[U("action")].as_string();

        float temperature = std::stof(jsonValue[U("temperature")].as_string());
        float airHumidity = std::stof(jsonValue[U("air_humidity")].as_string());
        float soilHumidity = std::stof(jsonValue[U("soil_humidity")].as_string());

        utility::string_t lightDefStr = jsonValue[U("light_def")].as_string();
        float lightDef = (lightDefStr == U("ON")) ? 1 : 0;


        // Imprime os valores para verificar
        cout << "Msg Parse: " << endl;
        wcout << L"Action: " << action.c_str() << endl;
        wcout << L"Temperature: " << temperature << endl;
        wcout << L"Air Humidity: " << airHumidity << endl;
        wcout << L"Soil Humidity: " << soilHumidity << endl;
        cout  << " Light: " << lightDef << endl;

        define_buffer[0] = temperature;
        define_buffer[1] = airHumidity;
        define_buffer[2] = soilHumidity;
        define_buffer[3] = lightDef;

    } catch (const web::json::json_exception& e) {
        cerr << "Error parsing JSON: " << e.what() << endl;
    }
}

void* tReceiveFromServer(void* thread_ID){

    while(1){

        client.receive().then([](websocket_incoming_message msg) {
            return msg.extract_string();
        }).then([](string content) {

        // Converter a string wide para string normal (UTF-8)
        string utf8_content(content.begin(), content.end());

        //process data -> parsing -> switch das actions -> enviar para local via msg_queue
        processData(utf8_content);


        ssize_t bytesRead = mq_send(QueuereceiveRemote, reinterpret_cast<char*>(define_buffer), sizeof(float)* 4, 0);

        if (bytesRead == -1) {
            std::cerr << "Error receiving data in Process 2" << std::endl;
            perror("mq_receive");
        } else {
            cout << "Data received in process 2!" << endl;
        }

        cout << "Received Message: " << utf8_content << std::endl;
        }).wait();
    }

    return nullptr;
}

void* tReceiveFromLocal(void* thread_ID){

    float sensor_data_buffer[11];
    
    while(1){

        //Receive data from rasp
        ssize_t bytesRead = mq_receive(QueuesendRemote, reinterpret_cast<char*>(sensor_data_buffer), sizeof(float)* 11, 0);

        if (bytesRead == -1) {
            std::cerr << "Error receiving data in Process 2" << std::endl;
            perror("mq_receive");
        } else {
            cout << "Data received in process 2!" << endl;
        }

        std::tm tm = {};

        tm.tm_mday = static_cast<int>(sensor_data_buffer[0]);  // Dia
        tm.tm_mon  = static_cast<int>(sensor_data_buffer[1])- 1;  // Mês
        tm.tm_year = static_cast<int>(sensor_data_buffer[2])- 1900;  // Ano
        tm.tm_hour = static_cast<int>(sensor_data_buffer[3]);  // Hora
        tm.tm_min  = static_cast<int>(sensor_data_buffer[4]);  // Minuto
        tm.tm_sec  = static_cast<int>(sensor_data_buffer[5]);  // Segundo

        std::ostringstream oss;
        oss << std::put_time(&tm, "%d-%m-%Y %H:%M:%S");


        string timestamp = oss.str();

        string temperature = to_string(sensor_data_buffer[6]);
        string air_humidity = to_string(sensor_data_buffer[7]);
        string soil_humidity = to_string(sensor_data_buffer[8]);
        string water_level = to_string(sensor_data_buffer[9]);

        string light = "OFF" ;

        int a = sensor_data_buffer[10];

        if(a == 1){
            light = "ON";
        } else {
            light = "OFF";
        }

        for(int i=0; i<5; i++){
            cout << " buffer -> " << sensor_data_buffer[i] << endl;
        }

        //Send data to server
        try {
            // JSON msg to send
            web::json::value json_msg;

            json_msg[U("action")]  = web::json::value::string(U("update_data"));

            json_msg[U("temp")]    = web::json::value::string(timestamp);
            json_msg[U("a_hum")]   = web::json::value::string(temperature);
            json_msg[U("s_hum")]   = web::json::value::string(air_humidity);
            json_msg[U("light")]   = web::json::value::string(soil_humidity);
            json_msg[U("water")]   = web::json::value::string(water_level);
            json_msg[U("time")]    = web::json::value::string(light);

            utility::string_t dataJSON = json_msg.serialize();

            // Send msg to server
            websocket_outgoing_message out_msg;
            out_msg.set_utf8_message(dataJSON);
            client.send(out_msg).wait();

            cout << "Mensagem enviada com sucesso para o servidor." << endl;

            } catch (const web::websockets::client::websocket_exception& e) {
                cerr << "Erro ao enviar mensagem: " << e.what() << endl;
            } catch (const std::exception& e) {
                cerr << "Erro desconhecido: " << e.what() << endl;
            }

             //get messages via message queue
            sleep(5);
        }

    return nullptr;
}

int main(){

    pthread_t receiveFromServerID;
    pthread_t receiveFromLocalID;
    
    client.connect("ws://172.26.55.88:5000").wait();
    //client.connect("ws://192.168.1.73:5000").wait();

    web::json::value json_msg;

    json_msg[U("action")] = web::json::value::string(U("connection"));
    json_msg[U("type")]   = web::json::value::string(U("rasp"));

    utility::string_t dataJSON = json_msg.serialize();

    // Criar uma mensagem WebSocket com a carga útil JSON
    websocket_outgoing_message out_msg;
    out_msg.set_utf8_message(dataJSON);
    client.send(out_msg).wait();

    sleep(3);

    //Open Messages Queues
    QueuesendRemote = mq_open(msgqueue_sendRemote, O_RDONLY);

    if (QueuesendRemote == (mqd_t)-1) {
        std::cerr << "Error opening message queue in Process 2" << std::endl;
        perror("mq_open");
        return 1;
    } else {
        cout << "Message Queue bem criada no processo 2 " << endl; 
    }


    QueuereceiveRemote = mq_open(msgqueue_ReceiveRemote, O_WRONLY);

    if (QueuereceiveRemote == (mqd_t)-1) {
        std::cerr << "Error opening message queue in Process 2" << std::endl;
        perror("mq_open");
        return 1;
    } else {
        cout << "Message Queue bem criada no processo 2 " << endl; 
    }


    initThreads(receiveFromServerID, receiveFromLocalID);

    pthread_join(receiveFromServerID, nullptr);
    pthread_join(receiveFromLocalID, nullptr);

    pthread_exit(NULL);


    return 0;
}