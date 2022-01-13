# Démon information système
 
## Pré-requis :
- Installer Make
- Système d'exploitation Linux 
 
 ## Lancement de l'application
1. Nettoyage				: ```make mrproper```
2. Construction des dossiers obj et bin	: ```make start_build```
3. Lancement du démon			: ```make start_daemon```
4. Lancement d'un client		: ```make start_client```
L'étape 4 peut être répété plusieurs fois.

- Bibliothèque des fonctionnalités d’utilisation de la zone de mémoire partagée : ```make lib_shm```
 Le fichier est générée dans le dossier lib et porte l'extension "so"
