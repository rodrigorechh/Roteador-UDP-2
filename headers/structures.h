#define BUFLEN 100
#define QTD_MAXIMA_ROTEADORES 10

// packet types
#define DATA 0
#define CONTROL 1

//send type
#define ROUTE 0
#define FOWARD 1

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

void printaNodo();
void printaVec();
void printaTable();
void printaVizinhos();

int idx(int myid);
void mapeia();
void loadLinks();
void loadConfs(int vizinhos[]);
void socketConfig();
void *controlVec();
void sendMyVec();
void sendPacket(pacote packet, int strategy);
void *terminal();
void *router(void *porta);
void verificaEnlaces();
void updateFullTable();
void verificaVolta(pacote packet);