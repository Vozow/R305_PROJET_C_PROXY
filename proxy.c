#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "./simpleSocketAPI.h"
#include <signal.h>

#define SERVADDR "127.0.0.1" // Définition de l'adresse IP d'écoute
#define SERVPORT "0"         // Définition du port d'écoute, si 0 port choisi dynamiquement
#define LISTENLEN 1          // Taille de la file des demandes de connexion
#define MAXBUFFERLEN 1024    // Taille du tampon pour les échanges de données
#define MAXHOSTLEN 64        // Taille d'un nom de machine
#define MAXPORTLEN 64        // Taille d'un numéro de port
#define FTPPORT "21"         // Port FTP standard

// Fonction lié a la gestion client
void gestionClient(int descSockCOM);
void gestionConnexionClient(int descSockCOM, pid_t forkId);
int gestionConnexionServeurClient(int descSockCOM, int *descSockCOMSERVER, char buffer[], int ecode, pid_t forkId);
int gestionAuthentificationClient(int descSockCOM, int descSockCOMSERVER, char buffer[], int ecode, pid_t forkId);
int gestionCommunicationClientServeur(int descSockCOM, int descSockCOMSERVER, char buffer[], int ecode, pid_t forkId);
int gestionEchangeDonnees(int descSockCOM, int descSockCOMSERVER, char buffer[], int ecode, pid_t forkId);

// Fonction utile
void write_com_client(int descSockCOM, const char *message);
int read_com_client(int descSockCOM, int descSockCOMSERVER, char *buffer, bool connected);
void send_com_server(int descSockCOMSERVER, const char *message);
int recv_com_server(int descSockCOMSERVER, int descSockCOM, char *buffer);

// Fonction de fermeture du proxy 
void fermetureProxy(int signal);
//Variable declaré ici pour pouvoir fermer correctement
int descSockRDV;                // Descripteur de socket de rendez-vous
int descSockCOM;                // Descripteur de socket de communication

