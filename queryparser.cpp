#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include <limits>
#if defined(__linux__)
#include <sys/socket.h>
#include <sys/poll.h>
#endif
#define STR_MAX_LEN 2048        // максимальная длина строки ввода
#define POLL_TIMEOUT 30000  // таймаут для poll, миллисекунды. Ставим 10 сек

size_t ReadLine(int fd, char* line, ssize_t len, int flush=0);

extern FILE* log_file;

// возвращает 2, если на входе пришел 0; 1 если произошла ошибка; 0 - все в порядке
void parse_query( int fd_in, int64_t& num_val, int64_t& sum_val, int& res )
{
    char str[STR_MAX_LEN+1];
    std::string query, lexem;      // строка запроса и текущая лексема разбора
    std::string tail;   // остаток строки от ближайшего к концу пробела
    size_t data_length = 0;
    int64_t cur_int;
    bool newstring = true;
    struct pollfd child_poll;

    child_poll.events = POLLIN;
    child_poll.fd = fd_in;
    fprintf( log_file, "[Parse query] Start\n", num_val, sum_val );
    fflush( log_file );
    while( poll(&child_poll, 1, POLL_TIMEOUT)>0 )
    {
        data_length=ReadLine( fd_in, str, STR_MAX_LEN);
        if( data_length == 0 )
            break;  // типа таймаут
        if( newstring ) // если предыдущая строка заканчивается переводом строки
            query = str;
        else
            query += str;
        newstring = isspace(str[data_length-1]); //str[data_length-1] == '\n'; // прочитанная строка кончается переводом строки? true
        if( !newstring )
            continue;
//        fprintf( log_file, "[Parse query] Query=\"%s\"", query.data() );
        if( query.compare("\n")==0 || (query.compare("\r\n")==0 ) )   // строка только из '\n'
//                std::cout<<"empty string 3"<<std::endl;
            continue;   // пустая строка - пропускаем ее
//        fprintf( log_file, "\n[Parse query] New string received:\n%s", query.data() );
        fflush( log_file );
        fprintf( log_file, "\n[Parse query] Received integers:\n" );
//  разбираем полученную строку
        auto cur_ch=query.begin();
        do {
            while( isspace(*cur_ch) && cur_ch!=query.end() )    // пропускаем пробелы
                cur_ch++ ;
            if( cur_ch == query.end() ) break;
            lexem.clear();
            while( cur_ch!=query.end() && !isspace(*cur_ch) )
                    lexem += *cur_ch++;         // копируем символы в лексему
            if( cur_ch == query.end() ) break;
            bool isZero = true; // вся лексема из нулей?
            auto i=lexem.begin();
            if( *i == '-' || *i =='+' ) ++i;    // пропускаем лидирующий +/-
            for( ; i!=lexem.end(); ++i )
                if( *i != '0' )
                {
                    isZero = false;
                    break;
                }
            if( !isZero )  // лексема состоит не только из нулей
            {
//                fprintf( log_file, "\n[Parse query] lexem=%s\n", lexem.data() );
//                fflush( log_file );
                try{
                    cur_int = std::stoll( lexem.data() );
                }
                catch ( std::invalid_argument& e ){
                    cur_int = 0;
                    fprintf( log_file, "\n[Parse query] Exception %s, value redused to zero\n", e.what() );
                }
                catch ( std::out_of_range& e ){
                    cur_int = 0;
                    fprintf( log_file, "\n[Parse query] Exception %s value redused to zero\n", e.what() );
                }
                fprintf( log_file, "%lld ", cur_int );
                if( cur_int == 0 ) continue;    // лексема не из нулей, поэтому если сейчас cur_int==0, то лексема - не число
                {
                    if( (sum_val>0 && cur_int > (std::numeric_limits<int64_t>::max()-sum_val)) ||  // сумма sum_val и cur_int превысит max<int64_t>
                            (sum_val<0 && cur_int > (std::numeric_limits<int64_t>::min()-sum_val)) )   // сумма sum_val и cur_int пренизит min<int64_t>
                    {
                        lexem = "\n[Parse query] Overflow. Input canceled num_values="+ std::to_string(num_val)+
                                " sum="+std::to_string( sum_val )+"\n";
                        fprintf( log_file, "%s", lexem.data() );
                        send( fd_in, lexem.data(), lexem.size(), MSG_NOSIGNAL ); // выводим сообщение в сокет клиенту
                        fflush( log_file );
                        res = 0;
                        return;
                    }
                    sum_val += cur_int;
                    ++num_val;
                }
            }
            else    // получили 0 - сигнал к завершению
            {
                fprintf( log_file, "\n[Parse query] Exit by zero received: num_values=%lld, sum=%lld\n", num_val, sum_val );
                fflush( log_file );
                res= 0;
                return;
            }
        } while( cur_ch != query.end() );
        fprintf( log_file, "\n" );
    }
    fprintf( log_file, "[Parse query] Exit by timeout: num_values=%lld, sum=%lld\n", num_val, sum_val );
    fflush( log_file );
    res = 2;
    return;
}

