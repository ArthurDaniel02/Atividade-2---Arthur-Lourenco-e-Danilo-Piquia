#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

// Definições de tamanho para facilitar mudanças futuras
#define MAPA_SIZE 30
#define NUM_SENSORS 100
#define TAMANHO_FILA 50

// Recursos Compartilhados (Perigo de Condição de Corrida (por isso utilizamos os mutexes)

char mapa[MAPA_SIZE][MAPA_SIZE]; // O mapa(que é acessado por todo mundo (fogo, sensor, bombeiro, main))
pthread_mutex_t mapa_mutex;      // Cadeado para proteger o mapa
pthread_mutex_t registro_mutex;  // Cadeado para proteger o log e o histórico

// Variável global para exibir na tela o último log 
char ultimo_log_display[100] = "Aguardando deteccoes...";

// Função para deixar o mapa colorido 
void print_verde(int red, int green, int blue){
    printf("\033[48;2;%d;%d;%dm",red,green,blue);
}

// Struct para passar dados para a thread do sensor na criação
typedef struct coords{
    int x, y;   // Posições X e Y no mapa 
    int id;     // ID do sensor (0-99)
}coords;

coords dados_sensores[NUM_SENSORS];
pthread_t threads_sensores[NUM_SENSORS];
int total_sensores = 0;

// Struct da msg (viaja do Sensor -> Central)
typedef struct {
    int idsensor;
    int fogo_x, fogo_y;
    char timestamp[10]; // "HH:MM:SS"
} MensagemAlerta;

// Struct do fogo (viaja da Central -> Bombeiro)
typedef struct {
    int x, y;
} CoordsFogo;

// Histórico para evitar que o bombeiro tente apagar o mesmo fogo 2 vezes
CoordsFogo historico[500];
int cont_his = 0;  

// --- Fila de Comunicação: Sensores (Produtor) -> Central (Consumidor) ---
MensagemAlerta fila_central[TAMANHO_FILA];
int msg_entrada = 0; // Onde entra mensagem nova
int msg_saida = 0;   // Onde sai mensagem velha
int msg_cont = 0;    // Quantas mensagens tem agora

pthread_mutex_t msg_mutex; // Protege essa fila
pthread_cond_t msg_cond;   // Avisa a central que "tem mensagem nova"

// --- Fila de Comunicação: Central (Produtor) -> Bombeiro (Consumidor) ---
CoordsFogo fila_bombeiro[TAMANHO_FILA];
int bomb_entrada = 0;
int bomb_saida = 0;
int bomb_cont = 0;

pthread_mutex_t bombeiro_mutex; // Protege essa fila
pthread_cond_t bomb_cond;       // Avisa o bombeiro que "tem trabalho a fazer"

// Verifica se a coordenada está na borda do mapa 
bool e_borda(int x, int y) {
    return (x == 0 || x == MAPA_SIZE - 1 || y == 0 || y == MAPA_SIZE - 1);
}

// Função recursiva de roteamento.
// Se não está na borda, passa para o vizinho mais perto dela.
void propagar_alerta(int id_origem, int fogo_x, int fogo_y, int atual_x, int atual_y) {
    
    // Caso Base: Se chegou na borda, envio para a central
    if (e_borda(atual_x, atual_y)) {
        MensagemAlerta msg;
        msg.idsensor = id_origem; // Mantém o ID de quem viu o fogo original
        msg.fogo_x = fogo_x;
        msg.fogo_y = fogo_y;
        
        // Pega a hora atual do sistema
        time_t now = time(NULL);
        struct tm *tm_struct = localtime(&now);
        strftime(msg.timestamp, 10, "%H:%M:%S", tm_struct);

        //trava este mutex para escrever na fila
        pthread_mutex_lock(&msg_mutex);
        if(msg_cont < TAMANHO_FILA){ // Se a fila não estiver cheia
            fila_central[msg_entrada] = msg;
            msg_entrada = (msg_entrada + 1) % TAMANHO_FILA; // Lógica circular
            msg_cont++;
            //acorda a thread central que estava dormindo esperando msg
            pthread_cond_signal(&msg_cond);
        }
        pthread_mutex_unlock(&msg_mutex);
    } 
    else {
        // Caso Recursivo: Não ta na borda, calculo quem é o vizinho mais perto da borda
        int prox_x = atual_x;
        int prox_y = atual_y;

        // Lógica de roteamento: calcular distâncias menor para a parede mais próxima
        int dist_topo = atual_x;
        int dist_baixo = (MAPA_SIZE - 1) - atual_x;
        int dist_esq = atual_y;
        int dist_dir = (MAPA_SIZE - 1) - atual_y;

        int min_dist = dist_topo;
        if (dist_baixo < min_dist) min_dist = dist_baixo;
        if (dist_esq < min_dist) min_dist = dist_esq;
        if (dist_dir < min_dist) min_dist = dist_dir;

        if (min_dist == dist_topo) prox_x--;
        else if (min_dist == dist_baixo) prox_x++;
        else if (min_dist == dist_esq) prox_y--;
        else if (min_dist == dist_dir) prox_y++;

        // recursividade
        propagar_alerta(id_origem, fogo_x, fogo_y, prox_x, prox_y);
    }
}