int main()
{
    /*
     *
     * Initialisation des variables de main
     *
     */
    int ecode;                      // Code retour des fonctions
    char serverAddr[MAXHOSTLEN];    // Adresse du serveur
    char serverPort[MAXPORTLEN];    // Port du server
    struct addrinfo hints;          // Contrôle la fonction getaddrinfo
    struct addrinfo *res;           // Contient le résultat de la fonction getaddrinfo
    struct sockaddr_storage myinfo; // Informations sur la connexion de RDV
    struct sockaddr_storage from;   // Informations sur le client connecté
    socklen_t len;                  // Variable utilisée pour stocker les longueurs des structures de socket
    signal(SIGINT, fermetureProxy);

    /*
     *
     * Initialisation du proxy
     *
     */
    // Initialisation de la socket de RDV IPv4/TCP
    descSockRDV = socket(AF_INET, SOCK_STREAM, 0);
    if (descSockRDV == -1)
    {
        perror("Erreur création socket RDV\n");
        exit(2);
    }
    // Publication de la socket au niveau du système
    // Assignation d'une adresse IP et un numéro de port
    // Mise à zéro de hints
    memset(&hints, 0, sizeof(hints));
    // Initialisation de hints
    hints.ai_flags = AI_PASSIVE;     // mode serveur, nous allons utiliser la fonction bind
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_family = AF_INET;       // seules les adresses IPv4 seront présentées par
                                     // la fonction getaddrinfo

    // Récupération des informations du serveur
    ecode = getaddrinfo(SERVADDR, SERVPORT, &hints, &res);
    if (ecode)
    {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ecode));
        exit(1);
    }
    // Publication de la socket
    ecode = bind(descSockRDV, res->ai_addr, res->ai_addrlen);
    if (ecode == -1)
    {
        perror("Erreur liaison de la socket de RDV");
        exit(3);
    }
    // Nous n'avons plus besoin de cette liste chainée addrinfo
    freeaddrinfo(res);

    // Récuppération du nom de la machine et du numéro de port pour affichage à l'écran
    len = sizeof(struct sockaddr_storage);
    ecode = getsockname(descSockRDV, (struct sockaddr *)&myinfo, &len);
    if (ecode == -1)
    {
        perror("SERVEUR: getsockname");
        exit(4);
    }
    ecode = getnameinfo((struct sockaddr *)&myinfo, sizeof(myinfo), serverAddr, MAXHOSTLEN,
                        serverPort, MAXPORTLEN, NI_NUMERICHOST | NI_NUMERICSERV);
    if (ecode != 0)
    {
        fprintf(stderr, "error in getnameinfo: %s\n", gai_strerror(ecode));
        exit(4);
    }
    printf("Lancement du Proxy...\n");
    printf("  - L'adresse d'ecoute est: %s\n", serverAddr);
    printf("  - Le port d'ecoute est: %s\n", serverPort);

    // Definition de la taille du tampon contenant les demandes de connexion
    ecode = listen(descSockRDV, LISTENLEN);
    if (ecode == -1)
    {
        perror("Erreur initialisation buffer d'écoute");
        exit(5);
    }

    len = sizeof(struct sockaddr_storage);

    /*
     *
     *  Attente connexion du client
     *
     */
    // Boucle principal du proxy
    while (true)
    {
        descSockCOM = accept(descSockRDV, (struct sockaddr *)&from, &len); // Attente de connection
        printf("\nProcessus Initial - %d | Connexion au proxy, création du fils...\n", getpid());
        // Création d'un processus fils pour gérer le client
        int forkCode = fork(); // Création du fils
        if (forkCode == 0)
        {
            printf("\n%d | Connexion au proxy, création du fils...\n", getpid());
            // Gestion du client dans la fonction
            gestionClient(descSockCOM); // Fonction pour gérer le client
            // Fin de la gestion client et du processus fils
            close(descSockRDV);
            close(descSockCOM);
            exit(0);
        }
        if (forkCode > 0)
        {
            // Le pere n'a pas besoin de la communication Client, il gere les connexions seulement
            close(descSockCOM);
        }
        if (forkCode == -1)
        {
            // Erreur de fork
            perror("Erreur de fork\n");
            exit(6);
        }
    }
}

// Gestion du client
void gestionClient(int descSockCOM)
{
    /*
     *
     * Initialisation des variables de main
     *
     */
    pid_t forkId = getpid();   // PID du fils
    char buffer[MAXBUFFERLEN]; // Buffer
    int ecode;                 // Code de retour des fonctions write/read/send/recv
    int descSockCOMSERVER;     // Descripteur de socket vers le serveur FTP

    /*
     *
     *   Appel des fonctions de gestion des différentes phases du proxy au client
     *
     */
    gestionConnexionClient(descSockCOM, forkId);                                                   // Gestion lors de la connexion au proxy
    ecode = gestionConnexionServeurClient(descSockCOM, &descSockCOMSERVER, buffer, ecode, forkId); // Gestion de la connexion entre le client et le serveur FTP
    if (ecode == 0)                                                                                // Client deconnecté pendant la connexion au serveur
    {
        return; // Return pour ne pas faire le reste du code et fermer les socket et kill le fils
    }
    ecode = gestionAuthentificationClient(descSockCOM, descSockCOMSERVER, buffer, ecode, forkId); // Gestion de l'authentification entre le client et le serveur FTP
    if (ecode == 0)                                                                               // Client ou serveur déconnecté pendant l'authentification
    {
        return; // Return pour ne pas faire le reste du code et fermer les socket et kill le fils
    }
    ecode = gestionCommunicationClientServeur(descSockCOM, descSockCOMSERVER, buffer, ecode, forkId); // Gestion de la communication entre le client et le serveur FTP
}

/*
 *
 *   CONNEXION DU CLIENT AU PROXY
 *
 */
