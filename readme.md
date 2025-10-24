# Pilote Ring Buffer - ELE784 Lab3

## Description des fichiers

### buf_driver.c
Fichier principal du pilote noyau Linux implémentant un tampon circulaire thread-safe.

**Structures de données :**
- `BufStruct Buffer` : Gère le tampon circulaire avec indices d'écriture (InIdx) et lecture (OutIdx), drapeaux plein/vide, taille configurable
- `Buf_Dev BDev` : Structure du dispositif contenant sémaphore de protection, files d'attente, compteurs de lecteurs/écrivains, tampons locaux

**Fonctions principales :**
- `buf_init()` : Initialise le module, alloue la mémoire du buffer, enregistre le device avec major/minor, crée `/dev/buf0`
- `buf_exit()` : Libère toutes les ressources (mémoire, device, class)
- `buf_open()` : Gère l'ouverture du device, impose un seul écrivain, alloue ReadBuf/WriteBuf
- `buf_release()` : Ferme le device, décrémente les compteurs
- `buf_read()` : Lit des données (unsigned short) depuis le buffer, supporte modes bloquant/non-bloquant
- `buf_write()` : Écrit des données dans le buffer, supporte modes bloquant/non-bloquant
- `buf_ioctl()` : Exécute les commandes de contrôle (statistiques, redimensionnement)
- `BufIn()` / `BufOut()` : Insèrent/extraient une donnée du tampon circulaire

**Mécanismes de synchronisation :**
- Sémaphore binaire (`SemBuf`) protège l'accès concurrent au buffer
- Files d'attente (`InQueue`, `OutQueue`) bloquent les processus quand buffer plein/vide

### buf_ioctl.h
Définit les commandes IOCTL pour interagir avec le driver :
- `BUF_IOCGETNUMDATA` : Retourne le nombre d'éléments dans le buffer
- `BUF_IOCGETNUMREADER` : Retourne le nombre de lecteurs actifs
- `BUF_IOCGETBUFSIZE` : Retourne la taille actuelle du buffer
- `BUF_IOCSETBUFSIZE` : Redimensionne le buffer (nécessite privilèges root/CAP_SYS_RESOURCE)

### test_app.c
Programme utilisateur de test avec interface menu interactif.

**Fonctionnalités :**
- `read_data()` : Lit 2 valeurs unsigned short depuis `/dev/buf0`
- `write_data()` : Écrit 2 valeurs unsigned short dans `/dev/buf0`
- `ioctl_test()` : Teste toutes les commandes IOCTL (statistiques, redimensionnement)
- Menu permettant de choisir le mode d'accès (O_RDONLY, O_WRONLY, O_RDWR) et le mode (bloquant/non-bloquant)

---

## Instructions de compilation

### Prérequis
```bash
# Installer les outils de développement (Ubuntu/Debian)
sudo apt-get install build-essential linux-headers-$(uname -r)
```

### Compiler le driver
```bash
# Dans le répertoire du driver
cd driver
make
```

### Compiler le programme de test
```bash
# Dans le répertoire de l'application
cd app
make
```

---

## Instructions d'utilisation

### 1. Charger le module
```bash
sudo insmod buf_driver.ko

# Vérifier le chargement
lsmod | grep buf
dmesg | tail
```

### 2. Vérifier le device
```bash
# Le device /dev/buf0 est créé automatiquement
ls -l /dev/buf0
```

### 3. Exécuter le programme de test
```bash
./test_app
```

**Options du menu :**
- **1. Read** : Lit 2 valeurs depuis le buffer
- **2. Write** : Écrit 2 valeurs dans le buffer (un seul écrivain autorisé)
- **3. Read/Write** : Teste lecture et écriture
- **4. IOCTL Test** : Affiche statistiques et permet redimensionnement
- **Mode bloquant** : Attend si buffer vide (lecture) ou plein (écriture)
- **Mode non-bloquant** : Retourne immédiatement avec erreur si pas de données/espace

### 4. Décharger le module
```bash
sudo rmmod buf_driver
dmesg | tail
```

---

## Exemple d'utilisation

```bash
# Terminal 1 : Écrire des données
./test_app
# Choisir : 2 (Write) -> 1 (Blocking)
# Entrer : 100 200

# Terminal 2 : Lire les données
./test_app
# Choisir : 1 (Read) -> 1 (Blocking)
# Résultat : Read 4 bytes: 100 200

# Terminal 3 : Vérifier les statistiques
./test_app
# Choisir : 4 (IOCTL Test) -> affiche nombre de données, lecteurs, taille
```

---

## Description des tests effectués

### Test 1 : Plusieurs écrivains
**Objectif** : Vérifier l'exclusivité de l'accès en écriture

**Procédure :**
1. Terminal 1 : Ouvrir `/dev/buf0` en mode écriture (O_WRONLY)
2. Terminal 2 : Tenter d'ouvrir `/dev/buf0` en mode écriture (O_WRONLY)

**Résultat attendu :** 
- Terminal 1 : Ouverture réussie
- Terminal 2 : Échec avec erreur "Open failed: Device or resource busy" (-EBUSY)

**Conclusion :** Le driver impose correctement l'exclusivité d'accès en écriture (un seul écrivain à la fois)

### Test 2 : Un écrivain + plusieurs lecteurs
**Objectif** : Vérifier l'accès concurrent en lecture avec un écrivain actif

**Procédure :**
1. Terminal 1 : Ouvrir `/dev/buf0` en mode écriture (O_WRONLY), écrire des valeurs (ex: 100, 200)
2. Terminal 2 : Ouvrir `/dev/buf0` en mode lecture (O_RDONLY), lire les valeurs
3. Terminal 3 : Ouvrir `/dev/buf0` en mode lecture (O_RDONLY), lire les valeurs
4. Terminal 1 : Écrire d'autres valeurs (ex: 300, 400)

**Résultat attendu :**
- Terminal 1 : Écriture réussie
- Terminal 2 : Lecture réussie (100, 200 puis 300, 400)
- Terminal 3 : Lecture réussie (peut lire les mêmes données ou les suivantes)
- Pas de blocage ni d'erreur

**Conclusion :** Le driver permet correctement plusieurs lecteurs simultanés tout en maintenant un écrivain exclusif. La synchronisation fonctionne correctement entre lecteurs et écrivain.

---

## Problèmes connus

### Limitation d'un seul écrivain
- **Description** : Un seul processus peut ouvrir `/dev/buf0` en mode écriture à la fois
- **Impact** : Applications multi-écrivains nécessitent une coordination externe
- **Comportement** : Le deuxième écrivain reçoit l'erreur -EBUSY

### Alignement des données
- **Contrainte** : Les opérations read/write doivent être des multiples de `sizeof(unsigned short)` (2 octets)
- **Impact** : Tentative de lire/écrire un nombre impair d'octets retourne -EINVAL

---

## Informations techniques

- **Auteur** : Anis Chabi
- **Licence** : Dual BSD/GPL
- **Taille par défaut du buffer** : 256 éléments (512 octets)
- **Type de données** : unsigned short (2 octets)
- **Device** : /dev/buf0 (major dynamique, minor 0)
