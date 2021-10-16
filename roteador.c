#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>
#include "structures.h"

/**
 * Array com a configuração dos roteadores vizinhos
 * O array tem o tamanho da quantidade de nodos da rede
 * mas apenas o índices referente aos vizinhos são preenchidos
 * Por padrão, o primeiro índice possui as configurações do roteador 
 * do processo atual
 */
struct roteador *roteadores_vizinhos;

/**
 * Instancia do socket do roteador do processo atual
 * e instância do socket de comunicação externa
 */
struct sockaddr_in socket_roteador, socket_externo;

/**
 * Id do roteador do processo atual
 */
int *id_roteador_atual;

/**
 * Mutex que controla a confirmação da entrega do pacote
 */
pthread_mutex_t mutex_timer = PTHREAD_MUTEX_INITIALIZER;

/**
 * 
 * Mutex que controla o acesso a tabela de roteamento
 */
pthread_mutex_t mutex_tabela_roteamento = PTHREAD_MUTEX_INITIALIZER;

/**
 * Mutex que controla o acesso as filas, tanto de entrada
 * quanto a fila de saída
 */
pthread_mutex_t mutex_fila_entrada = PTHREAD_MUTEX_INITIALIZER, mutex_fila_saida = PTHREAD_MUTEX_INITIALIZER;

/**
 * Filas que armazenam os pacotes pendentes, tanto de entrada
 * quanto a fila de saída
 */
fila_mensagens fila_entrada, fila_saida;

/**
 * socket_id => identificador do socket atual
 * sequencial_pacote => controle de identificador de pacotes
 * confirmacao => auxiliar para controlar se o pacote foi recebido ou não
 * qt_nodos => controla a quantidade de nodos que a rede possui
 * quantidade_vizinhos => controla a quantidade de vizinhos que o roteador possui
 */
int socket_id, sequencial_pacote = 0, confirmacao = 0, qt_nodos = 0, quantidade_vizinhos = 1;

/**
 * Armazena os índices aonde a tabela deve desconsiderar para
 * ser atualizada com novas informações
 */
int remover_enlace[QTD_MAXIMA_ROTEADORES];

/**
 * Array com mapeamento dos nodos da rede
 * Quando a rede é iniciada, o processo busca a quantidade
 * de roteadores que estão no arquivo de configuração
 * Após isso, é feito o mapeamento de índice => id, com isso
 * o sistema sabe que os arrays de índice, cada índice se refere
 * a um roteador específico
 * 
 * Ex: nodos_rede       = [ 3, 1, 2, 4]
 *     mapeamento_saida = [-1, 1, 4, 1]
 * 
 * Isso significa que para sair do roteador 3 (id do exemplo)
 * e chegar até o roteador 2 (índice 2), o sistema busca no
 * mapeamento de saída o índce 2 (roteador 4). Então saindo
 * de 3 e indo para o 2, o primeiro salto é para o roteador 4
 */
int nodos_rede[QTD_MAXIMA_ROTEADORES];

/**
 * A partir do que esta documentando em nodos_rede, o mapeamento
 * de saída é calculado conforme recebe as tabelas dos vizinhos.
 * Com isso, esse array esta sempre atualizado com o melhor 
 * caminhos, sendo necessário apenas buscar pelo índice, e o valor
 * no índice é o id do roteador que o pacote deve ser enviado
 * 
 * mapeamento_saida_original é apenas um array utilizado como
 * auxiliar durante o processamento do algoritmo de melhor caminho
 */
int *mapeamento_saida, *mapeamento_saida_original;

/**
 * Array que armazena o custo do enlace para cada roteador
 * 
 * meus_vetores_original e enlaces são arrays utilizados como
 * auxiliar durante o processamento do algoritmo de melhor caminho
 */
int *meus_vetores, *meus_vetores_original, *enlaces;

/**
 * Array de duas dimensões que armazena a tabela de roteamento
 * do roteador atual e dos roteadores recebeidos por seus
 * vizinhos
 */
int *tabela_roteamento[QTD_MAXIMA_ROTEADORES];

/**
 * Array que armazena os vizinhos diretos do roteador
 */
int vizinhos[QTD_MAXIMA_ROTEADORES];