// вспомогательная функция поиска символа в буфере ограниченной длины
char* buf_strchr( char* buffer, char ch, size_t bufferlen )
{
    for( size_t un=0; un<bufferlen; un++)
        if( buffer[un] == ch )
            return buffer+un;
    return nullptr;
}

// We read-ahead, so we store in static buffer
// what we already read, but not yet returned by ReadLine.
// если flush<>0, чтения файла не производится, возвращаются уже прочитанные данные
size_t ReadLine(int fd, char* line, ssize_t len, int flush)
{
     static char *buffer = static_cast<char*>(malloc(1025*sizeof(char)));
     static size_t bufferlen=0;
     char tmpbuf[1025];
// Do the real reading from fd until buffer has '\n'.
     char *pos;
     ssize_t n;
     size_t un; //, i;  // unsigned значение n, шоб 100 раз не преобразовывать

     if( !line || !len )
         return 0;
     if( flush )
         pos=buffer+bufferlen-1;
     else
     {
         while( (pos=buf_strchr(buffer, '\n', bufferlen)) ==nullptr )
         {
//             n = read(fd, tmpbuf, 1024);
             n = recv(fd, tmpbuf, 1024, MSG_NOSIGNAL );
             if (n==0 || n==-1)
             {  // handle errors
                 bufferlen=0;
                 buffer = static_cast<char*>( realloc(buffer, sizeof(char)) );
                 buffer[0] = '\0';
                 line = buffer;
                 return 0;
             }
             un = static_cast<size_t>(n);
             tmpbuf[un] = '\0';
             buffer = static_cast<char*>( realloc(buffer, (bufferlen+un)*sizeof(char)) );
             memcpy( buffer+bufferlen, tmpbuf, un );
             bufferlen += un;
         }
     }
// Split the buffer around '\n' found and return first part.
     if( (pos-buffer+1)>=len ) // полученная срока больше буфера для чтения
     {  // возвращаем то, что влезет
         un = static_cast<size_t>(len-1);
         memcpy(line, buffer, un);
         line[un] = '\0';
         memmove( buffer, buffer+un, bufferlen-un );  // остаток оставляем в буфере
         bufferlen -= un;
         buffer = static_cast<char*>( realloc(buffer, (bufferlen)*sizeof(char)) );
     }
     else    // строка входит в буфер
     {
         un = static_cast<size_t>(pos-buffer+1);
         memcpy(line, buffer, un);
         line[un] = '\0';
         if( bufferlen>un )
         {
             bufferlen -= un;
             memmove( buffer, pos+1, bufferlen );  // остаток оставляем в буфере
             buffer = static_cast<char*>( realloc(buffer, (bufferlen)*sizeof(char)) );
         }
         else
         {
             bufferlen = 0;
             buffer = static_cast<char*>( realloc(buffer, sizeof(char)) );
             buffer[0] = '\0';
         }
     }
     return un;
}
