#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio_ext.h>
#include "headers/structures.h"
#include <stdbool.h>

struct roteador *roteadores_vizinhos;
struct sockaddr_in socket_roteador, si_other;
int *id_roteador_atual;
pthread_mutex_t timerMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t tableMutex = PTHREAD_MUTEX_INITIALIZER;
int sock, seq = 0, confirmacao = 0, tentativa = 0;
int unlinkRouter[QTD_MAXIMA_ROTEADORES];
int nodos_rede[QTD_MAXIMA_ROTEADORES], qt_nodos = 0;
int *meus_vetores, *myvec_original, *enlaces, *saida, *saida_original;
int *tabela_roteamento[QTD_MAXIMA_ROTEADORES];

int vizinhos[QTD_MAXIMA_ROTEADORES], quantidade_vizinhos = 1;

fila_mensagens fila_entrada, fila_saida;
pthread_mutex_t mutex_fila_entrada = PTHREAD_MUTEX_INITIALIZER, mutex_fila_saida = PTHREAD_MUTEX_INITIALIZER;
int tamanho_atual_fila_entrada = 0, tamanho_atual_fila_saida = 0;

int main(int argc, char *argv[])
{
    id_roteador_atual = malloc(sizeof(int));

    if (argc < 2)
    {
        printf("Execute: %s <id do roteado>\n", argv[0]);
        return 0;
    }

    *id_roteador_atual = atoi(argv[1]);

    if (DEBUG)
        printf("\n\nId do roteador do processo atual é %d\n", *id_roteador_atual);

    saida = malloc(sizeof(int) * QTD_MAXIMA_ROTEADORES);
    setar_valor_default_tabela_roteamento(QTD_MAXIMA_ROTEADORES);
    mapeia();
    carregar_links_roteadores(*id_roteador_atual);

    printar_nodos_rede();
    printar_tabela_roteamento();

    carregar_configuracoes_roteadores(vizinhos);

    printar_roteadores_vizinhos();

    pthread_t tids[3];

    instanciar_socket();

    sleep(2);

    pthread_create(&tids[0], NULL, thread_roteador, (void *)&roteadores_vizinhos[0].porta);
    pthread_create(&tids[1], NULL, thread_terminal, NULL);
    pthread_create(&tids[2], NULL, thread_controle_vec, NULL);
    pthread_join(tids[0], NULL);
    pthread_join(tids[1], NULL);
    pthread_join(tids[2], NULL);

    close(sock);
    return (1);
}

/**
 * Método para encerar processamento
 */
void die(char *s)
{
    perror(s);
    exit(1);
}

/**
 * Método auxiliar para copiar um vator com determinado tamanho
 */
int *copiar_vetor(int vetor[], int tamanho)
{
    int *copy = malloc(sizeof(int) * tamanho);

    for (int i = 0; i < tamanho; i++)
    {
        copy[i] = vetor[i];
    }

    return copy;
}

/**
 * Retorna o índice do roteador no vetor nodos da rede
 */
int obter_index_por_id_roteador(int id)
{
    for (int i = 0; i < qt_nodos; i++)
    {
        if (nodos_rede[i] == id)
            return i;
    }
}

/**
 * Exibe tabela dos roteadores vizinhos
 */
void printar_roteadores_vizinhos()
{
    puts("---------------------------------");
    printf("| id:%d %s:%d\n| Vizinhos:\n", roteadores_vizinhos[0].id, roteadores_vizinhos[0].ip, roteadores_vizinhos[0].porta);
    for (int i = 1; i < quantidade_vizinhos; i++)
        printf("| id:%d | %s:%d\n", roteadores_vizinhos[i].id, roteadores_vizinhos[i].ip, roteadores_vizinhos[i].porta);
    puts("---------------------------------");
}

/**
 * Printa nodos da rede
 */