/**
 * Auxilia no controle de movimentação de pacotes na fila
 */
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
    pthread_create(&instancia_thread[0], NULL, thread_receiver, NULL);
    pthread_create(&instancia_thread[1], NULL, thread_packet_handler, NULL);
    pthread_create(&instancia_thread[2], NULL, thread_sender, NULL);
    pthread_create(&instancia_thread[3], NULL, thread_terminal, NULL);
    pthread_create(&instancia_thread[4], NULL, thread_controle_vetores, NULL);

    pthread_join(instancia_thread[0], NULL);
    pthread_join(instancia_thread[1], NULL);
    pthread_join(instancia_thread[2], NULL);
    pthread_join(instancia_thread[3], NULL);
    pthread_join(instancia_thread[4], NULL);

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
void die(char *texto)
{
    perror(texto);
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
    printf("\n\n.::INFORMAÇÕES BÁSICAS::.\n");

    printf("Id roteador: %d - ip %s:%d\n", roteadores_vizinhos[0].id, roteadores_vizinhos[0].ip, roteadores_vizinhos[0].porta);

    printf("Roteadores vizinhos:\n");
    for (int i = 1; i < quantidade_vizinhos; i++)
        printf("Id roteador: %d - ip %s:%d\n", roteadores_vizinhos[i].id, roteadores_vizinhos[i].ip, roteadores_vizinhos[i].porta);

    printf("\n\n");
}

/**
 * Printa nodos da rede
 */
void printar_nodos_rede()
{
    printf("\n\n.::NODOS DA REDE::.\n");

    printf("[");
    for (int i = 0; i < qt_nodos; i++)
        printf("%d, ", nodos_rede[i]);
    printf("]\n");

    printf("\n");
}

/**
 * Printa a tabela de roteamento
 */
void printar_tabela_roteamento()
{
    printf("\n\n.::TABELA DE ROTEAMENTO::.\n");

    for (int i = 0; i < qt_nodos; i++)
    {
        printf("\nId %d: ", nodos_rede[i]);

        if (tabela_roteamento[i] == NULL || *(tabela_roteamento[i]) == VAZIO)
        {
            printf(" --Sem tabela");
            continue;
        }

        printf("[");
        for (int j = 0; j < qt_nodos; j++)
            printf("%d, ", tabela_roteamento[i][j]);
        printf("]");
    }

    printf("\n\n.::MAPEAMENTO DE SAÍDA::.\n");

    printf("[");
    for (int i = 0; i < qt_nodos; i++)
        printf("%d, ", mapeamento_saida[i]);
    printf("]\n");

    printar_vizinhos();
    printar_meus_vetores();
}

/**
 * Printa os vizinhos do roteador atual
 */
void printar_vizinhos()
{
    printf("\n\n.::VIZINHOS::.\n");

    printf("[");
    for (int i = 0; i < quantidade_vizinhos; i++)
        printf("%d, ", vizinhos[i]);
    printf("]\n");
}

/**
 * Printa o array de vetores
 */
void printar_meus_vetores()
{
    printf("\n\n.::MEU VETORES::.\n");

    printf("[");
    for (int i = 0; i < qt_nodos; i++)
        printf("%d, ", meus_vetores[i]);
    printf("]\n");
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

    FILE *arquivo = fopen("enlaces.config", "r");

    if (!arquivo)
        die("Não foi possível abrir o arquivo enlaces.config");

    vizinhos[0] = *id_roteador_atual;

    int controle_indice_nodos = 0;
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
        {
            nodos_rede[controle_indice_nodos] = id_esquerdo;
            controle_indice_nodos++;
        }

        if (aux_id_direito)
        {
            nodos_rede[controle_indice_nodos] = id_direito;
            controle_indice_nodos++;
        }

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
    printar_nodos_rede();
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
    FILE *arquivo = fopen("roteador.config", "r");
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

    mapeamento_saida_original = copiar_vetor(mapeamento_saida, QTD_MAXIMA_ROTEADORES);
    meus_vetores_original = copiar_vetor(meus_vetores, QTD_MAXIMA_ROTEADORES);

    for (int i = 0; i < quantidade_vizinhos; i++)
    {
        FILE *arquivo = fopen("roteador.config", "r");
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

        mensagem msg = {.pacote = pacote, .comportamento = COMPORTAMENTO_PACOTE_TABELA};
        fila_saida_add(msg);
    }
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

    meus_vetores_original[obter_index_por_id_roteador(packet.id_origem)] = enlaces[obter_index_por_id_roteador(packet.id_origem)];
    mapeamento_saida[obter_index_por_id_roteador(packet.id_origem)] = packet.id_origem;
    quantidade_vizinhos++;
    vizinhos[quantidade_vizinhos - 1] = packet.id_origem;
}

/**
 * Método que verificar se há alguma informação a desconsiderada
 * Caso exista, o método remove informações da tabela de
 * reteamento para serem atualizadas posteriormente
 */
