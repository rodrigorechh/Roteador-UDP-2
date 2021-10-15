#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>
#include "headers/structures.h"

struct roteador *roteadores_vizinhos;
struct sockaddr_in socket_roteador, socket_externo;

int *id_roteador_atual;

pthread_mutex_t mutex_timer = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_tabela_roteamento = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_fila_entrada = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_fila_saida = PTHREAD_MUTEX_INITIALIZER;
fila_mensagens fila_entrada, fila_saida;

int socket_id, sequencial_pacote = 0, confirmacao = 0, qt_nodos = 0, quantidade_vizinhos = 1;
int remover_enlace[QTD_MAXIMA_ROTEADORES];
int nodos_rede[QTD_MAXIMA_ROTEADORES];
int *meus_vetores, *meus_vetores_origem, *enlaces, *mapeamento_saida, *saida_original;
int *tabela_roteamento[QTD_MAXIMA_ROTEADORES];
int vizinhos[QTD_MAXIMA_ROTEADORES];
int tamanho_atual_fila_entrada = 0, tamanho_atual_fila_saida = 0;

int main(int argc, char *argv[])
{
    inicializa_variaveis_globais();

    if (argc < 2)
    {
        printf("Execute: %s <id do roteado>\n", argv[0]);
        return 0;
    }

    *id_roteador_atual = atoi(argv[1]);

    if (DEBUG)
        printf("\n\nId do roteador do processo atual é %d\n", *id_roteador_atual);

    carregar_quantidade_nodos();
    carregar_links_roteadores();
    carregar_configuracoes_roteadores(vizinhos);

    printar_nodos_rede();
    printar_tabela_roteamento();
    printar_roteadores_vizinhos();

    instanciar_socket();
    sleep(2);

    pthread_t instancia_thread[3];
    pthread_create(&instancia_thread[0], NULL, thread_roteador, NULL);
    pthread_create(&instancia_thread[1], NULL, thread_terminal, NULL);
    pthread_create(&instancia_thread[2], NULL, thread_controle_vetores, NULL);
    pthread_join(instancia_thread[0], NULL);
    pthread_join(instancia_thread[1], NULL);
    pthread_join(instancia_thread[2], NULL);

    close(socket_id);

    return 0;
}

/**
 * Método para inicias algumas variáveis globais
 */
void inicializa_variaveis_globais()
{
    id_roteador_atual = malloc(sizeof(int));
    mapeamento_saida = malloc(sizeof(int) * QTD_MAXIMA_ROTEADORES);
    setar_valor_default_tabela_roteamento(QTD_MAXIMA_ROTEADORES);
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
int *copiar_vetor(int origem[], int tamanho)
{
    int *destino = malloc(sizeof(int) * tamanho);

    for (int i = 0; i < tamanho; i++)
        destino[i] = origem[i];

    return destino;
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

    return VAZIO;
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
        printf("[%d]", nodos_rede[i]);

    printf("\n");
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

        if (*(tabela_roteamento[i]) == VAZIO)
        {
            puts("N/A");
            continue;
        }

        for (int j = 0; j < qt_nodos; j++)
        {
            printf("[%d]", tabela_roteamento[i][j]);
        }

        printf("\n");
    }

    printf("\nsaida: ");

    for (int i = 0; i < qt_nodos; i++)
        printf("[%d]", mapeamento_saida[i]);

    printf("\n");

    printar_vizinhos();
}

/**
 * Printa os vizinhos do roteador atual
 */
