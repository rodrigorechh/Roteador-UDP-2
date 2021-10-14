#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<sys/time.h>
#include<pthread.h>
#include<unistd.h>
#include <stdio_ext.h>
#include "headers/structures.h"

struct roteador *roteadores;
struct sockaddr_in si_me, si_other;
int *meuid;
pthread_mutex_t timerMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tableMutex = PTHREAD_MUTEX_INITIALIZER;
int sock, seq = 0, confirmacao = 0, tentativa = 0;
int unlinkRouter[QTD_MAXIMA_ROTEADORES];
int nodos[QTD_MAXIMA_ROTEADORES], qt_nodos = 0;//qt_nodos = qtd_nodos
int *meus_vetores, *myvec_original, *enlaces, *saida, *saida_original;
int *table[QTD_MAXIMA_ROTEADORES];

int vizinhos[QTD_MAXIMA_ROTEADORES], n_viz = 1;//salva o id dos roteadores vizinhos. n_viz é a qtd de vizinhos

void die(char *s)
{
    perror(s);
    exit(1);
}

void now(){
    struct timeval tv;
    struct timezone tz;
    struct tm *tm;

    gettimeofday(&tv, &tz);
    tm=localtime(&tv.tv_sec);
    printf("(%d:%02d:%02d:%d):\n", tm->tm_hour, tm->tm_min, tm->tm_sec, (int)tv.tv_usec);
}

/*copia um vetor de inteiro pra um novo vetor de inteiro com endereço diferente*/
int *copyvec(int vetor[], int tamanho) {
    int *copy = malloc(sizeof(int) * tamanho);

    for(int i=0; i<tamanho; i++){
        copy[i] = vetor[i];
    }

    return copy;
}

/* Recebe o id de um roteador e retorna a posição dele no array global nodos*/
int idNodo(int id){
    for(int i=0; i<qt_nodos; i++){
        if(nodos[i] == id)
            return i;
    }
}

void printRoteadores(){
    puts("---------------------------------");
     printf("| id:%d %s:%d\n| Vizinhos:\n", roteadores[0].id, roteadores[0].ip, roteadores[0].porta);
    for(int i=1; i<n_viz; i++)
        printf("| id:%d | %s:%d\n", roteadores[i].id, roteadores[i].ip, roteadores[i].porta);
    puts("---------------------------------");
}

/*
    Add novo vizinho no array vizinhos[] se ele já não existir no array.
*/
void adicionaVizinho(int id) {
    for(int i=1; i< n_viz; i++){
        if(id == vizinhos[i])
            return;
    }
    vizinhos[n_viz] = id;
    n_viz++;
    return;
}

int main(int argc, char *argv[ ])
{
    meuid = malloc(sizeof(int));

    if(argc==1){//TODO retirar isso e deixar só o else
        printf("Olá, por favor informe o meu ID:\n");
        scanf("%d", meuid);
    }else if(argc==2){
        *meuid = atoi(argv[1]);
        printf("Olá, sou o roteador %d:\n", *meuid);
    }

    saida = malloc(sizeof(int) * QTD_MAXIMA_ROTEADORES);
    mapeia();
    loadLinks(*meuid);

    printaNodo();
    printaTable();

    loadConfs(vizinhos);

    printRoteadores();

    pthread_t tids[3];

    socketConfig();

    sleep(2);

    pthread_create(&tids[0], NULL, router, (void *) &roteadores[0].porta);
    pthread_create(&tids[1], NULL, terminal, NULL);
    pthread_create(&tids[2], NULL, controlVec, NULL);
    pthread_join(tids[0], NULL);
    pthread_join(tids[1], NULL);
    pthread_join(tids[2], NULL);

    close(sock);
    return(1);
}

void printaNodo(){
    printf("nodos: ");
    for(int i=0; i<qt_nodos; i++){
        printf("[%d]", nodos[i]);
    }
    puts("");
}

void printaVec(int *vec){
    //printf("meu vetor: ");
    for(int i=0; i<qt_nodos; i++){
        printf("[%d]", vec[i]);
    }
    puts("");
}

