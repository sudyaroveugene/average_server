// Тестовое задание:
// Многопоточный сервер расчёта средних значений.
// При запуске из командной строки серверу передаётся один параметр: номер IP-порта.
// Сервер начинает слушать этот порт (протокол TCP).
// Подключается несколько клиентов, которые начинают параллельно слать целые числа (int64_t) серверу.
// Когда от какого-то из клиентов приходит 0 - это считается концом передачи для данного клиента, после чего клиенту
// возвращается среднее значение от отправленных этим клиентом чисел (тоже int64_t) и соединение закрывается.
// Когда отключается последний из клиентов, сервер выводит в файл result.txt общее среднее по всем клиентам и завершает свою работу.

//Условия и ограничения:
//* Должна присутствовать инструкция по сборке.
//* Код должен компилироваться g++ 7.3 (можно использовать c++ до 17-й версии включительно).
//* Использовать только стандартные библиотеки c/с++.

#include <iostream>
#include <set>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#if defined(__linux__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/mman.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <vector>

//#define POLL_TIMEOUT 20000  // таймаут для poll, миллисекунды. Ставим 10 сек
void parse_query( int fd_in, int64_t& num_val, int64_t& sum_val, int& res );

int set_nonblock( int fd )
{
    int flags;
#if defined (O_NONBLOCK)
    if( -1==(flags=fcntl( fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl( fd, FIOBIO, &flags );
#endif
}

FILE *log_file;
std::string ip_addr, current_dir, port; // в этих строках будут параметры из командной строки IP-адрес сервера, рабочий каталог и порт
struct sockaddr_in client_name;//--структура sockaddr_in клиентской машины (параметры ее нам неизвестны.
                                // Мы не знаем какая машина к нам будет подключаться)
unsigned int client_name_size = sizeof(client_name);//--размер структуры (тоже пока неизвестен)
int client_socket_fd;          //--идентификатор клиентского сокета
int ret = 0;
pid_t child_pid = 0;

struct child_data
{
    pid_t pid;
    int fd;
    int dummy;  // неиспользуемая переменная для выравнивания в памяти
};

struct results
{
    int64_t sum_val;    // сумма полученных чисел данного коннекта
    int64_t num_val;    // количество полученных чисел данного коннекта
} *serv_res;	// указатель для общей памяти
int shm_d;	// дескриптор общей памяти

//--функция ожидания завершения дочернего процесса
void sig_child(int sig)
{
    pid_t pid;
    int stat;
    while( (pid=waitpid(-1,&stat,WNOHANG))>0 )
    {
        if( WIFEXITED(stat) )
            ret = WEXITSTATUS( stat );
        child_pid = pid;
//      cout<<"tut5 pid="<<pid<<" ret="<<ret<<endl;
    }
    return;
}

int server()
{
    void sig_child(int);//--объявление функции ожидания завершения дочернего процесса
    int res;
    char host[INET_ADDRSTRLEN];     // должно помещаться "ddd.ddd.ddd.ddd"
    const char* char_res;
    pid_t pid;
    time_t now;
    struct tm *tm_ptr;
    char timebuf[80];
    struct sockaddr_in master; //--структура sockaddr_in для нашего сервера
//    struct pollfd child_poll; // poll переехал в парсер
    std::vector<child_data> childs;   // массив pid для дочерних процессов

    fprintf( log_file, "\n[Server] Start\n" );
    int master_socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );

//    child_poll.events = POLLIN;
    master.sin_family = AF_INET;    //--говорим что сокет принадлежит к семейству интернет
    master.sin_port = htons( atoi(port.data()) );      //--и прослушивает порт
    master.sin_addr.s_addr = htonl( INADDR_ANY );   //--наш серверный сокет принимает запросы от любых машин с любым IP-адресом
//--функция bind спрашивает у операционной системы,может ли программа завладеть портом с указанным номером
    res = bind( master_socket, (struct sockaddr*) (&master), sizeof(master));
    if( res == -1 )
    {
        fprintf( log_file, "[Server] Error binding port: %s\n", strerror(errno) );
        fflush( log_file );
        exit( 1 );
    }
    set_nonblock( master_socket );
    listen( master_socket, SOMAXCONN ); //--перевод сокета сервера в режим прослушивания с очередью в макс. позиций
    fprintf( log_file, "port=%d ip_addr=%d\n", master.sin_port, master.sin_addr.s_addr );
    fflush( log_file );
    while( 1 )
    {
// устанавливаем обработчик завершения клиента (уже поработал и отключился, ждем завершение его дочернего процесса)
        signal( SIGCHLD, sig_child );
        client_socket_fd = accept( master_socket, (struct sockaddr*) (&client_name), &client_name_size ); //--подключение нашего клиента
        if( client_socket_fd>0 ) //--если подключение прошло успешно
        {
//--то мы создаем копию нашего сервера для работы с другим клиентом(то есть один сеанс уже занят дублируем свободный процесс)
            pid=fork();
            if( pid==0 )
            {   // child
                fprintf( log_file, "\n[Server] Client %d connected\n", getpid() );
                set_nonblock( client_socket_fd );
//                child_poll.fd = client_socket_fd;

                char_res =  inet_ntop( AF_INET, &client_name.sin_addr, host, sizeof(host) ); // --в переменную host заносим IP-клиента
                fprintf( log_file, "[Server] Client %s connected on socket %d\n", host, client_socket_fd );
                fflush( log_file );
                if( char_res == nullptr )
                {
                    fprintf( log_file, "[Server] Error сonverts the network address to string format: %s\n", strerror(errno) );
                    fflush( log_file );
                    exit( 1 );
                }
                time(&now);
                tm_ptr = localtime(&now);
                strftime( timebuf, sizeof( timebuf ), "%Y-%B-%e %H:%M:%S", tm_ptr );
                fprintf( log_file,"[Server] %s Connected client:%s in port: %d\n",
                         timebuf, host, ntohs( client_name.sin_port ) );
                fprintf( log_file, "[Server] Waiting request\n" );
                fflush( log_file );

                int64_t client_num =0, client_sum = 0;
//                if( poll(&child_poll, 1, POLL_TIMEOUT)>0 )
                std::string client_res_out = "\nReady to input:\r\n";
                send( client_socket_fd, client_res_out.data(), client_res_out.size(), MSG_NOSIGNAL ); // выводим в сокет клиенту

                parse_query( client_socket_fd, client_num, client_sum, res );
//                fprintf( log_file, "[Server] Request res=%d\n", res );
                int64_t client_res = 0;
                client_res_out = "Average for ";
                if( client_num>0 )
                    client_res = client_sum/client_num;    // получаем среднее
                client_res_out += std::to_string(client_num) + " values = " + std::to_string(client_res)+"\r\n" +
                        "Connection closed";
                if( res )
                    client_res_out += " by timeout\n";
                else
                    client_res_out += "by zero in input\n";
                send( client_socket_fd, client_res_out.data(), client_res_out.length(), MSG_NOSIGNAL ); // выводим среднее в сокет клиенту

                mlock( serv_res, sizeof(struct results) );  // блокируем общую память
                serv_res->num_val += client_num;            // добавляем результаты текущего клиента
                serv_res->sum_val += client_sum;
                munlock( serv_res, sizeof(struct results) );    // разблокируем

                time(&now);
                tm_ptr = localtime(&now);
//                strftime( timebuf, sizeof( timebuf ), "%Y-%B-%e %H:%M:%S", tm_ptr);
                strftime( timebuf, sizeof( timebuf ), "%H:%M:%S", tm_ptr);
                fprintf( log_file,"[Server] %s Close session on client: %s %d fd=%d\n", timebuf, host, getpid(), client_socket_fd );
                if( shutdown( client_socket_fd, SHUT_RDWR )==-1 )
                {
                    fprintf( log_file, "[Server] Error closed client %d: %s\n", getpid(), strerror(errno));
                    fflush( log_file );
                    exit( 2 );
                }
                close(client_socket_fd); //--естествено закрываем сокет
                fprintf( log_file, "[Server] Client %d closed\n", getpid() );
                fflush( log_file );
                exit( 2 ); //--гасим дочерний процесс
            }
            else if( pid>0 )   // parent
            {
                child_data new_child;
                new_child.pid = pid;
                new_child.fd = client_socket_fd;
                childs.push_back( new_child ); // добавляем дочерний процесс в список
                close(client_socket_fd);    // закрываем сокет в родителе, в дочке он остается открытым
                fflush( log_file );
            }
            else    // ошибка
            {
                fprintf( log_file, "[Server] Fork failed: %s\n", strerror(errno));
                fflush( log_file );
                exit( 1 );
            }
        }
        else
        {
//            fprintf( log_file, "[Server] Accept failed fd=%d: %s\n", client_socket_fd, strerror(errno));
            usleep( 10000 );
        }
        if( ret==2 ) // получена команда завершения сервера
        {
            for( auto i=childs.begin(); i!=childs.end(); ++i )
            {
                if( child_pid == i->pid )
                {
                    fprintf( log_file, "[Server] Client %d out of server\n", child_pid );
                    childs.erase( i );
                    break;
                }
            }
            if( childs.size() == 0 )
                ret = 3;
        }
        if( ret==3 ) // получена команда завершения сервера
        {
            fprintf( log_file, "[Server] Server shutdown: no clients\n" );
            for( auto i: childs )
            {
                fprintf( log_file, "[Server] i.pid=%d\n", i.pid );
                shutdown( i.fd, SHUT_RDWR );
                close( i.fd );
                kill( i.pid, SIGTERM );
            }
            FILE* fd_res = fopen( "result.txt", "w" );  // пробуем открыть файл result.txt
            if( fd_res )  //  файл открыт
            {
                int64_t res = 0;
                if( serv_res->num_val )
                    res = serv_res->sum_val/serv_res->num_val;
                if( fprintf( fd_res, "Average for %lld values = %lld\n", serv_res->num_val, res)<0 || // ошибка при записи в файл
                        fclose( fd_res )!=0 )   // ошибка закрытия файла
                    fprintf( log_file, "Error writing result.txt \"%s\"\n", strerror(errno));
            }
            else    // ошибка при открытии файла
                fprintf( log_file, "Error open result.txt \"%s\"\n", strerror(errno));
            shutdown( master_socket, SHUT_RDWR );
            close( master_socket );
            shm_unlink("as.shm");	// убиваем файл общей памяти
            fprintf( log_file, "[Server] Closed\n" );
            fflush( log_file );
            break;
        }
    }

    fprintf( log_file, "[Server] Stop\n\n" );
    fflush( log_file );
    return 0;
}


int main( int argc, char** argv )
{
    int optnum, pid;

// задаем параметры по умолчанию
    current_dir = getcwd( nullptr, 0 ); // рабочий каталог - текущий
    port = "12345"; // порт
    ip_addr = "127.0.0.1";  // IP-адрес сервера
// читаем параметры из командной строки
    optnum=getopt( argc, argv, "p:");
    if( optnum<0 )
        printf( "Use -p to define server port. Using default port 12345\n");
    else
    {
        switch( optnum )
        {
            case 'p':
                port = optarg;
                if( atoi(optarg) == 0 )
                {
                    printf( "Invalid port \"%s\". Using default port 12345\n");
                    port = "12345"; // порт
                }
                break;
            default:
                printf( "Use -p to define server port. Using default port 12345\n");
                break;
        }
    }
// открываем файл лога
//    printf( "Try to open final.log");
    log_file = fopen( "avarage_server.log", "a" );
    if( log_file==nullptr )
    {
        perror( "Error open avarage_server.log");
        exit( 1 );
    }
    perror( "Try to open avarage_server.log");

    shm_unlink("as.shm");
    shm_d = shm_open("as.shm", O_RDWR | O_CREAT, 0666);
    if( shm_d==-1 )
    {
        perror( "Error shm_open" );
        exit( 1 );
    }
    if( ftruncate(shm_d, sizeof(struct results)) == -1 )
    {
        perror( "Error ftruncate shared memory" );
        exit( 1 );
    }
    serv_res = static_cast<struct results*>( mmap( 0, sizeof(struct results), PROT_READ | PROT_WRITE, MAP_SHARED, shm_d, 0 ));
    if( serv_res==MAP_FAILED )
    {
        perror( "Error mmap shared memory" );
        exit( 1 );
    }

    fprintf( log_file, "\n=============== New session started =================\n" );
    fprintf( log_file, "current_dir=%s port=%s\n", current_dir.data(), port.data() );
    fflush( log_file );

    pid = fork();
    if( pid<0 )
    {
        perror( "fork");
        exit( 1 );
    }
    else if( pid>0 )  // parent
    {
        return 0;   // закрываем родительский процесс
    }
    else       // child
    {
        umask(0);   /* Изменяем файловую маску */
        if( setsid()<0 )    /* Создание нового SID для дочернего процесса */
        {
            perror( "setsid");
            exit( 1 );
        }

        if( (chdir(current_dir.data())) < 0) /* Изменяем текущий рабочий каталог */
        {
            perror( "chdir" );
            exit( 1 );
        }
        printf( "Server ready on port %s\n", port.data() );
    /* Закрываем стандартные файловые дескрипторы */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        pid = server();
        return pid;
    }
    return 0;
}