void printar_nodos_rede()
{
    printf("nodos da rede: ");

    for (int i = 0; i < qt_nodos; i++)
    {
        printf("[%d]", nodos_rede[i]);
    }

    puts("");
}

/**
 * 
 */
void printar_vec(int *vec)
{
    printf("vec: ");
    for (int i = 0; i < qt_nodos; i++)
    {
        printf("[%d]", vec[i]);
    }
    puts("");
}

/**
 * Printa a tabela de roteamento
 */
void printar_tabela_roteamento()
{
    puts("\n--Tabela de Roteamento--");
    for (int i = 0; i < qt_nodos; i++)
    {
        if (tabela_roteamento[i] == NULL)
        {
            puts("N/A");
            continue;
        }

        if (*(tabela_roteamento[i]) == -1)
        {
            puts("N/A");
            continue;
        }

        for (int j = 0; j < qt_nodos; j++)
        {
            printf("[%d]", tabela_roteamento[i][j]);
        }

        puts("");
    }
    printf("\nsaida: ");
    for (int i = 0; i < qt_nodos; i++)
    {
        printf("[%d]", saida[i]);
    }
    puts("");

    printar_vizinhos();
}

/**
 * Printa os vizinhos do roteador atual
 */
void printar_vizinhos()
{
    printf("Vizinhos: ");
    for (int i = 1; i < quantidade_vizinhos; i++)
    {
        printf("[%d]", vizinhos[i]);
    }
    puts("");
}

/**
 * Adiciona o id do roteador no array de vizinhos
 * e incrementa a vairável global que faz a contagem de vizinhos
 * 
 * Caso o id já consta na lista de vizinhos, a função finaliza
 */
void adicionar_vizinho_array(int id)
{
    for (int i = 1; i < quantidade_vizinhos; i++)
    {
        if (id == vizinhos[i])
            return;
    }

    vizinhos[quantidade_vizinhos] = id;
    quantidade_vizinhos++;

    return;
}

/**
 * Método para setar valores padrões na tabela de roteamento
 */
void setar_valor_default_tabela_roteamento(int quantidade_indices)
{
    for (int i = 0; i < QTD_MAXIMA_ROTEADORES; i++)
        tabela_roteamento[i] = NULL;
}

/**
 * Lê e extrai as configurações de enlaces que estão no arquivo
 * Apenas carrega os ids encontrados no arquivo e adiciona no array nodos_rede
 */
void mapeia()
{
    int rot1, rot2, custo;
    int r1_n, r2_n;

    FILE *file = fopen("configs/enlaces.config", "r");
    if (!file)
        die("Não foi possível abrir o arquivo de Enlaces");

    while (fscanf(file, "%d %d %d", &rot1, &rot2, &custo) != EOF)
    {
        int r1_n = r2_n = 1;
        for (int i = 0; i < qt_nodos; i++)
        {
            if (rot1 == nodos_rede[i])
            {
                r1_n = 0;
            }
            else if (rot2 == nodos_rede[i])
            {
                r2_n = 0;
            }
        }
        if (r1_n)
        {
            nodos_rede[qt_nodos] = rot1;
            qt_nodos++;
        }
        if (r2_n)
        {
            nodos_rede[qt_nodos] = rot2;
            qt_nodos++;
        }
    }
    fclose(file);
}

/**
 * Lê e extrai as configurações de enlaces que estão no arquivo
 * Carrega os links entre o roteador atual e vizinhos
 */