void printaTable(){
    puts("\n--Tabela de Roteamento--");    
    for(int i = 0; i < qt_nodos; i++){
        printf("%d ", nodos[i]);//cabeçalho da linha
        if(table[i] == -1){
            puts("N/A");//TODO se não há provavelmente não deveria nem estar na tabela de roteamento.
            continue;
        }
        for(int j = 0; j < qt_nodos; j++){
            printf("[id:%d, valor:%d]", nodos[j], table[i][j]);//conteúdo da linha
        }
        puts("");
    }
    printf("\nsaida: ");
    for(int i = 0; i < qt_nodos; i++){
        printf("[%d]", saida[i]);
    }
    puts("");
}

void printaVizinhos(){
    printf("Vizinhos: ");
    for (int i = 1; i < n_viz; i++){
        printf("[%d]", vizinhos[i]);
    }
    puts("");
}

/*
    Abre enlaces.config e apartir dele
    salva no array global nodos[] o id de todos os roteadores sem se repetir.
    Também salva na variável qt_nodos a quantidade de roteadores que existem no array.
*/
void mapeia(){
    int rot1, rot2, custo;
    int r1_n, r2_n;

    FILE *file = fopen("configs/enlaces.config", "r");
    if (!file)
        die("Não foi possível abrir o arquivo de Enlaces");

    while (fscanf(file, "%d %d %d", &rot1, &rot2, &custo) != EOF){//percorre linha por linha do enlaces.config
        int r1_n = r2_n = 1;
        for(int i=0; i < qt_nodos; i++){
            if (rot1 == nodos[i]){
                r1_n = 0;
            } else if(rot2 == nodos[i]){
                r2_n = 0;
            }
        }
        if(r1_n){
            nodos[qt_nodos] = rot1;
            qt_nodos++;
        }
        if(r2_n){
            nodos[qt_nodos] = rot2;
            qt_nodos++;
        }
    }
    fclose(file);
}

/*
    Pelo arquivo de enlaces.config encontra os roteadores vizinhos, salva o id deles no array vizinhos[]. Tbm salva a si mesmo mas em vez de salvar seu id como faz com todos, salva 0.
    Também salva em meus_vetores o custo pra chegar até todos os vizinhos e a si mesmo como 0.
    Por fim salva na variável table[idNodo(id)] o array de meus_vetores.
    salva em enlaces um array igual ao de meus_vetores só com endereço diferente.
*/
void loadLinks(int myid){
    int rot1, rot2, custo;
    meus_vetores = malloc(sizeof(int) * qt_nodos);

    memset(meus_vetores, -1, sizeof(int) * qt_nodos);

    meus_vetores[idNodo(myid)] = 0;//salva 0 no array meus_vetores na posição i, onde i é a mesma posição do roteador no id nodos.

    FILE *file = fopen("configs/enlaces.config", "r");
    if (!file)
        die("Não foi possível abrir o arquivo de Enlaces");

    vizinhos[0] = myid;//add a si mesmo na posição 0 de vizinhos

    while (fscanf(file, "%d %d %d", &rot1, &rot2, &custo) != EOF){//percorre todas as linhas do enlaces.config
        for(int i=0; i < qt_nodos; i++){
            if(rot1 == myid){
                meus_vetores[idNodo(rot2)] = custo;
                adicionaVizinho(rot2);
            }if(rot2 == myid){
                meus_vetores[idNodo(rot1)] = custo;
                adicionaVizinho(rot1);
            }
        }
    }
    fclose(file);

    memset(table, -1, sizeof(int*) * qt_nodos);
    table[idNodo(myid)] = meus_vetores;
    enlaces = copyvec(meus_vetores, QTD_MAXIMA_ROTEADORES);
}

void loadConfs(int vizinhos[])
{
    int id_rot, porta_rot;
    char ip_rot[32];
    FILE *file;
    roteadores = malloc(sizeof(struct roteador) * n_viz);

    memset(saida, -1, sizeof(int) * QTD_MAXIMA_ROTEADORES);

    for(int i=1; i<n_viz; i++){
        saida[idNodo(vizinhos[i])] = vizinhos[i];
    }

    saida_original = copyvec(saida, QTD_MAXIMA_ROTEADORES);
    myvec_original = copyvec(meus_vetores, QTD_MAXIMA_ROTEADORES);

    for(int i=0; i<n_viz; i++){
        file = fopen("configs/roteador.config", "r");
        if (!file)
            die("Não foi possível abrir o arquivo de Roteadores");

        while (fscanf(file, "%d %d %s", &id_rot, &porta_rot, ip_rot) != EOF){
            if(vizinhos[i] == id_rot){
                roteadores[i].id    =  id_rot;
                roteadores[i].porta =  porta_rot;
                strcpy(roteadores[i].ip, ip_rot);
            }
        }
        fclose(file);
    }
}

