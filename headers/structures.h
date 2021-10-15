#define BUFLEN 100
#define QTD_MAXIMA_ROTEADORES 10
#define DEBUG 1
#define VAZIO -1

// packet types
#define DATA 0
#define CONTROL 1

//send type
#define ROUTE 0
#define FOWARD 1

#define QTD_MENSAGENS_MAX_FILA 100

typedef struct roteador
{
    int id;
    int porta;
    char ip[32];
} roteador;

typedef struct pacote
{
    int id_dest;
    int id_font;
    int seq;
    char message[150];
    int type;
    int ack;
    int sendervec[QTD_MAXIMA_ROTEADORES];
} pacote;

typedef struct fila_mensagens{
    pacote mensagens[QTD_MENSAGENS_MAX_FILA];
} fila_mensagens;

void die(char *s);
int *copiar_vetor(int vetor[], int tamanho);
void inicializa_variaveis_globais();
int obter_index_por_id_roteador(int id);
void printar_roteadores_vizinhos();
void printar_nodos_rede();
void printar_tabela_roteamento();
void printar_vizinhos();
void adicionar_vizinho_array(int id);
void setar_valor_default_tabela_roteamento(int quantidade_indices);
void carregar_links_roteadores();
void carregar_quantidade_nodos();
void carregar_configuracoes_roteadores(int vizinhos[]);
void instanciar_socket();
void enviar_meus_vetores();
void enviar_pacote(pacote packet, int strategy);
void verificar_pacote_retorno(pacote packet);
void verificar_enlaces();
void atualizar_tabela_roteamento();
void *thread_controle_vetores();
void *thread_terminal();
void *thread_roteador();
void fila_entrada_add(pacote pacote_novo);
void fila_entrada_remove();
pacote fila_entrada_get();
int fila_entrada_tem_elementos();
void fila_saida_add(pacote pacote_novo);
void fila_saida_remove();
pacote fila_saida_get();
int fila_saida_tem_elementos();