void carregar_links_roteadores(int myid)
{
    int rot1, rot2, custo;
    meus_vetores = malloc(sizeof(int) * qt_nodos);

    memset(meus_vetores, -1, sizeof(int) * qt_nodos);

    meus_vetores[obter_index_por_id_roteador(myid)] = 0;

    FILE *file = fopen("configs/enlaces.config", "r");
    if (!file)
        die("Não foi possível abrir o arquivo de Enlaces");

    vizinhos[0] = myid;

    while (fscanf(file, "%d %d %d", &rot1, &rot2, &custo) != EOF)
    {
        for (int i = 0; i < qt_nodos; i++)
        {
            if (rot1 == myid)
            {
                meus_vetores[obter_index_por_id_roteador(rot2)] = custo;
                adicionar_vizinho_array(rot2);
            }
            if (rot2 == myid)
            {
                meus_vetores[obter_index_por_id_roteador(rot1)] = custo;
                adicionar_vizinho_array(rot1);
            }
        }
    }
    fclose(file);

    setar_valor_default_tabela_roteamento(qt_nodos);
    tabela_roteamento[obter_index_por_id_roteador(myid)] = meus_vetores;
    enlaces = copiar_vetor(meus_vetores, QTD_MAXIMA_ROTEADORES);
}

/**
 * Lê e extrai as configurações de roteadores que estão no arquivo
 */
void carregar_configuracoes_roteadores(int vizinhos[])
{
    int id_rot, porta_rot;
    char ip_rot[32];
    FILE *file;
    roteadores_vizinhos = malloc(sizeof(struct roteador) * quantidade_vizinhos);

    memset(saida, -1, sizeof(int) * QTD_MAXIMA_ROTEADORES);

    for (int i = 1; i < quantidade_vizinhos; i++)
    {
        saida[obter_index_por_id_roteador(vizinhos[i])] = vizinhos[i];
    }

    saida_original = copiar_vetor(saida, QTD_MAXIMA_ROTEADORES);
    myvec_original = copiar_vetor(meus_vetores, QTD_MAXIMA_ROTEADORES);

    for (int i = 0; i < quantidade_vizinhos; i++)
    {
        file = fopen("configs/roteador.config", "r");
        if (!file)
            die("Não foi possível abrir o arquivo de Roteadores");

        while (fscanf(file, "%d %d %s", &id_rot, &porta_rot, ip_rot) != EOF)
        {
            if (vizinhos[i] == id_rot)
            {
                roteadores_vizinhos[i].id = id_rot;
                roteadores_vizinhos[i].porta = porta_rot;
                strcpy(roteadores_vizinhos[i].ip, ip_rot);
            }
        }
        fclose(file);
    }
}

/**
 * Método para criar o socket do roteador atual
 */
void instanciar_socket()
{
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }

    memset((char *)&socket_roteador, 0, sizeof(socket_roteador));
    socket_roteador.sin_family = AF_INET;
    socket_roteador.sin_port = htons(roteadores_vizinhos[0].porta);
    socket_roteador.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&socket_roteador, sizeof(socket_roteador)) == -1)
    {
        die("bind");
    }
}

/**
 * 
 */
void enviar_meus_vetores()
{
    pacote vec_packet;
    vec_packet.id_font = *id_roteador_atual;
    vec_packet.type = CONTROL;
    vec_packet.ack = 0;
    for (int i = 0; i < qt_nodos; i++)
        vec_packet.sendervec[i] = meus_vetores[i];

    for (int i = 1; i < quantidade_vizinhos; i++)
    {
        vec_packet.id_dest = vizinhos[i];
        unlinkRouter[obter_index_por_id_roteador(vec_packet.id_dest)] += 1;
        enviar_pacote(vec_packet, FOWARD);
    }
}

/**
 * 
 */