void socketConfig()
{
    //create a UDP socket
    if ((sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }

    memset((char *) &si_me, 0, sizeof(si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(roteadores[0].porta);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);

    if( bind(sock , (struct sockaddr*)&si_me, sizeof(si_me) ) == -1)
    {
        die("bind");
    }
}

void *controlVec(){
    do{
        verificaEnlaces();
        sendMyVec();
        sleep(30);
    } while(1);
    return 0;
}

void sendMyVec()
{
    pacote vec_packet;
    vec_packet.id_font = *meuid;
    vec_packet.type = CONTROL;
    vec_packet.ack = 0;
    for (int i=0 ; i<qt_nodos ; i++)
        vec_packet.sendervec[i] = meus_vetores[i];

    for (int i = 1; i < n_viz; i++){
        vec_packet.id_dest = vizinhos[i];
        unlinkRouter[idNodo(vec_packet.id_dest)] += 1;
        sendPacket(vec_packet, FOWARD);
    }
}

void sendPacket(pacote packet, int strategy)
{
    int id_next, i, slen=sizeof(si_other);

    if(strategy == ROUTE){
        pthread_mutex_lock(&tableMutex);
        id_next = saida[idNodo(packet.id_dest)];
        pthread_mutex_unlock(&tableMutex);
    } else if(strategy == FOWARD){
        id_next = packet.id_dest;
    }

    if(id_next == -1){
        puts("DESTINO INALCANÇÁVEL");
        return;
    }

    for(i=1; i<n_viz; i++){
        if (roteadores[i].id == id_next)
            break;
    }

    if(strategy != FOWARD)
        printf("...encaminhando via roteador: %d | %s:%d\n", id_next, roteadores[i].ip, roteadores[i].porta);

    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(roteadores[i].porta);

    if (inet_aton(roteadores[i].ip , &si_other.sin_addr) == 0)
    {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    if (sendto(sock, &packet, sizeof(struct pacote) , 0 , (struct sockaddr *) &si_other, slen)==-1)
    {
        die("sendto()");
    }
}

void *terminal()
{
    int i, slen=sizeof(si_other);
    pacote packet;

    while(1)
    {
        while(1){
            printf("Enter router id:\n");
            scanf("%d", &packet.id_dest);
            if (packet.id_dest == roteadores[0].id)
                printf("Destino não alcançável, tente novamente.\n");
            else
                break;
        }

        printf("Enter message: ");
        __fpurge(stdin);
        fgets( packet.message, 100, stdin);

        packet.seq = ++seq;
        packet.type = DATA;
        packet.ack = 0;
        packet.id_font = roteadores[0].id;

        sendPacket(packet, ROUTE);
        pthread_mutex_lock(&timerMutex);
        tentativa = 0, confirmacao = 0;
        pthread_mutex_unlock(&timerMutex);

        while(1){
            sleep(10);
            pthread_mutex_lock(&timerMutex);

            if(tentativa >= 3 || confirmacao){
                pthread_mutex_unlock(&timerMutex);
                break;
            }
            else if(!confirmacao){
                printf("Pacote %d não entregue. Tentando novamente", packet.seq);
                tentativa += 1;
                pthread_mutex_unlock(&timerMutex);
                sendPacket(packet, ROUTE);
            }
        }
        pthread_mutex_unlock(&timerMutex);
        table[idNodo(*meuid)] = meus_vetores;
    }

    return 0;
}

void *router(void *porta)
{
    int i, slen = sizeof(si_other) , recv_len;
    int id_destino = -1;
    pacote packet;

    while(1)
    {
        if ((recv_len = recvfrom(sock, &packet, sizeof(struct pacote), 0, (struct sockaddr *) &si_other, &slen)) == -1)
        {
            die("recvfrom()");
        }

        id_destino = packet.id_dest;
        sleep(1);
       // printf("Pacote Chegado de %d -> Tipo: %s\n", packet.id_font, packet.type);
        if (id_destino != roteadores[0].id){
            int id_next = saida[idNodo(id_destino)];

            if(packet.type == DATA){
                printf("Roteador %d encaminhando mensagem com # sequência %d para o destino %d\n", roteadores[0].id, packet.seq, packet.id_dest);
            }else if (packet.type == CONTROL && packet.ack == 1){
                printf("Roteador %d encaminhando confirmação de msg #seq:%d para o sender %d\n", roteadores[0].id, packet.seq, packet.id_dest);
            }
            sendPacket(packet, ROUTE);

        }else if(id_destino == roteadores[0].id && packet.type == DATA){
            pacote response;
            response.type = CONTROL;
            response.ack = 1;
            response.id_font = id_destino;
            response.id_dest = packet.id_font;
            response.seq = packet.seq;

            printf("Pacote recebido de %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
            printf("Mensagem: %s\n", packet.message);
            puts("Enviando confirmação...");

            sendPacket(response, ROUTE);

        }else if(id_destino == roteadores[0].id && packet.ack == 1){
            printf("Confirmação recebida de %s:%d, mensagem #seq:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), packet.seq);
            pthread_mutex_lock(&timerMutex);
            confirmacao = 1;
            pthread_mutex_unlock(&timerMutex);
        }else if (id_destino == roteadores[0].id && packet.type == CONTROL){
            verificaVolta(packet);
            unlinkRouter[idNodo(packet.id_font)] = 0;
            table[idNodo(packet.id_font)] = copyvec(packet.sendervec, QTD_MAXIMA_ROTEADORES);
            updateFullTable();
        }
    }
    return 0;
}
void verificaVolta(pacote packet){
    int volta = 1;
    for(int i=0 ; i<n_viz ; i++){
        if(vizinhos[i] == packet.id_font)
            volta = 0;
    }
    if(volta){
        myvec_original[idNodo(packet.id_font)] = enlaces[idNodo(packet.id_font)];
        saida[idNodo(packet.id_font)] = packet.id_font;
        n_viz++;
        vizinhos[n_viz-1] = packet.id_font;
    }
}
void verificaEnlaces()
{
    pthread_mutex_lock(&tableMutex);
    int mudou = 0;
    for(int i = 0; i < qt_nodos; i++){
        if(unlinkRouter[i] > 2){
            table[i] = myvec_original[i] = saida[i] = -1;
            int *mynewvec = copyvec(myvec_original, qt_nodos);
            table[idNodo(*meuid)] = mynewvec;
            mudou = 1;

            for(int j=1; j<n_viz; j++){
                if(vizinhos[j] == nodos[i]){
                    if(j < n_viz-1){
                        vizinhos[j] = vizinhos[n_viz-1];
                    }
                    n_viz--;
                    unlinkRouter[i] = 0;
                }
            }
        }
    }
    pthread_mutex_unlock(&tableMutex);

    if(mudou)
        updateFullTable();
}

void updateFullTable(){
    pthread_mutex_lock(&tableMutex);

    int *lastvec;
    lastvec = malloc(sizeof(int) * qt_nodos);
    lastvec = copyvec(meus_vetores, QTD_MAXIMA_ROTEADORES);
    meus_vetores = copyvec(myvec_original, QTD_MAXIMA_ROTEADORES);

    for(int i=0 ; i<qt_nodos ; i++){
        if(i == idNodo(*meuid) || table[i] == -1)
            continue;
        for(int j=0 ; j<qt_nodos ; j++){
            if(table[i][j] == -1)
                continue;

            int novocusto = table[i][j] + myvec_original[i];
            if(novocusto < meus_vetores[j] || meus_vetores[j] == -1){
                meus_vetores[j] = novocusto;
                saida[j] = nodos[i];
                if(novocusto > 52){
                    printf("Detectado contagem ao infinito, enlace removido!\n");
                    meus_vetores[j] = -1;
                    saida[j] = -1;
                }
            }
        }
    }
    meus_vetores[idNodo(*meuid)] = 0;
    saida[idNodo(*meuid)] = -1;
    table[idNodo(*meuid)] = meus_vetores;
    for(int j=0 ; j<qt_nodos ; j++){
        if(table[idNodo(*meuid)][j] == -1)
            saida[j] = -1;
    }
    pthread_mutex_unlock(&tableMutex);
    for(int i=0 ; i<qt_nodos ; i++){
        if(lastvec[i] != meus_vetores[i]){
            printf("\n\nTabela atualizada ");
            now();
            printaTable();

            sendMyVec();
            break;
        }
    }
}