// Inicializa o mapa com os 100 sensores ('T') 1 a cada 3 casas e floresta vazia ('-')
void inicializar_mapa() {
    for (int i = 0; i < MAPA_SIZE; i++) {
        for (int j = 0; j < MAPA_SIZE; j++) {
            if((i-1)%3==0){
                if((j-1)%3==0){
                mapa[i][j] = 'T';
                }else{
                mapa[i][j] = '-';
            }
            } else{
                mapa[i][j] = '-';
            }
        }
    }
}

// Imprime o mapa
void imprimir_mapa() {
    int focos_ativos = 0; // Contador local para os status no terminal

    // Trava o mapa para ler
    pthread_mutex_lock(&mapa_mutex);
    system("clear"); // Limpa o terminal 
    
    // Printa coordenadas horizontais 
    printf("   ");
    for (int j = 0; j < MAPA_SIZE; j++) {
        printf("%2d ", j);
    }
    printf("\n");

    for (int i = 0; i < MAPA_SIZE; i++) {
        // Printa coordenada vertical 
        printf("%2d ", i);
        for (int j = 0; j < MAPA_SIZE; j++) {
            
            // Contagem de fogo para o status
            if(mapa[i][j] == '@') {
                focos_ativos++;
            }

            print_verde(34,139,34);
            printf(" %c ", mapa[i][j]);
            printf("\033[0m");
        }
        printf("\n");
    }
    pthread_mutex_unlock(&mapa_mutex);

    // --- Mensagem de Status ---
    printf("\n================ STATUS DO SISTEMA ================\n");
    printf(" Sensores Funcionando: %d\n", total_sensores);
    printf(" Focos de Incendio Ativos: %d\n", focos_ativos);
    
    // Trava mutex de registro para ler a string global sem conflito
    pthread_mutex_lock(&registro_mutex);
    printf(" Ultimo Log: %s\n", ultimo_log_display);
    pthread_mutex_unlock(&registro_mutex);
    printf("===================================================\n");
}

// Thread que representa cada sensor (temos 100)
void* sensor(void *data){
    coords* t  = (coords*) data;
    pthread_detach(pthread_self()); // Libera recursos automaticamente ao morrer

    // Matriz local para saber se já avisei sobre um fogo vizinho (evita spam)
    bool alerta_enviado[MAPA_SIZE][MAPA_SIZE];
    memset(alerta_enviado, 0, sizeof(alerta_enviado));

    while(1){
    sleep(1); 

        //  Verificar se o sensor ta pegando fogo
        pthread_mutex_lock(&mapa_mutex);
        if (mapa[t->x][t->y] == '@') {
            pthread_mutex_unlock(&mapa_mutex); // Solta o mutex
            total_sensores--; // para o status
            pthread_exit(NULL); // Mata a thread (Sensor queimado)
        }
        pthread_mutex_unlock(&mapa_mutex);

        //  Monitorar os 8 quadrados vizinhos
        for (int i = t->x - 1; i <= t->x + 1; i++) {
            for (int j = t->y - 1; j <= t->y + 1; j++) {

                // Ignorar a si mesmo e fora dos limites do mapa
                if ((i == t->x && j == t->y) || i < 0 || i >= MAPA_SIZE || j < 0 || j >= MAPA_SIZE) {
                    continue; 
                }

                // Verifica se tem fogo no vizinho (trava o mapa)
                pthread_mutex_lock(&mapa_mutex);
                bool tem_fogo = (mapa[i][j] == '@');
                pthread_mutex_unlock(&mapa_mutex);

                // Se achar fogo e ainda não foi avisado
                if (tem_fogo && !alerta_enviado[i][j]) {
                    alerta_enviado[i][j] = true; // Marco que já avisei sobre o fogo
                    // Inicia o processo de roteamento até a borda
                    propagar_alerta(t->id, i, j, t->x, t->y);
                }
                // se apagou, reseta para poder avisar de novo no futuro
                else if (!tem_fogo && alerta_enviado[i][j]) {
                    alerta_enviado[i][j] = false;
                }
            }
        }
    }
    return NULL;
}