void enviar_pacote(pacote packet, int strategy)
{
    int id_next, i, slen = sizeof(si_other);

    if (strategy == ROUTE)
    {
        pthread_mutex_lock(&tableMutex);
        id_next = saida[obter_index_por_id_roteador(packet.id_dest)];
        printf("\n\npacket.id_dest = %d", packet.id_dest);
        printf("\n\nidx(packet.id_dest) = %d", obter_index_por_id_roteador(packet.id_dest));
        printf("\n\nid_next = %d", id_next);
        pthread_mutex_unlock(&tableMutex);
    }
    else if (strategy == FOWARD)
    {
        id_next = packet.id_dest;
    }

    if (id_next == -1)
    {
        puts("DESTINO INALCANÇÁVEL");
        return;
    }

    for (i = 1; i < quantidade_vizinhos; i++)
    {
        if (roteadores_vizinhos[i].id == id_next)
            break;
    }

    if (strategy != FOWARD)
        printf("...encaminhando via roteador: %d | %s:%d\n", id_next, roteadores_vizinhos[i].ip, roteadores_vizinhos[i].porta);

    memset((char *)&si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(roteadores_vizinhos[i].porta);

    if (inet_aton(roteadores_vizinhos[i].ip, &si_other.sin_addr) == 0)
    {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    if (sendto(sock, &packet, sizeof(struct pacote), 0, (struct sockaddr *)&si_other, slen) == -1)
    {
        die("sendto()");
    }
}

/**
 * 
 */
void verificar_pacote_retorno(pacote packet)
{
    int volta = 1;
    for (int i = 0; i < quantidade_vizinhos; i++)
    {
        if (vizinhos[i] == packet.id_font)
            volta = 0;
    }
    if (volta)
    {
        myvec_original[obter_index_por_id_roteador(packet.id_font)] = enlaces[obter_index_por_id_roteador(packet.id_font)];
        saida[obter_index_por_id_roteador(packet.id_font)] = packet.id_font;
        quantidade_vizinhos++;
        vizinhos[quantidade_vizinhos - 1] = packet.id_font;
    }
}

/**
 * 
 */
void verificar_enlaces()
{
    pthread_mutex_lock(&tableMutex);
    int mudou = 0;
    for (int i = 0; i < qt_nodos; i++)
    {
        if (unlinkRouter[i] > 2)
        {
            puts("\nverificaEnlaces\n");
            *(tabela_roteamento[i]) - 1;
            myvec_original[i] = -1;
            saida[i] = -1;
            int *mynewvec = copiar_vetor(myvec_original, qt_nodos);
            tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)] = mynewvec;
            mudou = 1;

            for (int j = 1; j < quantidade_vizinhos; j++)
            {
                if (vizinhos[j] == nodos_rede[i])
                {
                    if (j < quantidade_vizinhos - 1)
                    {
                        vizinhos[j] = vizinhos[quantidade_vizinhos - 1];
                    }
                    quantidade_vizinhos--;
                    unlinkRouter[i] = 0;
                }
            }
        }
    }
    pthread_mutex_unlock(&tableMutex);

    if (mudou)
        atualizar_tabela_roteamento();
}

/**
 * 
 */
void atualizar_tabela_roteamento()
{
    pthread_mutex_lock(&tableMutex);

    int *lastvec;
    lastvec = malloc(sizeof(int) * qt_nodos);
    lastvec = copiar_vetor(meus_vetores, QTD_MAXIMA_ROTEADORES);
    meus_vetores = copiar_vetor(myvec_original, QTD_MAXIMA_ROTEADORES);

    for (int i = 0; i < qt_nodos; i++)
    {
        if (!tabela_roteamento[i])
            continue;

        if (i == obter_index_por_id_roteador(*id_roteador_atual) || *(tabela_roteamento[i]) == -1)
            continue;

        for (int j = 0; j < qt_nodos; j++)
        {
            if (tabela_roteamento[i][j] == -1)
                continue;

            int novocusto = tabela_roteamento[i][j] + myvec_original[i];
            if (novocusto < meus_vetores[j] || meus_vetores[j] == -1)
            {
                meus_vetores[j] = novocusto;
                saida[j] = nodos_rede[i];
                if (novocusto > 52)
                {
                    printf("Detectado contagem ao infinito, enlace removido!\n");
                    meus_vetores[j] = -1;
                    saida[j] = -1;
                }
            }
        }
    }

    meus_vetores[obter_index_por_id_roteador(*id_roteador_atual)] = 0;
    saida[obter_index_por_id_roteador(*id_roteador_atual)] = -1;
    tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)] = meus_vetores;
    for (int j = 0; j < qt_nodos; j++)
    {
        if (tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)][j] == -1)
            saida[j] = -1;
    }

    pthread_mutex_unlock(&tableMutex);
    for (int i = 0; i < qt_nodos; i++)
    {
        if (lastvec[i] != meus_vetores[i])
        {
            printf("\n\nTabela atualizada ");
            printar_tabela_roteamento();

            enviar_meus_vetores();
            break;
        }
    }
}