void gestionConnexionClient(int descSockCOM, pid_t forkId)
{
    // Connection au serveur FTP
    printf("%d | Phase de connexion au proxy\n", forkId);
    write_com_client(descSockCOM, "220 - Bienvenue sur le Proxy, veuillez vous authentifier\r\n");
}

/*
 *
 *   AUTHENTIFICATION AU SERVEUR FTP
 *
 */
// Retourne 0 si la connexion est interrompu, sinon > 1
int gestionConnexionServeurClient(int descSockCOM, int *descSockCOMSERVER, char buffer[], int ecode, int forkId)
{
    printf("%d | Phase de connexion au serveur\n", forkId);
    char username[MAXBUFFERLEN] = ""; // Variable pour l'username
    char server[MAXBUFFERLEN] = "";   // Variable pour l'adresse du serveur
    bool connected = false;           // Boolean si l'utilisateur n'est pas connecté (connecté & username valide)
    while (connected == false)        // Boucle tant que l'utilisateur n'est pas connecté
    {
        while (username[0] == '\0' || server[0] == '\0') // Boucle tant que l'utilisateur et le serveur sont vide
        {
            // Lecture de la réponse du client
            ecode = read_com_client(descSockCOM, *descSockCOMSERVER, buffer, false);
            if (ecode == 0)
            {
                return 0;
            }
            if (ecode > 0)
            {
                /*
                 * Gestion des commandes FTP lors de la connexion
                 */
                // AUTH :
                if (strncmp(buffer, "AUTH", 4) == 0)
                {
                    write_com_client(descSockCOM, "500 SSL et TLS non disponible.\r\n");
                }
                // USER :
                else if (strncmp(buffer, "USER", 4) == 0)
                {
                    sscanf(buffer + 5, "%49[^@]@%s", username, server); // Expression reguliere qui scan le buffer après 5 caractere (pour passer "USER ")
                    if (username[0] == '\0' || server[0] == '\0')       // Si username ou server vide
                    {
                        printf("%d |      ➥  Format incorrect (530)\n", forkId);
                        write_com_client(descSockCOM, "530 Format incorrect. Utilisez user@host.\r\n");
                        username[0] = '\0';
                        server[0] = '\0';
                    }
                    else // logs
                    {
                        printf("%d |      ➥  Format Correct, information :\n", forkId);
                        printf("%d |        ➥  Username : %s\n", forkId, username);
                        printf("%d |        ➥  Server : %s\n", forkId, server);
                    }
                }
                // QUIT :
                else if (strncmp(buffer, "QUIT", 4) == 0)
                {
                    write_com_client(descSockCOM, "221 Au revoir.\r\n");
                    printf("%d |        ➥  Deconnexion du proxy ⭕ (221)\n", forkId);
                    return 0;
                }
                // SYST :
                else if (strncmp(buffer, "SYST", 4) == 0)
                {
                    write_com_client(descSockCOM, "530 Utilisez \"user\" pour vous authentifier (format user@host).\r\n");
                }
                // AUTRES COMMANDES :
                else
                {
                    write_com_client(descSockCOM, "502 Commande non disponible.\r\n");
                }
            }
        }
        /*
         * Connexion au serveur FTP
         */
        printf("%d |  - Tentative de connexion au serveur FTP :\n", forkId);
        connect2Server(server, FTPPORT, descSockCOMSERVER); // Connection venant de la librairie simpleSocketAPI
        // Lecture de la réponse du serveur FTP
        ecode = recv_com_server(*descSockCOMSERVER, descSockCOM, buffer);
        if (ecode == -1) // Si la connexion au serveur est invalide
        {
            write_com_client(descSockCOM, "530 Erreur de connexion au serveur FTP. Utilisez \"user\" pour vous authentifier.\r\n");
            printf("%d |      ➥  Connexion au serveur FTP non établi (adresse incorrecte ou serveur indisponible) ⚠️\n", forkId);
            username[0] = '\0';
            server[0] = '\0';
        }
        if (ecode > 0) // Si on est bien connecté au serveur
        {
            if (strncmp(buffer, "220", 3) == 0)
            {
                // Informer l'utilisateur de la connexion réussie
                printf("%d |  - Connexion Validé... \n", forkId);
                // Login côté serveur FTP
                strcpy(buffer, "USER ");
                strcat(buffer, username);
                strcat(buffer, "\r\n");
                send_com_server(*descSockCOMSERVER, buffer);
                // Lecture de la réponse du serveur FTP
                ecode = recv_com_server(*descSockCOMSERVER, descSockCOM, buffer);
                // Cas de code erreur
                if (ecode == 0)
                {
                    return 0;
                }
                if (ecode > 0)
                {
                    // Username correct
                    if (strncmp(buffer, "331", 3) == 0)
                    {
                        printf("%d |      ➥  Identifiant valide\n", forkId);
                        connected = true;
                    }
                    // Username incorrect
                    else
                    {
                        write_com_client(descSockCOM, "530 Connecté au serveur, Identifiant invalide !\r\n");
                        printf("%d |      ➥  Identifiant invalide ⚠️\n", forkId);
                        username[0] = '\0'; // On vide le server
                        server[0] = '\0';   // On vide l'username
                                            // pour rerentrer dans le boucle précédante
                    }
                }
            }
            else
            {
                printf("%d | Erreur de réponse du serveur FTP ⚠️\n", forkId);
            }
        }
    }
    write_com_client(descSockCOM, "331 Connecté au serveur, Identifiant valide, veuillez saisir votre mot de passe.\r\n");
    return 1;
}