void printar_vizinhos()
{
    printf("Vizinhos: ");

    for (int i = 1; i < quantidade_vizinhos; i++)
        printf("[%d]", vizinhos[i]);

    printf("\n");
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
 * Carrega os links entre o roteador atual e vizinhos
 */
void carregar_links_roteadores()
{
    int id_esquerdo, id_direito, custo_enlace;
    meus_vetores = malloc(sizeof(int) * QTD_MAXIMA_ROTEADORES);
    memset(meus_vetores, VAZIO, sizeof(int) * QTD_MAXIMA_ROTEADORES);

    meus_vetores[obter_index_por_id_roteador(*id_roteador_atual)] = 0;

    FILE *arquivo = fopen("configs/enlaces.config", "r");

    if (!arquivo)
        die("Não foi possível abrir o arquivo enlaces.config");

    vizinhos[0] = *id_roteador_atual;

    while (fscanf(arquivo, "%d %d %d", &id_esquerdo, &id_direito, &custo_enlace) != EOF)
    {
        int aux_id_esquerdo = 1;
        int aux_id_direito = 1;

        for (int i = 0; i < qt_nodos; i++)
        {
            if (id_esquerdo == nodos_rede[i])
                aux_id_esquerdo = 0;
            else if (id_direito == nodos_rede[i])
                aux_id_direito = 0;
        }

        if (aux_id_esquerdo)
            nodos_rede[qt_nodos] = id_esquerdo;
        if (aux_id_direito)
            nodos_rede[qt_nodos] = id_direito;

        for (int i = 0; i < qt_nodos; i++)
        {
            if (id_esquerdo == (*id_roteador_atual))
            {
                meus_vetores[obter_index_por_id_roteador(id_direito)] = custo_enlace;
                adicionar_vizinho_array(id_direito);
            }

            if (id_direito == (*id_roteador_atual))
            {
                meus_vetores[obter_index_por_id_roteador(id_esquerdo)] = custo_enlace;
                adicionar_vizinho_array(id_esquerdo);
            }
        }
    }

    fclose(arquivo);

    setar_valor_default_tabela_roteamento(qt_nodos);
    tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)] = meus_vetores;
    enlaces = copiar_vetor(meus_vetores, QTD_MAXIMA_ROTEADORES);
}

/**
 * Lê e extrai as configurações de roteadores que estão no arquivo
 * apenas para contar a quanitdade de nodos pré configurados na rede
 */
void carregar_quantidade_nodos()
{
    FILE *arquivo = fopen("configs/roteador.config", "r");
    int aux1, aux2;
    char aux3[32];

    if (!arquivo)
        die("Não foi possível abrir o arquivo roteador.config");

    while (fscanf(arquivo, "%d %d %s", &aux1, &aux2, aux3) != EOF)
    {
        qt_nodos++;
    }

    fclose(arquivo);

    if (DEBUG)
        printf("A rede esta pré configurada com %d roteadores\n", qt_nodos);
}

/**
 * Lê e extrai as configurações de roteadores que estão no arquivo
 */
void carregar_configuracoes_roteadores(int vizinhos[])
{
    int id_roteador, porta_roteador;
    char ip_roteador[32];
    roteadores_vizinhos = malloc(sizeof(struct roteador) * quantidade_vizinhos);

    memset(mapeamento_saida, VAZIO, sizeof(int) * QTD_MAXIMA_ROTEADORES);
    for (int i = 1; i < quantidade_vizinhos; i++)
        mapeamento_saida[obter_index_por_id_roteador(vizinhos[i])] = vizinhos[i];

    saida_original = copiar_vetor(mapeamento_saida, QTD_MAXIMA_ROTEADORES);
    meus_vetores_origem = copiar_vetor(meus_vetores, QTD_MAXIMA_ROTEADORES);

    for (int i = 0; i < quantidade_vizinhos; i++)
    {
        FILE *arquivo = fopen("configs/roteador.config", "r");
        if (!arquivo)
            die("Não foi possível abrir o arquivo roteador.config");

        while (fscanf(arquivo, "%d %d %s", &id_roteador, &porta_roteador, ip_roteador) != EOF)
        {
            if (vizinhos[i] != id_roteador)
                continue;

            roteadores_vizinhos[i].id = id_roteador;
            roteadores_vizinhos[i].porta = porta_roteador;
            strcpy(roteadores_vizinhos[i].ip, ip_roteador);
        }

        fclose(arquivo);
    }
}

/**
 * Método para criar o socket do roteador atual
 */