/**
 * 
 */
void *thread_controle_vec()
{
    while (1)
    {
        verificar_enlaces();
        enviar_meus_vetores();
        sleep(30);
    }
}

/**
 * 
 */
void *thread_terminal()
{
    int i, slen = sizeof(si_other);
    pacote packet;

    while (1)
    {
        while (1)
        {
            printf("Enter router id:\n");
            scanf("%d", &packet.id_dest);
            if (packet.id_dest == roteadores_vizinhos[0].id)
                printf("Destino não alcançável, tente novamente.\n");
            else
                break;
        }

        printf("Enter message: ");
        __fpurge(stdin);
        fgets(packet.message, 100, stdin);

        packet.seq = ++seq;
        packet.type = DATA;
        packet.ack = 0;
        packet.id_font = roteadores_vizinhos[0].id;

        enviar_pacote(packet, ROUTE);
        pthread_mutex_lock(&timerMutex);
        tentativa = 0, confirmacao = 0;
        pthread_mutex_unlock(&timerMutex);

        while (1)
        {
            sleep(10);
            pthread_mutex_lock(&timerMutex);

            if (tentativa >= 3 || confirmacao)
            {
                pthread_mutex_unlock(&timerMutex);
                break;
            }
            else if (!confirmacao)
            {
                printf("Pacote %d não entregue. Tentando novamente", packet.seq);
                tentativa += 1;
                pthread_mutex_unlock(&timerMutex);
                enviar_pacote(packet, ROUTE);
            }
        }
        pthread_mutex_unlock(&timerMutex);
        tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)] = meus_vetores;
    }

    return 0;
}

/**
 * 
 */
