# Roteamento de pacotes por uma rede baseado em sockets UDP

#### Autores: Lucas Mahle, Rodrigo Rech e Vagno Serpa

### Passo a passo

**1 - Compilação**
```sh
$ gcc -pthread roteador.c -o roteador
```

**2 - Execução**
```sh
$ ./roteador <id>
```

**3 - Utilização**
Quando aparecer a mensagem `Digite o destino da mensagem:`, informe o **id** do roteador que você deseja fazer comunicação.

Depois que aparecer a mensagem `Digite a mensagem para o destino <id>:` você pode digitar a mensagem que será enviada para o roteador alvo. A mensagem deve ter no máximo 150 caracteres.

### Configurações da rede

Dentro do arquivo `enlaces.config`há a configurações do link entre os roteadores. O padrão do arquivo é `id` `id` `custo` (separados por um espaço apenas). Cada linha é um enlace.

Dentro do arquivo `roteadores.config`há a configurações de funcionamento dos roteadores. O padrão do arquivo é `id` `porta` `ip` (separados por um espaço apenas). Cada linha é um roteador

### Configurações do programa

No arquivo com as DEFINEs há algumas constantes de configurações.
Para desativar o debug, basta definir a constante com valor `0`. 
Obs: é necessário recompilar o código