void verificar_enlaces()
{
    pthread_mutex_lock(&mutex_tabela_roteamento);
    int is_ocorreu_mudanca = 0;

    for (int i = 0; i < qt_nodos; i++)
    {
        if (remover_enlace[i] <= 2)
            continue;

        meus_vetores_original[i] = VAZIO;
        mapeamento_saida[i] = VAZIO;
        if (tabela_roteamento[i] != NULL)
            *(tabela_roteamento[i]) = VAZIO;

        int *novo_vetor = copiar_vetor(meus_vetores_original, qt_nodos);
        tabela_roteamento[obter_index_por_id_roteador(*id_roteador_atual)] = novo_vetor;
        is_ocorreu_mudanca = 1;

        for (int j = 1; j < quantidade_vizinhos; j++)
        {
            if (vizinhos[j] == nodos_rede[i])
            {
                if (j < quantidade_vizinhos - 1)
                    vizinhos[j] = vizinhos[quantidade_vizinhos - 1];

                quantidade_vizinhos--;
                remover_enlace[i] = 0;
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
    meus_vetores = copiar_vetor(meus_vetores_original, QTD_MAXIMA_ROTEADORES);

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

            int novo_custo = tabela_roteamento[i][j] + meus_vetores_original[i];
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
 * Adiciona pacote na fila de manipulação
 */
void fila_entrada_add(mensagem mensagem_nova)
{
    if (tamanho_atual_fila_entrada < QTD_MENSAGENS_MAX_FILA)
    {
        pthread_mutex_lock(&mutex_fila_entrada);
        fila_entrada.mensagens[tamanho_atual_fila_entrada] = mensagem_nova;
        tamanho_atual_fila_entrada++;
        pthread_mutex_unlock(&mutex_fila_entrada);
    }
    else
    {
        printf("A fila de entrada não aceitou um novo pacote pois ela já está cheia");
    }
}

/**
 * Remove item da fila de manipulação
 */
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

/**
 * Método auxiliar para retornar o item na fila de manipulação
 */
mensagem fila_entrada_get()
{
    pthread_mutex_lock(&mutex_fila_entrada);
    mensagem mensagem = fila_entrada.mensagens[0];
    pthread_mutex_unlock(&mutex_fila_entrada);
    return mensagem;
}

/**
 * Método auxiliar para retornar se há itens na manipulação
 */
int fila_entrada_tem_elementos()
{
    pthread_mutex_lock(&mutex_fila_entrada);
    int has_elemento = (tamanho_atual_fila_entrada > 0) ? 1 : 0;
    pthread_mutex_unlock(&mutex_fila_entrada);
    return has_elemento;
}

/**
 * Adiciona pacote na fila de saída
 */
void fila_saida_add(mensagem mensagem_nova)
{
    if (tamanho_atual_fila_saida < QTD_MENSAGENS_MAX_FILA)
    {
        pthread_mutex_lock(&mutex_fila_saida);
        fila_saida.mensagens[tamanho_atual_fila_saida] = mensagem_nova;
        tamanho_atual_fila_saida++;
        pthread_mutex_unlock(&mutex_fila_saida);
    }
    else
    {
        printf("A fila de saída não aceitou um novo pacote pois ela já está cheia");
    }
}

/**
 * Remove item da fila de saída
 */
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


/**
 * Método auxiliar para retornar o item na fila de saída
 */
mensagem fila_saida_get()
{
    pthread_mutex_lock(&mutex_fila_saida);
    mensagem mensagem = fila_saida.mensagens[0];
    pthread_mutex_unlock(&mutex_fila_saida);
    return mensagem;
}

/**
 * Método auxiliar para retornar se há itens na saída
 */
int fila_saida_tem_elementos()
{
    pthread_mutex_lock(&mutex_fila_saida);
    int has_elemento = (tamanho_atual_fila_saida > 0) ? 1 : 0;
    pthread_mutex_unlock(&mutex_fila_saida);
    return has_elemento;
}

/**
 * Enviar o pacote para o noto de destino
 * Caso o destino não seja vizinho, busca qual é o próximo
 * nodo para chegar até o vizinho
 */
void *thread_sender()
{
    int id_vizinho_encaminhar_pacote, i, tamanho_socket = sizeof(struct sockaddr_in);
    struct sockaddr_in socket_externo;
    while (1)
    {
        if (fila_saida_tem_elementos())
        {
            mensagem msg = fila_saida_get();
            fila_saida_remove();

            pacote packet = msg.pacote;
            int comportamento = msg.comportamento;

            if (DEBUG)
            {
                if (comportamento == COMPORTAMENTO_PACOTE_ROTEAMENTO)
                    printf("Enviando pacote com finalidade de roteamento de pacote\n");
                else
                    printf("Enviando pacote com finalidade de compartilhamento de tabela\n");
            }

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
            }
            else
            {
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
        }
    }
}

/**
 * Thread que controla recebimentos
 * Quando a thread recebe um pacote, adiciona
 * na fila de entrada para ser processada
 */
void *thread_receiver()
{
    struct sockaddr_in socket_externo;
    int tamanho_socket = sizeof(socket_externo), tamanho_recebimento;
    int id_destino = -1;
    pacote packet;
    while (1)
    {
        if ((tamanho_recebimento = recvfrom(socket_id, &packet, sizeof(struct pacote), 0, (struct sockaddr *)&socket_externo, &tamanho_socket)) == -1)
            die("Ocorreu uma falha no recimento de informação do socket");

        mensagem mensagem = {.pacote = packet, .socket_externo = socket_externo};
        fila_entrada_add(mensagem);
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
    int i, tamanho_socket = sizeof(socket_externo);
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

        mensagem msg = {.pacote = packet, .comportamento = COMPORTAMENTO_PACOTE_ROTEAMENTO};
        fila_saida_add(msg);

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
                mensagem mensagem = {.pacote = packet, .comportamento = COMPORTAMENTO_PACOTE_ROTEAMENTO};
                fila_saida_add(mensagem);
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
void *thread_packet_handler()
{
    struct sockaddr_in socket_externo;
    int i, tamanho_socket = sizeof(socket_externo);
    int id_destino = -1;
    pacote response;
    pacote pacote;

    while (1)
    {
        if (fila_entrada_tem_elementos())
        {
            mensagem msg = fila_entrada_get();
            fila_entrada_remove();
            pacote = msg.pacote;
            socket_externo = msg.socket_externo;

            id_destino = pacote.id_destino;
            sleep(1);

            if (id_destino != roteadores_vizinhos[0].id)
            {
                int id_next = mapeamento_saida[obter_index_por_id_roteador(id_destino)];

                if (pacote.tipo == TIPO_PACOTE_DADO)
                {
                    printf("Roteador %d encaminhando mensagem com #sequência %d para o destino %d\n", roteadores_vizinhos[0].id, pacote.sequencia, pacote.id_destino);
                }
                else if (pacote.tipo == TIPO_PACOTE_CONTROLE && pacote.confirmacao == 1)
                {
                    printf("Roteador %d encaminhando confirmação de mensagem #sequência:%d para o sender %d\n", roteadores_vizinhos[0].id, pacote.sequencia, pacote.id_destino);
                }
                mensagem msg2 = {.pacote = pacote, .comportamento = COMPORTAMENTO_PACOTE_ROTEAMENTO};
                fila_saida_add(msg2);
            }
            else if (id_destino == roteadores_vizinhos[0].id && pacote.tipo == TIPO_PACOTE_DADO)
            {
                response.tipo = TIPO_PACOTE_CONTROLE;
                response.confirmacao = 1;
                response.id_origem = id_destino;
                response.id_destino = pacote.id_origem;
                response.sequencia = pacote.sequencia;

                printf("Pacote recebido de %s:%d\n", inet_ntoa(socket_externo.sin_addr), ntohs(socket_externo.sin_port));
                printf("\n\n\n\nMENSAGEM ===> %s\n\n\n", pacote.conteudo);
                if (DEBUG)
                    printf("Enviando confirmação para %d", pacote.id_origem);

                mensagem msg = {.pacote = response, .comportamento = COMPORTAMENTO_PACOTE_ROTEAMENTO};
                fila_saida_add(msg);
            }
            else if (id_destino == roteadores_vizinhos[0].id && pacote.confirmacao == 1)
            {
                printf("Confirmação recebida de %s:%d, mensagem #sequência:%d\n", inet_ntoa(socket_externo.sin_addr), ntohs(socket_externo.sin_port), pacote.sequencia);
                pthread_mutex_lock(&mutex_timer);
                confirmacao = 1;
                pthread_mutex_unlock(&mutex_timer);
            }
            else if (id_destino == roteadores_vizinhos[0].id && pacote.tipo == TIPO_PACOTE_CONTROLE)
            {
                verificar_pacote_retorno(pacote);
                remover_enlace[obter_index_por_id_roteador(pacote.id_origem)] = 0;
                tabela_roteamento[obter_index_por_id_roteador(pacote.id_origem)] = copiar_vetor(pacote.vetores_tabela_roteamento, QTD_MAXIMA_ROTEADORES);
                atualizar_tabela_roteamento();
            }
        }
    }
    return 0;
}