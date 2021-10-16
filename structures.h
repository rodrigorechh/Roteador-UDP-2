#define BUFLEN 100
#define QTD_MAXIMA_ROTEADORES 10
#define QTD_MENSAGENS_MAX_FILA 100
#define DEBUG 0
#define VAZIO -1
#define CUSTO_MAXIMO_CONTAGEM_INFINITO 70 // valor considerado em vista da configuração de enlaces

#define TIMEOUT_COMPARTILHAMENTO_TABELA_ROTEAMENTOS 30

#define TIPO_PACOTE_DADO 0
#define TIPO_PACOTE_CONTROLE 1

#define COMPORTAMENTO_PACOTE_ROTEAMENTO 0
#define COMPORTAMENTO_PACOTE_TABELA 1

typedef struct roteador
{
    int id;
    int porta;
    char ip[32];
} roteador;

typedef struct pacote
{
    int id_destino;
    int id_origem;
    int sequencia;
    char conteudo[150];
    int tipo;
    int confirmacao;
    int vetores_tabela_roteamento[QTD_MAXIMA_ROTEADORES];
} pacote;

typedef struct mensagem
{
    pacote pacote;
    struct sockaddr_in socket_externo;
    int comportamento;
} mensagem;//struct trocada entre receiver, sender e package_handler.

typedef struct fila_mensagens{
    mensagem mensagens[QTD_MENSAGENS_MAX_FILA];
} fila_mensagens;

void die(char *s);
int *copiar_vetor(int vetor[], int tamanho);
void inicializa_variaveis_globais();
int obter_index_por_id_roteador(int id);
void printar_roteadores_vizinhos();
void printar_nodos_rede();
void printar_tabela_roteamento();
void printar_vizinhos();
void printar_meus_vetores();
void adicionar_vizinho_array(int id);
void setar_valor_default_tabela_roteamento(int quantidade_indices);
void carregar_links_roteadores();
void carregar_quantidade_nodos();
void carregar_configuracoes_roteadores(int vizinhos[]);
void instanciar_socket();
void enviar_meus_vetores();
void verificar_pacote_retorno(pacote packet);
void verificar_enlaces();
void atualizar_tabela_roteamento();
void *thread_receiver();
void *thread_sender();
void *thread_packet_handler();
void *thread_controle_vetores();
void *thread_terminal();
void *thread_roteador();
void fila_entrada_add(mensagem mensagem_nova);
void fila_entrada_remove();
mensagem fila_entrada_get();
int fila_entrada_tem_elementos();
void fila_saida_add(mensagem mensagem_nova);
void fila_saida_remove();
mensagem fila_saida_get();
int fila_saida_tem_elementos();