void instanciar_socket()
{
    if ((socket_id = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        die("Não foi possível instanciar o socket do roteador");

    memset((char *)&socket_roteador, 0, sizeof(socket_roteador));
    socket_roteador.sin_family = AF_INET;
    socket_roteador.sin_port = htons(roteadores_vizinhos[0].porta);
    socket_roteador.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(socket_id, (struct sockaddr *)&socket_roteador, sizeof(socket_roteador)) == -1)
        die("Não foi possível 'bindar' o socket do roteador");
}

/**
 * Método que faz o envio da sua tabela de roteamento para seus vizinhos
 */
void enviar_meus_vetores()
{
    pacote pacote;
    pacote.id_origem = *id_roteador_atual;
    pacote.tipo = TIPO_PACOTE_CONTROLE;
    pacote.confirmacao = 0;

    for (int i = 0; i < qt_nodos; i++)
        pacote.vetores_tabela_roteamento[i] = meus_vetores[i];

    for (int i = 1; i < quantidade_vizinhos; i++)
    {
        pacote.id_destino = vizinhos[i];
        remover_enlace[obter_index_por_id_roteador(pacote.id_destino)] += 1;
        enviar_pacote(pacote, COMPORTAMENTO_PACOTE_TABELA);
    }
}

/**
 * Enviar o pacote para o noto de destino
 * Caso o destino não seja vizinho, busca qual é o próximo
 * nodo para chegar até o vizinho
 */
void enviar_pacote(pacote packet, int comportamento)
{
    int id_vizinho_encaminhar_pacote, i, tamanho_socket = sizeof(socket_externo);

    if (comportamento == COMPORTAMENTO_PACOTE_ROTEAMENTO)
    {
        pthread_mutex_lock(&mutex_tabela_roteamento);
        id_vizinho_encaminhar_pacote = mapeamento_saida[obter_index_por_id_roteador(packet.id_destino)];
        pthread_mutex_unlock(&mutex_tabela_roteamento);
    }
    else if (comportamento == COMPORTAMENTO_PACOTE_TABELA)
    {
        id_vizinho_encaminhar_pacote = packet.id_destino;
    }

    if (id_vizinho_encaminhar_pacote == VAZIO)
    {
        puts("O destino não é alcançável");
        return;
    }

    for (i = 1; i < quantidade_vizinhos; i++)
    {
        if (roteadores_vizinhos[i].id == id_vizinho_encaminhar_pacote)
            break;
    }

    if (comportamento != COMPORTAMENTO_PACOTE_TABELA)
        printf("========> Encaminhando pacote via roteador: %d | %s:%d\n", id_vizinho_encaminhar_pacote, roteadores_vizinhos[i].ip, roteadores_vizinhos[i].porta);

    memset((char *)&socket_externo, 0, sizeof(socket_externo));
    socket_externo.sin_family = AF_INET;
    socket_externo.sin_port = htons(roteadores_vizinhos[i].porta);

    if (inet_aton(roteadores_vizinhos[i].ip, &socket_externo.sin_addr) == 0)
        die("O endereço do socket vizinho é inválido");

    if (sendto(socket_id, &packet, sizeof(struct pacote), 0, (struct sockaddr *)&socket_externo, tamanho_socket) == -1)
        die("Falha ao enviar informaçãos ao socket vizinho");
}

/**
 * Manipula o pacote de retorno após o envio do pacote
 */
void verificar_pacote_retorno(pacote packet)
{
    int is_retorno = 1;
    for (int i = 0; i < quantidade_vizinhos; i++)
    {
        if (vizinhos[i] == packet.id_origem)
            is_retorno = 0;
    }

    if (!is_retorno)
        return;

    meus_vetores_origem[obter_index_por_id_roteador(packet.id_origem)] = enlaces[obter_index_por_id_roteador(packet.id_origem)];
    mapeamento_saida[obter_index_por_id_roteador(packet.id_origem)] = packet.id_origem;
    quantidade_vizinhos++;
    vizinhos[quantidade_vizinhos - 1] = packet.id_origem;
}

/**
 * 
 */
void verificar_enlaces()
{
    pthread_mutex_lock(&mutex_tabela_roteamento);
    int is_ocorreu_mudanca = 0;

    for (int i = 0; i < qt_nodos; i++)
    {
        if (remover_enlace[i] > 2)
        {
            ;
            if (tabela_roteamento[i] != NULL)
                *(tabela_roteamento[i]) = VAZIO;
            meus_vetores_origem[i] = VAZIO;
            mapeamento_saida[i] = VAZIO;

            int *novo_vetor = copiar_vetor(meus_vetores_origem, qt_nodos);
            tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)] = novo_vetor;
            is_ocorreu_mudanca = 1;

            for (int j = 1; j < quantidade_vizinhos; j++)
            {
                if (vizinhos[j] == nodos_rede[i])
                {
                    if (j < quantidade_vizinhos - 1)
                    {
                        vizinhos[j] = vizinhos[quantidade_vizinhos - 1];
                    }

                    quantidade_vizinhos--;
                    remover_enlace[i] = 0;
                }
            }
        }
    }

    pthread_mutex_unlock(&mutex_tabela_roteamento);

    if (is_ocorreu_mudanca)
        atualizar_tabela_roteamento();
}

