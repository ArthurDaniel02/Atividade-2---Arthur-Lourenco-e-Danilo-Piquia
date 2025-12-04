### Projeto: Simulador de Sistema Detector de Incêndios Florestais
Disciplina: Sistemas Operacionais - 2025/2
Instituto Federal de Brasília - Campus Taguatinga

## Desenvolvedores
- Arthur Daniel Ribeiro Pereira Dantas Lourenço
- Danilo Moraes Borges Piquiá

## Descrição
Este software simula uma rede de sensores (threads) em uma malha florestal de 30x30. O sistema gerencia a detecção de focos de incêndio, a comunicação entre sensores via propagação de mensagens até a borda (roteamento), o registro em log pela central e a atuação de uma thread bombeiro.

## Pré-requisitos
É necessário um ambiente Linux com o compilador GCC e as bibliotecas de construção essenciais. Caso não possua, instale via terminal:
$ sudo apt update
$ sudo apt install build-essential

## Como Compilar
Abra o terminal no diretório onde está o arquivo fonte (ex: main.c) e execute o comando abaixo.
Nota: A flag -pthread é obrigatória para o funcionamento das threads.

gcc main.c -o simulador -pthread

## Como Executar
Após a compilação, execute o programa com o comando:

./simulador

## Funcionamento
1. Visualização: O terminal exibirá a matriz 30x30 atualizada a cada segundo.
   - 'T': Sensor ativo
   - '-': Área de floresta vazia
   - '@': Foco de incêndio
2. Logs: Durante a execução, o arquivo "incendios.log" será criado/atualizado automaticamente no mesmo diretório, registrando o Timestamp, ID do sensor e Coordenadas do fogo.
3. Extinção: A thread bombeiro apaga os focos de incêndio 2 segundos após receber o alerta da central.
4. Destruição: Se um foco de incêndio iniciar exatamente sobre um sensor, a thread daquele sensor é encerrada (simulando destruição), mas o fogo será detectado pelos vizinhos e apagado normalmente.