/*
 *
 *   AUTHENTIFICATION DU CLIENT AU SERVEUR
 *
 */
// Retourne 0 si la connexion est interrompu, sinon > 1
int gestionAuthentificationClient(int descSockCOM, int descSockCOMSERVER, char buffer[], int ecode, int forkId)
{
    printf("\n%d | Phase d'authentification au proxy\n", forkId);
    printf("%d |  - Demande de mot de passe (331)\n", forkId);
    bool authentified = false;        // Si on est authentifié (mot de passe valide)
    char password[MAXBUFFERLEN] = ""; // Variable mot de passe pour les logs
    while (authentified == false)
    {
        // Lecture de la réponse du client
        ecode = read_com_client(descSockCOM, descSockCOMSERVER, buffer, true);
        if (ecode == 0)
        {
            return 0;
        }
        if (ecode > 0)
        {
            // PASS :
            if (strncmp(buffer, "PASS", 4) == 0)
            {
                // Recuperation du mot de passe
                strcpy(password, buffer + 5);               // Copy le buffer a partir du 5eme caractere
                password[strcspn(password, "\r\n")] = '\0'; // Enleve les caracteres spéciaux \r\n
                printf("%d |      ➥  Mot de passe : %s\n", forkId, password);
                // Envoi du mot de passe au serveur FTP
                send(descSockCOMSERVER, buffer, strlen(buffer), 0);
                // Lecture de la réponse du serveur FTP
                ecode = recv_com_server(descSockCOMSERVER, descSockCOM, buffer);
                if (ecode == 0)
                {
                    return 0;
                }
                if (ecode > 0)
                {
                    if (strncmp(buffer, "230", 3) == 0) // Connexion réussie
                    {
                        // Informer le client de l'authentification réussie
                        write_com_client(descSockCOM, "230 Connecté au serveur, Authentification réussie valide !\r\n");
                        printf("%d |      ➥  Authentification réussie (230)\n", forkId);
                        printf("%d | Connexion établie (Client <---> Proxy <---> Serveur FTP) ✅\n\n", forkId);
                        authentified = true;
                    }
                    else // Connexion non réussis -> mot de passe invalide
                    {
                        // Nouvelle demande de mot de passe
                        write_com_client(descSockCOM, "530 Authentification échouée. Veuillez réessayer avec \"PASS\".\r\n");
                        printf("%d |      ➥  Authentification échouée (530)\n", forkId);
                    }
                }
            }
        }
    }
    return 1;
}

