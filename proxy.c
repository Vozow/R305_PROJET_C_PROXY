#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include "./simpleSocketAPI.h"

#define SERVADDR "127.0.0.1" // Définition de l'adresse IP d'écoute
#define SERVPORT "0"         // Définition du port d'écoute, si 0 port choisi dynamiquement
#define LISTENLEN 1          // Taille de la file des demandes de connexion
#define MAXBUFFERLEN 1024    // Taille du tampon pour les échanges de données
#define MAXHOSTLEN 64        // Taille d'un nom de machine
#define MAXPORTLEN 64        // Taille d'un numéro de port
#define FTPPORT "21"         // Port FTP standard

int main()
{
    int ecode;                      // Code retour des fonctions
    char serverAddr[MAXHOSTLEN];    // Adresse du serveur
    char serverPort[MAXPORTLEN];    // Port du server
    int descSockRDV;                // Descripteur de socket de rendez-vous
    int descSockCOM;                // Descripteur de socket de communication
    int descSockSERVER;             // Descripteur de socket vers le serveur FTP
    struct addrinfo hints;          // Contrôle la fonction getaddrinfo
    struct addrinfo *res;           // Contient le résultat de la fonction getaddrinfo
    struct sockaddr_storage myinfo; // Informations sur la connexion de RDV
    struct sockaddr_storage from;   // Informations sur le client connecté
    socklen_t len;                  // Variable utilisée pour stocker les
                                    // longueurs des structures de socket
    char buffer[MAXBUFFERLEN];      // Tampon de communication entre le client et le serveur

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
    printf("  - Le port d'ecoute est: %s\n\n", serverPort);

    // Definition de la taille du tampon contenant les demandes de connexion
    ecode = listen(descSockRDV, LISTENLEN);
    if (ecode == -1)
    {
        perror("Erreur initialisation buffer d'écoute");
        exit(5);
    }

    len = sizeof(struct sockaddr_storage);
    // Attente connexion du client
    // Lorsque demande de connexion, creation d'une socket de communication avec le client
    descSockCOM = accept(descSockRDV, (struct sockaddr *)&from, &len);
    if (descSockCOM == -1)
    {
        perror("Erreur accept\n");
        exit(6);
    }

    // Echange de données avec le client connecté

    /*
     *
     *   CONNEXION DU CLIENT AU PROXY
     *
     */
    // Connection au serveur FTP
    strcpy(buffer, "220 - Bienvenue sur le Proxy, veuillez vous authentifier\r\n");
    write(descSockCOM, buffer, strlen(buffer));
    printf("Phase de connexion au proxy\n");
    printf("  - Connection au proxy (220)\n\n");
    memset(buffer, 0, MAXBUFFERLEN);

    /*
     *
     *   CONNEXION DU CLIENT ET DU PROXY AU SERVEUR
     *
     */
    // Boucle de connexion
    printf("Phase de connexion au serveur\n");
    char username[MAXBUFFERLEN] = "";
    char server[MAXBUFFERLEN] = "";
    char *result;
    bool connected = false;
    while (connected == false)
    {
        while (username[0] == '\0' && server[0] == '\0')
        {
            // Lecture de la réponse du client
            memset(buffer, 0, MAXBUFFERLEN);
            ecode = read(descSockCOM, buffer, MAXBUFFERLEN);
            // Cas de code erreur
            if (ecode == -1)
            {
                perror("  ⚠️  Erreur Programme ⚠️ - Erreur lecture");
            }
            if (ecode == 0)
            {
                printf("  Connexion au Client fermé ⭕\n");
                close(descSockCOM);
                close(descSockRDV);
                exit(0);
            }
            // Cas normal
            if (ecode > 0)
            {
                // Logs
                printf("  - Lecture réponse du client :\n");
                buffer[strcspn(buffer, "\r\n")] = '\0';
                printf("    ➥  %s\n", buffer);
                /*
                 * Gestion des commandes FTP lors de la connexion
                 */
                // AUTH :
                if (strncmp(buffer, "AUTH", 4) == 0)
                {
                    memset(buffer, 0, MAXBUFFERLEN);
                    strcpy(buffer, "500 SSL et TLS non disponible.\r\n");
                    write(descSockCOM, buffer, strlen(buffer));
                    printf("      ➥  Réponse - SSL et TLS non disponible (500)\n");
                }
                // USER :
                else if (strncmp(buffer, "USER", 4) == 0)
                {
                    result = strchr(buffer, '@');
                    if (result != NULL)
                    {
                        *result = '\0';
                        strcpy(username, buffer + 5);
                        strcpy(server, result + 1);
                    }
                    if (result == NULL || username[0] == '\0' || server[0] == '\0')
                    {
                        memset(buffer, 0, MAXBUFFERLEN);
                        strcpy(buffer, "530 Format incorrect. Utilisez user@host.\r\n");
                        write(descSockCOM, buffer, strlen(buffer));
                        username[0] = '\0';
                        server[0] = '\0';
                        printf("      ➥  Format incorrect (530)\n");
                    }
                    else
                    {
                        printf("      ➥  Format Correct, information :\n");
                        printf("        ➥  Username : %s\n", username);
                        printf("        ➥  Server : %s\n", server);
                    }
                }
                // QUIT :
                else if (strncmp(buffer, "QUIT", 4) == 0)
                {
                    memset(buffer, 0, MAXBUFFERLEN);
                    strcpy(buffer, "221 Au revoir.\r\n");
                    write(descSockCOM, buffer, strlen(buffer));
                    printf("        ➥  Deconnexion du proxy (221)\n");
                    close(descSockCOM);
                    close(descSockRDV);
                    exit(0);
                }
                // SYST :
                else if (strncmp(buffer, "SYST", 4) == 0)
                {
                    memset(buffer, 0, MAXBUFFERLEN);
                    strcpy(buffer, "530 Utilisez \"user\" pour vous authentifier (format user@host).\r\n");
                    printf("      ➥  Information du proxy (530)\n");
                    write(descSockCOM, buffer, strlen(buffer));
                }
                // AUTRES COMMANDES :
                else
                {
                    memset(buffer, 0, MAXBUFFERLEN);
                    strcpy(buffer, "502 Commande non disponible.\r\n");
                    write(descSockCOM, buffer, strlen(buffer));
                    printf("    ➥  Commande indisponible (502)\n");
                }
            }
        }

        // Connexion au serveur FTP
        connect2Server(server, FTPPORT, &descSockSERVER);
        printf("  - Tentative de connexion au serveur FTP :\n");
        // Lecture de la réponse du serveur FTP
        memset(buffer, 0, MAXBUFFERLEN);
        ecode = recv(descSockSERVER, buffer, MAXBUFFERLEN, 0);
        // Cas de code erreur
        if (ecode == -1)
        {
            memset(buffer, 0, MAXBUFFERLEN);
            strcpy(buffer, "530 Erreur de connexion au serveur FTP. Utilisez \"user\" pour vous authentifier.\r\n");
            send(descSockCOM, buffer, strlen(buffer), 0);
            printf("      ➥  Connexion au serveur FTP non établi (adresse incorrecte ou serveur indisponible) ⚠️\n");
            username[0] = '\0';
            server[0] = '\0';
        }
        if (ecode == 0)
        {
            printf("  Connexion au Serveur fermé ⭕\n");
        }
        // Cas normal
        if (ecode > 0)
        {
            buffer[strcspn(buffer, "\r\n")] = '\0';
            printf("    ➥  %s\n", buffer);
            if (strncmp(buffer, "220", 3) == 0)
            {
                // Informer l'utilisateur de la connexion réussie
                printf("  - Connexion Validé... \n");
                // Login côté serveur FTP
                strcpy(buffer, "USER ");
                strcat(buffer, username);
                strcat(buffer, "\r\n");
                send(descSockSERVER, buffer, strlen(buffer), 0);
                // Lecture de la réponse du serveur FTP
                memset(buffer, 0, MAXBUFFERLEN);
                ecode = recv(descSockSERVER, buffer, MAXBUFFERLEN, 0);
                // Cas de code erreur
                if (ecode == -1)
                {
                    perror("  ⚠️  Erreur Programme ⚠️  - Erreur lecture serveur");
                }
                if (ecode == 0)
                {
                    printf("  Connexion au Serveur fermé ⭕\n");
                }
                // Cas normal
                if (ecode > 0)
                {
                    printf("  - Réponse du serveur FTP :\n");
                    buffer[strcspn(buffer, "\r\n")] = '\0';
                    printf("    ➥  %s\n", buffer);
                    // Username correct
                    if (strncmp(buffer, "331", 3) == 0)
                    {
                        printf("      ➥  Identifiant valide\n");
                        connected = true;
                    }
                    // Username incorrect
                    else
                    {
                        memset(buffer, 0, MAXBUFFERLEN);
                        strcpy(buffer, "Connecté au serveur, Identifiant invalide !\r\n");
                        write(descSockCOM, buffer, strlen(buffer));
                        printf("      ➥  Identifiant invalide ⚠️\n");
                        username[0] = '\0';
                        server[0] = '\0';
                    }
                }
            }
        }
    }

    /*
     *
     *   AUTHENTIFICATION AU SERVEUR FTP
     *
     *
     */
    // Authentification au serveur
    memset(buffer, 0, MAXBUFFERLEN);
    strcpy(buffer, "331 Connecté au serveur, Identifiant valide, veuillez saisir votre mot de passe.\r\n");
    write(descSockCOM, buffer, strlen(buffer));
    printf("\nPhase d'authentification au proxy\n");
    printf("  - Demande de mot de passe (331)\n\n");
    bool authentified = false;
    char password[MAXBUFFERLEN] = "";
    while (authentified == false)
    {
        // Lecture de la réponse du client
        memset(buffer, 0, MAXBUFFERLEN);
        ecode = read(descSockCOM, buffer, MAXBUFFERLEN);
        // Cas de code erreur
        if (ecode == -1)
        {
            perror("  ⚠️  Erreur Programme ⚠️ - Erreur lecture");
        }
        if (ecode == 0)
        {
            printf("  Connexion au Client fermé ⭕\n");
            close(descSockCOM);
            close(descSockRDV);
            exit(0);
        }
        // Cas normal
        if (ecode > 0)
        {
            // Logs
            printf("  - Lecture réponse du client :\n");
            buffer[strcspn(buffer, "\r\n")] = '\0';
            printf("    ➥  %s\n", buffer);
            // PASS :
            if (strncmp(buffer, "PASS", 4) == 0)
            {
                // Recuperation du mot de passe
                strcpy(password, buffer + 5);
                printf("      ➥  Mot de passe : %s\n", password);
                // Envoi du mot de passe au serveur FTP
                strcat(buffer, "\r\n");
                send(descSockSERVER, buffer, strlen(buffer), 0);
                // Lecture de la réponse du serveur FTP
                memset(buffer, 0, MAXBUFFERLEN);
                ecode = recv(descSockSERVER, buffer, MAXBUFFERLEN, 0);
                // Cas de code erreur
                if (ecode == -1)
                {
                    perror("  ⚠️  Erreur Programme ⚠️  - Erreur lecture serveur");
                }
                if (ecode == 0)
                {
                    printf("  Connexion au Serveur fermé ⭕\n");
                }
                // Cas normal
                if (ecode > 0)
                {
                    printf("  - Réponse du serveur FTP :\n");
                    buffer[strcspn(buffer, "\r\n")] = '\0';
                    printf("    ➥  %s\n", buffer);
                    if (strncmp(buffer, "230", 3) == 0)
                    {
                        // Informer le client de l'authentification réussie
                        memset(buffer, 0, MAXBUFFERLEN);
                        strcpy(buffer, "230 Authentification réussie.\r\n");
                        write(descSockCOM, buffer, strlen(buffer));
                        printf("      ➥  Authentification réussie (230)\n");
                        authentified = true;
                    }
                    else
                    {
                        // Nouvelle demande de mot de passe
                        memset(buffer, 0, MAXBUFFERLEN);
                        strcpy(buffer, "530 Authentification échouée. Veuillez réessayer avec \"PASS\".\r\n");
                        write(descSockCOM, buffer, strlen(buffer));
                        printf("      ➥  Authentification échouée (530)\n");
                    }
                }
            }
        }
    }

    memset(buffer, 0, MAXBUFFERLEN);
    strcpy(buffer, "230 Connecté au serveur, Authentification réussie valide !\r\n");
    write(descSockCOM, buffer, strlen(buffer));
    printf("\nConnexion établie (Client <---> Proxy <---> Serveur FTP) ✅\n\n");

    // Fermeture de la connexion
    close(descSockCOM);
    close(descSockRDV);
}
