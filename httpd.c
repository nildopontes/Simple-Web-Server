#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>

#define CONNMAX 1000
#define PORT "12913"

char js[]   = "text/javascript",
     html[] = "text/html",
     css[]  = "text/css",
     txt[]  = "text/plain",
     bin[]  = "aplication/octet-stream";

// Client request
char    *method,    // "GET" or "POST"
        *uri,       // "index.html"
        *qs,        // "a=1&b=2"
        *prot,      // "HTTP/1.1"
        *mime,      // Mime-type do arquivo solicitado
        *payload;   // body of POST
int     payload_size; // size of body in bytes
static int listenfd, clients[CONNMAX];

typedef struct { char *name, *value; } header_t;
static header_t reqhdr[30] = { {"\0", "\0"} };
static int clientfd;

static char *buf;

// Retorna o número de bytes do arquivo solicitado na requisição
long int fileLength(){
    struct stat info;
    if(stat(uri, &info) == -1){
        return -1;
    }
    return (intmax_t)info.st_size;
}

// Despeja o conteudo do arquivo solicitado na saida padão
int echoFile(long int size){
    FILE *fd;
    char *data = (char*)malloc(size);
    fd = fopen(uri, "rb");
    fread(data, size, 1, fd);
    fwrite(data, 1 , size, stdout);
    fclose(fd);
    return 0;
}

void notFound(){
    printf("HTTP/1.1 404 Not Found\r\n");
    printf("Content-Type: text/html\r\n");
    printf("Content-Length: 158\r\n\r\n");
    echoFile(158);
}

// Define o Content-Type com base na extensão do arquivo requisitado
void defineMimeByName(){
    char *p = strchr(uri, '.');
    if(p == NULL) mime = bin; // arquivo sem extensão
    else p++; if(strcmp(p, "js") == 0) mime = js;
    else if(strcmp(p, "html") == 0) mime = html;
    else if(strcmp(p, "css") == 0) mime = css;
    else if(strcmp(p, "txt") == 0) mime = txt;
    else mime = bin; // bin se aplica a qualquer extensão desconhecida
}

// Retorna o valor do cabeçalho passado no parâmetro
char *request_header(const char* name){
    header_t *h = reqhdr;
    while(h->name){
        if(strcmp(h->name, name) == 0){
            return h->value;
        }
        h++;
    }
    return NULL;
}

// Responde com o arquivo pedido ou um 404 Not found
int route(){
    defineMimeByName();
    long int fsize = fileLength();
    if(fsize < 0){
        sprintf(uri, "404.html");
        notFound();
        return 0;
    }
    printf("HTTP/1.1 200 OK\r\n");
    printf("Content-Type: %s\r\n", mime);
    printf("Content-Length: %ld\r\n\r\n", fsize);
    echoFile(fsize);
    return 0;
}

// Inicia o servidor
void startServer(){
    struct addrinfo hints, *res, *p;

    // getaddrinfo for host
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if(getaddrinfo(NULL, PORT, &hints, &res) != 0){
        perror ("getaddrinfo() error");
        exit(1);
    }
    // socket and bind
    for(p = res; p!=NULL; p=p->ai_next){
        int option = 1;
        listenfd = socket (p->ai_family, p->ai_socktype, 0);
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
        if (listenfd == -1) continue;
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) break;
    }
    if(p == NULL){
        perror ("socket(p) or bind()");
        exit(1);
    }

    freeaddrinfo(res);

    // listen for incoming connections
    if(listen(listenfd, CONNMAX) != 0){
        perror("listen() error");
        exit(1);
    }
}

//client connection
void respond(int n){
    int rcvd, fd, bytes_read;
    char *ptr;

    buf = malloc(65535);
    rcvd = recv(clients[n], buf, 65535, 0);

    if(rcvd<0)    // receive error
        fprintf(stderr,("recv() error\n"));
    else if (rcvd==0)    // receive socket closed
        fprintf(stderr,"Cliente desconectado.\n");
    else{    // message received
        buf[rcvd] = '\0';

        method = strtok(buf,  " \t\r\n");
        uri    = strtok(NULL, " \t")+1;
        prot   = strtok(NULL, " \t\r\n");

        if((qs = strchr(uri, '?'))){
            *qs++ = '\0'; //split URI
        }else{
            qs = uri - 1; //use an empty string
        }
        header_t *h = reqhdr;
        char *t, *t2;
        while(h < reqhdr+29){
            char *k,*v;
            k = strtok(NULL, "\r\n: \t"); if (!k) break;
            v = strtok(NULL, "\r\n");     while(*v && *v==' ') v++;
            h->name  = k;
            h->value = v;
            h++;
            t = v + 1 + strlen(v);
            if (t[1] == '\r' && t[2] == '\n') break;
        }
        t += 3;
        t2 = request_header("Content-Length");
        payload = t; // body da requisição
        payload_size = t2 ? atol(t2) : (rcvd-(t-buf)); // Tamanho do body da requisição

        // bind clientfd to stdout, making it easier to write
        clientfd = clients[n];
        dup2(clientfd, STDOUT_FILENO);
        close(clientfd);
        // call router
        route();
        // tidy up
        fflush(stdout);
        shutdown(STDOUT_FILENO, SHUT_WR);
        close(STDOUT_FILENO);
    }

    //Closing SOCKET
    shutdown(clientfd, SHUT_RDWR); //All further send and recieve operations are DISABLED...
    close(clientfd);
    clients[n]=-1;
}

void serve_forever(){
    struct sockaddr_in clientaddr;
    socklen_t addrlen;
    char c;
    int slot=0;
    printf("Servidor iniciado na porta %s%s%s\n", "\033[92m",PORT,"\033[0m");
    // Setting all elements to -1: signifies there is no client connected
    int i;
    for(i=0; i<CONNMAX; i++){
        clients[i]=-1;
    }
    startServer();

    // Ignore SIGCHLD to avoid zombie threads
    signal(SIGCHLD,SIG_IGN);

    // Ouvindo pedidos de conexão
    while(1){
        addrlen = sizeof(clientaddr);
        clients[slot] = accept (listenfd, (struct sockaddr *) &clientaddr, &addrlen);

        if(clients[slot]<0){
            perror("accept() error");
        }else{
            if(fork()==0){
                respond(slot);
                exit(0);
            }
        }
        while (clients[slot]!=-1) slot = (slot+1)%CONNMAX;
    }
}

int main(){
    serve_forever();
    return 0;
}