// Thread Central que recebe alertas dos sensores e despacha bombeiros
void* central(void *data){
    FILE* incendiolog;

    while(1){
        // Trava a fila 
        pthread_mutex_lock(&msg_mutex);
        while(msg_cont == 0){
            // Se fila vazia, espera e libera o mutex. Só acorda com sinal.
            pthread_cond_wait(&msg_cond, &msg_mutex);  
        }

        // Consome a mensagem
        MensagemAlerta msg = fila_central[msg_saida]; 
        msg_saida = (msg_saida + 1) % TAMANHO_FILA;
        msg_cont--; 
        pthread_mutex_unlock(&msg_mutex);


        // Verifica duplicidade no histórico global (pra não mandar bombeiro 2x pro mesmo lugar)
        bool dup = false; 
        pthread_mutex_lock(&registro_mutex);
        for(int i = 0;i<cont_his;i++){
            if(historico[i].x == msg.fogo_x && historico[i].y == msg.fogo_y){
                dup = true;
                break;
            }
        }

        if(!dup){ // Se é um fogo novo
            if(cont_his<500){
                // Adiciona no histórico 
                historico[cont_his].x = msg.fogo_x;
                historico[cont_his].y = msg.fogo_y;
                cont_his++;
            }
        
            // Escreve no arquivo de Log
            incendiolog = fopen("incendios.log", "a"); 
            if (incendiolog) {
                fprintf(incendiolog, "[%s] Alerta do Sensor %d: Fogo em (%d, %d)\n",
                msg.timestamp, msg.idsensor, msg.fogo_x, msg.fogo_y);
                fclose(incendiolog);

                // Atualiza a variável global para o usuário ver na tela
                sprintf(ultimo_log_display, "[%s] Sensor %d -> Fogo em (%d, %d)", 
                        msg.timestamp, msg.idsensor, msg.fogo_x, msg.fogo_y);
            }
            pthread_mutex_unlock(&registro_mutex);
            
            // Envia ordem para a fila do Bombeiro
            pthread_mutex_lock(&bombeiro_mutex);
            if(bomb_cont < TAMANHO_FILA){
                CoordsFogo fogo_pos = {msg.fogo_x,msg.fogo_y};

                fila_bombeiro[bomb_entrada] = fogo_pos;
                bomb_entrada = (bomb_entrada+1)%TAMANHO_FILA;
                bomb_cont++;
                // Acorda a thread bombeiro
                pthread_cond_signal(&bomb_cond);
            }
            pthread_mutex_unlock(&bombeiro_mutex);
        }else{
            // Se era duplicado, só destrava e ignora
            pthread_mutex_unlock(&registro_mutex);
        }
    } return NULL;
}

