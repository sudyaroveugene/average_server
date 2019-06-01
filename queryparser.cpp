#include <iostream>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <sys/socket.h>
#endif
#define STR_MAX_LEN 2048        // максимальная длина строки ввода

size_t ReadLine(int fd, char* line, ssize_t len, int flush=0);

extern FILE* log_file;

// возвращает 2, если на входе пришел 0; 1 если произошла ошибка; 0 - все в порядке
int parse_query( int fd_in, int64_t& num_val, int64_t& sum_val )
{
    char str[STR_MAX_LEN+1];
    std::string query, lexem;      // строка запроса и текущая лексема разбора
    size_t data_length = 0;
    int64_t cur_int;
    int res = 0;
    bool newstring = true;

    while( (data_length=ReadLine( fd_in, str, STR_MAX_LEN)) )
    {
        if( newstring ) // если предыдущая строка заканчивается переводом строки (буфер ввода разделил число на 2 части)
            query = str; // то создаем новую строку
        else
            query.append( str ); // иначе добавляем в предыдущую
        newstring = str[data_length-1] == '\n'; // прочитанная строка кончается переводом строки? true
        if( !newstring || (query.compare("\n") )==0 || (query.compare("\r\n") )==0 )    // строка только из '\n' или незаконченная
//                cout<<"empty string in begin"<<endl;
            continue;   // пустая или незаконченная строка - пропускаем ее
        fprintf( log_file, "[Parse query] New string received:\n%s", str );
        fprintf( log_file, "[Parse query] Received integers:\n" );
//  разбираем полученную строку
        auto cur_ch=query.begin();
        do {
            while( isspace(*cur_ch) && cur_ch!=query.end() )    // пропускаем пробелы
                cur_ch++ ;
            lexem.clear();
            while( cur_ch!=query.end() && !isspace(*cur_ch) )
                    lexem += *cur_ch++;         // копируем символы в лексему
            bool isNull = true; // вся лексема из нулей?
            auto i=lexem.begin();
            if( *i == '-' || *i =='+' ) ++i;    // пропускаем лидирующий +/-
            for( ; i!=lexem.end(); ++i )
                if( *i != '0' )
                {
                    isNull = false;
                    break;
                }
            if( !isNull )  // лексема состоит не только из нулей
            {
                cur_int = atoi( lexem.data() );
                if( cur_int == 0 ) continue;    // лексема не из нулей, поэтому если сейчас cur_int==0, то лексема - не число
                fprintf( log_file, "%lld ", cur_int );
                try
                {
                    sum_val += cur_int;
                    ++num_val;
                }
                catch( const std::overflow_error& e )
                {
                    lexem = "\n[Parse query] Overflow. Input canceled\n";
                    fprintf( log_file, "%s", lexem.data() );
                    send( fd_in, lexem.data(), lexem.size(), MSG_NOSIGNAL ); // выводим сообщение в сокет клиенту
                    return 2;
                }
            }
            else    // получили 0 - сигнал к завершению
                return 2;
        } while( cur_ch != query.end() );
    }
    return 0;
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