/**
 * Método que recalcula a saída de pacotes
 * A partir do vetor recebido pelos vizinhos
 * e seus próprios vizinhos, o método determina
 * qual o próximo vizinho para chegar até outro destino
 */
void atualizar_tabela_roteamento()
{
    pthread_mutex_lock(&mutex_tabela_roteamento);

    int *vetor_antes_modificacao = malloc(sizeof(int) * qt_nodos);
    vetor_antes_modificacao = copiar_vetor(meus_vetores, QTD_MAXIMA_ROTEADORES);
    meus_vetores = copiar_vetor(meus_vetores_origem, QTD_MAXIMA_ROTEADORES);

    for (int i = 0; i < qt_nodos; i++)
    {
        if (!tabela_roteamento[i])
            continue;

        if (i == obter_index_por_id_roteador(*id_roteador_atual) || *(tabela_roteamento[i]) == VAZIO)
            continue;

        for (int j = 0; j < qt_nodos; j++)
        {
            if (tabela_roteamento[i][j] == VAZIO)
                continue;

            int novo_custo = tabela_roteamento[i][j] + meus_vetores_origem[i];
            if (novo_custo < meus_vetores[j] || meus_vetores[j] == VAZIO)
            {
                meus_vetores[j] = novo_custo;
                mapeamento_saida[j] = nodos_rede[i];

                if (novo_custo > CUSTO_MAXIMO_CONTAGEM_INFINITO)
                {
                    printf("Possível 'count to infinity', o enlace será removido\n");
                    meus_vetores[j] = VAZIO;
                    mapeamento_saida[j] = VAZIO;
                }
            }
        }
    }

    meus_vetores[obter_index_por_id_roteador(*id_roteador_atual)] = 0;
    mapeamento_saida[obter_index_por_id_roteador(*id_roteador_atual)] = VAZIO;
    tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)] = meus_vetores;

    for (int j = 0; j < qt_nodos; j++)
    {
        if (tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)][j] == VAZIO)
            mapeamento_saida[j] = VAZIO;
    }

    pthread_mutex_unlock(&mutex_tabela_roteamento);
    for (int i = 0; i < qt_nodos; i++)
    {
        if (vetor_antes_modificacao[i] != meus_vetores[i])
        {
            if (DEBUG)
                printf("\n\nTabela de roteamento foi atualizada ");
            printar_tabela_roteamento();
            enviar_meus_vetores();

            break;
        }
    }
}

/**
 * Thread que controla enlaces
 * Tanto recebido dos vizinhos quanto 
 * replicar para outros vizinhos
 */
void *thread_controle_vetores()
{
    while (1)
    {
        verificar_enlaces();
        enviar_meus_vetores();
        sleep(TIMEOUT_COMPARTILHAMENTO_TABELA_ROTEAMENTOS);
    }
}