/*
 *
 *   COMMUNICATION DU CLIENT ET DU SERVEUR
 *
 */
// Retourne 0 si la connexion est interrompu, sinon > 1
int gestionCommunicationClientServeur(int descSockCOM, int descSockCOMSERVER, char buffer[], int ecode, int forkId)
{
    printf("\n%d | Phase de communication client-serveur\n", forkId);
    // Boucle de communication
    while (true)
    {
        ecode = read_com_client(descSockCOM, descSockCOMSERVER, buffer, true);
        if (ecode == 0)
        {
            return 0;
        }
        if (ecode > 0)
        {
            // COMMANDE EN MODE ACTIF -> Echange de données
            if (strncmp(buffer, "PORT", 4) == 0)
            {
                // Gestion d'echange de donnée
                ecode = gestionEchangeDonnees(descSockCOM, descSockCOMSERVER, buffer, ecode, forkId);
                if (ecode == 0)
                {
                    return 0;
                }
            }
            // COMMANDE EN MODE PASSIF -> Non géré
            else if (strncmp(buffer, "PASV", 4) == 0)
            {
                write_com_client(descSockCOM, "500 Mode passif non supporté.\r\n");
                printf("%d |      ➥  Mode passif non supporté (500)\n", forkId);
            }
            // AUTRES COMMANDES
            else
            {
                send_com_server(descSockCOMSERVER, buffer);
                printf("%d |      ➥  Envoi de la réponse - Client --> Serveur\n", forkId);
                ecode = recv_com_server(descSockCOMSERVER, descSockCOM, buffer);
                if (ecode == 0)
                {
                    return 0;
                }
                if (ecode > 0)
                {
                    write_com_client(descSockCOM, buffer);
                    printf("%d |      ➥  Envoi de la réponse - Serveur --> Client\n", forkId);
                    if (strncmp(buffer, "221", 3) == 0)
                    {
                        printf("%d |        ➥  Deconnexion du serveur et du proxy ⭕ (221)\n", forkId);
                        close(descSockCOMSERVER);
                        return 0;
                    }
                }
            }
        }
    }
    return 1;
}

/*
 *
 *   ECHANGE DE DONNEES ENTRE LE CLIENT ET LE SERVEUR
 *
 */