void *thread_roteador(void *porta)
{
    int i, slen = sizeof(si_other), recv_len;
    int id_destino = -1;
    pacote packet;

    while (1)
    {
        if ((recv_len = recvfrom(sock, &packet, sizeof(struct pacote), 0, (struct sockaddr *)&si_other, &slen)) == -1)
        {
            die("recvfrom()");
        }

        id_destino = packet.id_dest;
        sleep(1);
        // printf("Pacote Chegado de %d -> Tipo: %s\n", packet.id_font, packet.type);
        if (id_destino != roteadores_vizinhos[0].id)
        {
            int id_next = saida[obter_index_por_id_roteador(id_destino)];

            if (packet.type == DATA)
            {
                printf("Roteador %d encaminhando mensagem com # sequência %d para o destino %d\n", roteadores_vizinhos[0].id, packet.seq, packet.id_dest);
            }
            else if (packet.type == CONTROL && packet.ack == 1)
            {
                printf("Roteador %d encaminhando confirmação de msg #seq:%d para o sender %d\n", roteadores_vizinhos[0].id, packet.seq, packet.id_dest);
            }
            enviar_pacote(packet, ROUTE);
        }
        else if (id_destino == roteadores_vizinhos[0].id && packet.type == DATA)
        {
            pacote response;
            response.type = CONTROL;
            response.ack = 1;
            response.id_font = id_destino;
            response.id_dest = packet.id_font;
            response.seq = packet.seq;

            printf("Pacote recebido de %s:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port));
            printf("Mensagem: %s\n", packet.message);
            puts("Enviando confirmação...");

            enviar_pacote(response, ROUTE);
        }
        else if (id_destino == roteadores_vizinhos[0].id && packet.ack == 1)
        {
            printf("Confirmação recebida de %s:%d, mensagem #seq:%d\n", inet_ntoa(si_other.sin_addr), ntohs(si_other.sin_port), packet.seq);
            pthread_mutex_lock(&timerMutex);
            confirmacao = 1;
            pthread_mutex_unlock(&timerMutex);
        }
        else if (id_destino == roteadores_vizinhos[0].id && packet.type == CONTROL)
        {
            verificar_pacote_retorno(packet);
            unlinkRouter[obter_index_por_id_roteador(packet.id_font)] = 0;
            tabela_roteamento[obter_index_por_id_roteador(packet.id_font)] = copiar_vetor(packet.sendervec, QTD_MAXIMA_ROTEADORES);
            atualizar_tabela_roteamento();
        }
    }
    return 0;
}

/*Add elemento no final da fila*/
void fila_entrada_add(pacote pacote_novo) {
    if(tamanho_atual_fila_entrada < QTD_MENSAGENS_MAX_FILA) {
        pthread_mutex_lock(&mutex_fila_entrada);
        fila_entrada.mensagens[tamanho_atual_fila_entrada] = pacote_novo;
        tamanho_atual_fila_entrada++;
        pthread_mutex_unlock(&mutex_fila_entrada);
    } else {
        printf("A fila de entrada não aceitou o pacote com a mensagem: \"%s\" pois ela já está cheia", pacote_novo.message);
    }
}

/*Remove elemento do inicio da fila*/
void fila_entrada_remove() {
    pthread_mutex_lock(&mutex_fila_entrada);
    for(int i = 0; i < tamanho_atual_fila_entrada; i++) {
        fila_entrada.mensagens[i] = fila_entrada.mensagens[i+1];
    }
    tamanho_atual_fila_entrada--;
    pthread_mutex_unlock(&mutex_fila_entrada);
}

pacote fila_entrada_get() {
    pthread_mutex_lock(&mutex_fila_entrada);
    pacote pacote = fila_entrada.mensagens[0];
    pthread_mutex_unlock(&mutex_fila_entrada);
    return pacote;
}

bool fila_entrada_tem_elementos() {
    pthread_mutex_lock(&mutex_fila_entrada);
    bool temElementos = (tamanho_atual_fila_entrada > 0) ? true : false;
    pthread_mutex_unlock(&mutex_fila_entrada);
    return temElementos;
}

/*Add elemento no final da fila*/
void fila_saida_add(pacote pacote_novo) {
    if(tamanho_atual_fila_saida < QTD_MENSAGENS_MAX_FILA) {
        pthread_mutex_lock(&mutex_fila_saida);
        fila_saida.mensagens[tamanho_atual_fila_saida] = pacote_novo;
        tamanho_atual_fila_saida++;
        pthread_mutex_unlock(&mutex_fila_saida);
    } else {
        printf("A fila de saída não aceitou o pacote com a mensagem: \"%s\" pois ela já está cheia", pacote_novo.message);
    }
}

/*Remove elemento do inicio da fila*/
void fila_saida_remove() {
    pthread_mutex_lock(&mutex_fila_saida);
    for(int i = 0; i < tamanho_atual_fila_saida; i++) {
        fila_saida.mensagens[i] = fila_saida.mensagens[i+1];
    }
    tamanho_atual_fila_saida--;
    pthread_mutex_unlock(&mutex_fila_saida);
}

pacote fila_saida_get() {
    pthread_mutex_lock(&mutex_fila_saida);
    pacote pacote = fila_saida.mensagens[0];
    pthread_mutex_unlock(&mutex_fila_saida);
    return pacote;
}

bool fila_saida_tem_elementos() {
    pthread_mutex_lock(&mutex_fila_saida);
    bool temElementos = (tamanho_atual_fila_saida > 0) ? true : false;
    pthread_mutex_unlock(&mutex_fila_saida);
    return temElementos;
}