/**
 * Thread que controla o terminar de interação com o usuário
 */
void *thread_terminal()
{
    int i, slen = sizeof(socket_externo);
    pacote packet;

    while (1)
    {
        while (1)
        {
            printf("Digite o destino da mensagem:\n");
            scanf("%d", &packet.id_destino);
            if (packet.id_destino == roteadores_vizinhos[0].id)
                printf("O destino não é alcançável\n");
            else
                break;
        }

        getchar();
        printf("Digite a mensagem para o destino %d: ", packet.id_destino);
        fgets(packet.conteudo, 100, stdin);

        packet.sequencia = ++sequencial_pacote;
        packet.tipo = TIPO_PACOTE_DADO;
        packet.confirmacao = 0;
        packet.id_origem = roteadores_vizinhos[0].id;

        enviar_pacote(packet, COMPORTAMENTO_PACOTE_ROTEAMENTO);

        pthread_mutex_lock(&mutex_timer);
        confirmacao = 0;
        pthread_mutex_unlock(&mutex_timer);

        while (1)
        {
            sleep(10);
            pthread_mutex_lock(&mutex_timer);

            if (confirmacao)
            {
                pthread_mutex_unlock(&mutex_timer);
                break;
            }
            else
            {
                printf("Pacote %d não entregue. Tentando novamente", packet.sequencia);
                pthread_mutex_unlock(&mutex_timer);
                enviar_pacote(packet, COMPORTAMENTO_PACOTE_ROTEAMENTO);
            }
        }

        pthread_mutex_unlock(&mutex_timer);
        tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)] = meus_vetores;
    }

    return 0;
}

/**
 * Thread que controla o recebimento de pacotes
 */
void *thread_roteador()
{
    int i, slen = sizeof(socket_externo), recv_len;
    int id_destino = VAZIO;
    pacote packet;

    while (1)
    {
        if ((recv_len = recvfrom(socket_id, &packet, sizeof(struct pacote), 0, (struct sockaddr *)&socket_externo, &slen)) == -1)
        {
            die("recvfrom()");
        }

        id_destino = packet.id_destino;
        sleep(1);
        // printf("Pacote Chegado de %d -> Tipo: %s\n", packet.id_origem, packet.type);
        if (id_destino != roteadores_vizinhos[0].id)
        {
            int id_next = mapeamento_saida[obter_index_por_id_roteador(id_destino)];

            if (packet.tipo == TIPO_PACOTE_DADO)
            {
                printf("Roteador %d encaminhando mensagem com # sequência %d para o destino %d\n", roteadores_vizinhos[0].id, packet.sequencia, packet.id_destino);
            }
            else if (packet.tipo == TIPO_PACOTE_CONTROLE && packet.confirmacao == 1)
            {
                printf("Roteador %d encaminhando confirmação de msg #sequencia:%d para o sender %d\n", roteadores_vizinhos[0].id, packet.sequencia, packet.id_destino);
            }
            enviar_pacote(packet, COMPORTAMENTO_PACOTE_ROTEAMENTO);
        }
        else if (id_destino == roteadores_vizinhos[0].id && packet.tipo == TIPO_PACOTE_DADO)
        {
            pacote response;
            response.tipo = TIPO_PACOTE_CONTROLE;
            response.confirmacao = 1;
            response.id_origem = id_destino;
            response.id_destino = packet.id_origem;
            response.sequencia = packet.sequencia;

            printf("Pacote recebido de %s:%d\n", inet_ntoa(socket_externo.sin_addr), ntohs(socket_externo.sin_port));
            printf("Mensagem: %s\n", packet.conteudo);
            puts("Enviando confirmação...");

            enviar_pacote(response, COMPORTAMENTO_PACOTE_ROTEAMENTO);
        }
        else if (id_destino == roteadores_vizinhos[0].id && packet.confirmacao == 1)
        {
            printf("Confirmação recebida de %s:%d, mensagem #sequencia:%d\n", inet_ntoa(socket_externo.sin_addr), ntohs(socket_externo.sin_port), packet.sequencia);
            pthread_mutex_lock(&mutex_timer);
            confirmacao = 1;
            pthread_mutex_unlock(&mutex_timer);
        }
        else if (id_destino == roteadores_vizinhos[0].id && packet.tipo == TIPO_PACOTE_CONTROLE)
        {
            verificar_pacote_retorno(packet);
            remover_enlace[obter_index_por_id_roteador(packet.id_origem)] = 0;
            tabela_roteamento[obter_index_por_id_roteador(packet.id_origem)] = copiar_vetor(packet.vetores_tabela_roteamento, QTD_MAXIMA_ROTEADORES);
            atualizar_tabela_roteamento();
        }
    }
    return 0;
}