int gestionEchangeDonnees(int descSockCOM, int descSockCOMSERVER, char buffer[], int ecode, int forkId)
{
    // Déclaration des variables
    int descSockDATACLIENT;
    int descSockDATASERVER;
    int clientAnswer[6];
    char clientAdress[MAXBUFFERLEN] = "";
    char clientPort[6] = "";
    char serverAdress[MAXBUFFERLEN] = "";
    char serverPort[6] = "";
    // Obtention de l'adresse et port du client
    printf("\n%d | Echange de Données\n", forkId);
    buffer[strcspn(buffer, "\r\n")] = '\0';                                                                                                              // Remplace les \r\n par une fin de chaine
    sscanf(buffer + 5, "%d,%d,%d,%d,%d,%d", &clientAnswer[0], &clientAnswer[1], &clientAnswer[2], &clientAnswer[3], &clientAnswer[4], &clientAnswer[5]); // Extrait les nombre du servuer
    sprintf(clientAdress, "%d.%d.%d.%d", clientAnswer[0], clientAnswer[1], clientAnswer[2], clientAnswer[3]);                                            // Print dans un char[] l'adresse du serveur
    sprintf(clientPort, "%d", clientAnswer[4] * 256 + clientAnswer[5]);                                                                                  // Print dans un char[] le port du serveur (avec la formule du port)
    printf("%d |        ➥  Adresse : %s\n", forkId, clientAdress);
    printf("%d |        ➥  Port : %s\n", forkId, clientPort);
    // Envoi de la commande PASV au serveur FTP
    send_com_server(descSockCOMSERVER, "PASV\r\n");
    printf("%d |      ➥  Envoi de la commande PASV au serveur FTP\n", forkId);
    // Lecture de la réponse du serveur FTP
    ecode = recv_com_server(descSockCOMSERVER, descSockCOM, buffer); // Recupere la réponse du serveur
    if (ecode == 0)
    {
        return 0;
    }
    if (ecode > 0)
    {
        // Gestion d'une mauvaise reponse du serveur FTP
        if (strncmp(buffer, "227", 3) != 0)
        {
            write_com_client(descSockCOM, "500 Erreur, passage en mode passif (Proxy --> Serveur) interrompu\r\n");
            printf("%d |      ➥  Erreur passage en mode passif (Proxy --> Serveur) interrompu (500)\n", forkId);
        }
        else // Dans le cas normal
        {
            char *pointer = strchr(buffer, '('); // Pointeur qui pointe sur la premiere parenthése (format FTP)
            if (pointer == NULL)
            {
                write_com_client(descSockCOM, "500 Erreur, format du serveur non valide\r\n");
                printf("%d | ERREUR: Format 227 invalide (pas de parenthèse)\n", forkId);
            }
            else
            {
                // Extraction de l'adresse et du port du serveur FTP
                int serverAnswer[6];
                buffer[strcspn(buffer, "\r\n")] = '\0';                                                                                                            // Enleve les caracteres spéciaux \r\n
                sscanf(pointer, "(%d,%d,%d,%d,%d,%d).", &serverAnswer[0], &serverAnswer[1], &serverAnswer[2], &serverAnswer[3], &serverAnswer[4], &serverAnswer[5]); // Comme pour le client
                sprintf(serverAdress, "%d.%d.%d.%d", serverAnswer[0], serverAnswer[1], serverAnswer[2], serverAnswer[3]);
                sprintf(serverPort, "%d", serverAnswer[4] * 256 + serverAnswer[5]);
                printf("%d |        ➥  Adresse : %s\n", forkId, serverAdress);
                printf("%d |        ➥  Port : %s\n", forkId, serverPort);
            }
        }
    }
    if (clientAdress[0] != '\0' && clientPort != 0 && serverAdress[0] != '\0' && serverPort != 0) // Verification si les champs sont bon
    {
        // Connexion au serveur de données (Serveur)
        connect2Server(serverAdress, serverPort, &descSockDATASERVER);
        printf("%d |      ➥  Connexion au serveur (Serveur) établie\n", forkId);
        // Connexion au serveur de données (Client)
        write_com_client(descSockCOM, "200 Connexion au mode actif établie.\r\n");
        connect2Server(clientAdress, clientPort, &descSockDATACLIENT);
        printf("%d |      ➥  Connexion au serveur (Client) établie\n", forkId);
        // Attente d'une requete cliente
        ecode = read_com_client(descSockCOM, descSockCOMSERVER, buffer, true);
        if (ecode == 0)
        {
            close(descSockDATACLIENT); // En cas d'erreur si le serveur est fermer
            close(descSockDATASERVER); // En cas d'erreur si le serveur est fermer
            return 0;
        }
        if (ecode > 0)
        {
            printf("%d |      ➥  Envoi de la requête - Client --> Serveur\n", forkId);
            send_com_server(descSockCOMSERVER, buffer);
            // Boucle de transfert
            bool transfer = false;
            while (transfer == false)
            {
                ecode = recv_com_server(descSockCOMSERVER, descSockCOM, buffer);
                if (ecode == 0)
                {
                    close(descSockDATACLIENT); // En cas d'erreur si le serveur est fermer
                    close(descSockDATASERVER); // En cas d'erreur si le serveur est fermer
                    return 0;
                }
                if (ecode > 0)
                {
                    write_com_client(descSockCOM, buffer);
                    printf("%d |      ➥  Envoi de la réponse - Serveur --> Client\n", forkId);
                    if (strncmp(buffer, "226", 3) == 0) // 226 -> Transfert terminé
                    {
                        transfer = true;
                        printf("%d | Transfer terminé.", forkId);
                    }
                    else if (strncmp(buffer, "150", 3) == 0) // 150 -> Début de transfer
                    {
                        printf("%d |      ➥  Envoi des données - Serveur --> Client\n", forkId);
                        int lenBuffer;
                        while ((lenBuffer = recv(descSockDATASERVER, buffer, MAXBUFFERLEN, 0)) > 0) // Si le buffer n'est pas vide (recv renvoie la len du buffer)
                        {
                            send(descSockDATACLIENT, buffer, lenBuffer, 0); // Envoie les données
                        }
                        // Fin du transfert
                        close(descSockDATACLIENT);
                        close(descSockDATASERVER);
                        printf("%d |      ➥  Fermeture des connexions de données\n", forkId);
                    }
                    else
                    {
                        printf("%d | Erreur de réponse serveur\n", forkId);
                    }
                }
            }
        }
    }
    else
    {
        printf("%d | Erreur de format de Serveur ou Port", forkId);
    }
    return 1;
}