// Thread Bombeiro que espera ordens da Central e apaga o fogo
void* bombeiro(void *data){
    while (1)
    {
        // Espera chegar ordem na fila
        pthread_mutex_lock(&bombeiro_mutex);
        while(bomb_cont == 0){
            pthread_cond_wait(&bomb_cond, &bombeiro_mutex); // Dorme se não tem trabalho da mesma forma que a fila da central
        }    
        CoordsFogo fogo_pos = fila_bombeiro[bomb_saida];
        bomb_saida = (bomb_saida + 1) % TAMANHO_FILA;
        bomb_cont--;
        pthread_mutex_unlock(&bombeiro_mutex);  

        sleep(2); // Simula tempo de deslocamento até o fogo

        // Apaga o fogo no mapa 
        pthread_mutex_lock(&mapa_mutex);
        if(mapa[fogo_pos.x][fogo_pos.y] == '@'){
            mapa[fogo_pos.x][fogo_pos.y] = '-'; 
        }
        pthread_mutex_unlock(&mapa_mutex);

        // Remove do histórico para que se pegar fogo de novo, possa ser atendido
        pthread_mutex_lock(&registro_mutex); 
            for(int i = 0; i < cont_his; i++){
                if(historico[i].x == fogo_pos.x && historico[i].y == fogo_pos.y){
                    // Remove do vetor trocando pelo último (já que a ordem não importa)
                    historico[i] = historico[cont_his - 1];
                    cont_his--;
                    break; 
                }
            }
        pthread_mutex_unlock(&registro_mutex);
    }
    
    return NULL;
}

// Thread Geradora de Incêndios 
void* fogo(void *arg){
    while (1){
      sleep(5); // Gera fogo a cada 5 segundos
      int x = rand()%MAPA_SIZE;
      int y = rand()%MAPA_SIZE;
      
      pthread_mutex_lock(&mapa_mutex);
      // Inicia fogo só se for floresta ('-') ou em cima de um sensor ('T')
      if (mapa[x][y] == '-' || mapa[x][y] == 'T') {
        mapa[x][y] = '@';
      }
      pthread_mutex_unlock(&mapa_mutex);
    }
    return NULL;
}

int main(int argc, char* argv[]){
    srand(time(NULL)); // Semente para números aleatórios

    //--- Inicialização dos Mutexes e Cond ---
    pthread_mutex_init(&mapa_mutex, NULL);
    pthread_mutex_init(&msg_mutex, NULL);
    pthread_mutex_init(&bombeiro_mutex, NULL);
    pthread_mutex_init(&registro_mutex,NULL);
    pthread_cond_init(&msg_cond,NULL);
    pthread_cond_init(&bomb_cond,NULL);

    // Cria/Limpa o arquivo de log no início
    FILE* incendiolog = fopen("incendios.log","w");
    if(incendiolog){
        fprintf(incendiolog,"--Histórico de Incêndios--\n");
        fclose(incendiolog);
    }
    
    inicializar_mapa();
    total_sensores = 0;

    // Criação das Threads dos Sensores
    for(int i = 0;i<MAPA_SIZE;i++){
        for(int j = 0; j<MAPA_SIZE;j++){
            if(mapa[i][j] == 'T'){
                // Configura ID e Posição do sensor
                dados_sensores[total_sensores].x = i;
                dados_sensores[total_sensores].y = j;
                dados_sensores[total_sensores].id = total_sensores;
                
                // Cria a thread
                pthread_create(&threads_sensores[total_sensores],NULL,sensor,(void*)&dados_sensores[total_sensores]);
                total_sensores++;
            }
        }
    }

    // Cria as threads de Fogo, Central e Bombeiro
    pthread_t fogo_tid,msg_tid,bombeiro_tid;
    pthread_create(&fogo_tid,NULL,fogo,NULL);
    pthread_create(&msg_tid,NULL,central,NULL);
    pthread_create(&bombeiro_tid,NULL,bombeiro,NULL);
    
    // Loop principal (atualiza a cada 1 seg)
    while(1){
        imprimir_mapa();
        sleep(1);
    }
    
    // (Código nunca chega aqui por causa do while(1), mas é boa prática)
    pthread_join(fogo_tid,NULL);
    pthread_join(msg_tid,NULL);
    pthread_join(bombeiro_tid,NULL);

    pthread_mutex_destroy(&mapa_mutex);
    pthread_mutex_destroy(&msg_mutex);
    pthread_mutex_destroy(&bombeiro_mutex);
    pthread_mutex_destroy(&registro_mutex);
    
    pthread_cond_destroy(&msg_cond);
    pthread_cond_destroy(&bomb_cond);    

    return 0;
}