/*Add elemento no final da fila*/
void fila_entrada_add(pacote pacote_novo)
{
    if (tamanho_atual_fila_entrada < QTD_MENSAGENS_MAX_FILA)
    {
        pthread_mutex_lock(&mutex_fila_entrada);
        fila_entrada.mensagens[tamanho_atual_fila_entrada] = pacote_novo;
        tamanho_atual_fila_entrada++;
        pthread_mutex_unlock(&mutex_fila_entrada);
    }
    else
    {
        printf("A fila de entrada não aceitou o pacote com a mensagem: \"%s\" pois ela já está cheia", pacote_novo.conteudo);
    }
}

/*Remove elemento do inicio da fila*/
void fila_entrada_remove()
{
    pthread_mutex_lock(&mutex_fila_entrada);
    for (int i = 0; i < tamanho_atual_fila_entrada; i++)
    {
        fila_entrada.mensagens[i] = fila_entrada.mensagens[i + 1];
    }
    tamanho_atual_fila_entrada--;
    pthread_mutex_unlock(&mutex_fila_entrada);
}

pacote fila_entrada_get()
{
    pthread_mutex_lock(&mutex_fila_entrada);
    pacote pacote = fila_entrada.mensagens[0];
    pthread_mutex_unlock(&mutex_fila_entrada);
    return pacote;
}

int fila_entrada_tem_elementos()
{
    pthread_mutex_lock(&mutex_fila_entrada);
    int temElementos = (tamanho_atual_fila_entrada > 0) ? 1 : 0;
    pthread_mutex_unlock(&mutex_fila_entrada);
    return temElementos;
}

/*Add elemento no final da fila*/
void fila_saida_add(pacote pacote_novo)
{
    if (tamanho_atual_fila_saida < QTD_MENSAGENS_MAX_FILA)
    {
        pthread_mutex_lock(&mutex_fila_saida);
        fila_saida.mensagens[tamanho_atual_fila_saida] = pacote_novo;
        tamanho_atual_fila_saida++;
        pthread_mutex_unlock(&mutex_fila_saida);
    }
    else
    {
        printf("A fila de saída não aceitou o pacote com a mensagem: \"%s\" pois ela já está cheia", pacote_novo.conteudo);
    }
}

/*Remove elemento do inicio da fila*/
void fila_saida_remove()
{
    pthread_mutex_lock(&mutex_fila_saida);
    for (int i = 0; i < tamanho_atual_fila_saida; i++)
    {
        fila_saida.mensagens[i] = fila_saida.mensagens[i + 1];
    }
    tamanho_atual_fila_saida--;
    pthread_mutex_unlock(&mutex_fila_saida);
}

pacote fila_saida_get()
{
    pthread_mutex_lock(&mutex_fila_saida);
    pacote pacote = fila_saida.mensagens[0];
    pthread_mutex_unlock(&mutex_fila_saida);
    return pacote;
}

int fila_saida_tem_elementos()
{
    pthread_mutex_lock(&mutex_fila_saida);
    int temElementos = (tamanho_atual_fila_saida > 0) ? 1 : 0;
    pthread_mutex_unlock(&mutex_fila_saida);
    return temElementos;
}