// Ecriture d'un message au client
void write_com_client(int descSockCOM, const char *message)
{
    write(descSockCOM, message, strlen(message));
    printf("%d |  ➥  Envoi au Client : %s", getpid(), message);
}

// Lecture d'une reponse du client et renvoie l'ecode
int read_com_client(int descSockCOM, int descSockCOMSERVER, char *buffer, bool connected)
{
    memset(buffer, 0, MAXBUFFERLEN);
    int ecode = read(descSockCOM, buffer, MAXBUFFERLEN);
    if (ecode == -1)
    {
        perror("  ⚠️  Erreur Programme ⚠️ - Erreur lecture (Communication Client)");
    }
    if (ecode == 0)
    {
        printf("Connexion au Client fermé ⭕\n");
        if (connected == true)
        {
            send_com_server(descSockCOMSERVER, "QUIT\r\n");
            close(descSockCOMSERVER);
        }
    }
    if (ecode > 0)
    {
        printf("%d |  - Lecture du Client :\n", getpid());
        buffer[strcspn(buffer, "\r\n")] = '\0';
        printf("%d |    ➥  %s\n", getpid(), buffer);
        strcat(buffer, "\r\n");
    }
    return ecode;
}

// Envoi d'un message au serveur
void send_com_server(int descSockCOMSERVER, const char *message)
{
    send(descSockCOMSERVER, message, strlen(message), 0);
    printf("%d |  ➥  Envoi au Serveur : %s", getpid(), message);
}

// Reception d'une reponse du serveur et renvoie l'ecode
int recv_com_server(int descSockCOMSERVER, int descSockCOM, char *buffer)
{
    memset(buffer, 0, MAXBUFFERLEN);
    int ecode = recv(descSockCOMSERVER, buffer, MAXBUFFERLEN, 0);
    if (ecode == -1)
    {
        perror("  ⚠️  Erreur Programme ⚠️  - Erreur lecture (Communication Serveur)");
    }
    if (ecode == 0)
    {
        printf("Connexion au Serveur fermé ⭕\n");
        write_com_client(descSockCOM, "500 Erreur de connexion au serveur\r\n");
        close(descSockCOM);
        close(descSockCOMSERVER);
    }
    if (ecode > 0)
    {
        printf("%d |  - Réponse du Serveur :\n", getpid());
        buffer[strcspn(buffer, "\r\n")] = '\0';
        printf("%d |    ➥  %s\n", getpid(), buffer);
        strcat(buffer, "\r\n");
    }
    return ecode;
}


// Fonction appeler lorsque le signal detecte un ctrl+c
void fermetureProxy(int signal)
{
    close(descSockRDV);
    kill(0, SIGTERM);
    exit(0);
}