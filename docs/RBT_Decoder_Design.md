# Description du décodage RBT (basée uniquement sur les commentaires de ScummVM)

Source : commentaires extraits de `ScummVM/robot.h` et `ScummVM/robot.cpp`.

But : fournir une description claire et précise, utilisable comme spécification pour implémenter un décodeur RBT (extraction d'images, vidéo, son et métadonnées).

== Vue d'ensemble ==

- Robot est un conteneur AV paquetisé qui encode plusieurs bitmaps (cels) + données de positionnement et audio synchronisé pour le rendu dans le système graphique SCI.
- Le format n'utilise habituellement pas de compression inter-Image : la compression est locale à chaque cel (blocs contigus).
- Le fichier contient : un en-tête, une section audio primer optionnelle, une palette optionnelle, un index de tailles de trames, une table de cues (temps/valeurs), puis des paquets vidéo+audio variables.
- Les entiers sont codés selon l'endianness « native » du fichier (LSB pour x86, MSB pour 68k/PPC). Le type de endianness peut être déterminé en examinant le champ version (commentaire dans le code).

== Endianness ==

- Déterminer l'endianness en lisant le champ `version` (2 octets) : certains jeux distribuaient des fichiers PC/Mac identiques ; l'endianness n'est pas toujours corrélée à la plateforme.

== En-tête de fichier (v5/v6) ==

Disposition (champ | signification) — champs en octets selon les commentaires :

- 0 : signature 0x16
- 1 : unused
- 2-5 : signature 'SOL\0'
- 6-7 : version (4,5,6 connus)
- 8-9 : taille des blocs audio
- 10-11 : flag "primer is compressed"
- 12-13 : unused
- 14-15 : nombre total de trames (frames)
- 16-17 : taille de la palette embarquée (en octets)
- 18-19 : primer reserved size
- 20-21 : X-resolution des coordonnées (0 => utiliser la résolution du jeu)
- 22-23 : Y-resolution des coordonnées (0 => utiliser la résolution du jeu)
- 24 : flag non-zero => Robot inclut une palette
- 25 : flag non-zero => Robot inclut de l'audio
- 26-27 : unused
- 28-29 : frame rate (en fps)
- 30-31 : coordinate conversion flag (si vrai, utiliser les coordonnées telles quelles pour l'affichage spécifique d'une frame)
- 32-33 : max number of packets that can be skipped without causing audio drop-out
- 34-35 : max cels par frame possible
- 36-51 : 4 valeurs 32-bit : tailles maximales possibles pour les 4 cels fixes
- 52-59 : unused

Remarques d'alignement :
- Après l'index de tailles de paquets/frames et les cuepoints, le premier bloc de trame est aligné sur le prochain secteur de 2048 octets (`kRobotFrameSize = 2048`).

== Primer audio ==

- Si le flag "file includes audio" est faux, il faut sauter `primer_reserved_size` octets après l'en-tête pour passer une zone de padding.
- Si le flag audio est vrai et `primer_reserved_size` != 0, les données immédiatement après l'en-tête sont un "audio primer header" + les données audio compressées.

Audio primer header (ordre des champs dans les commentaires) :
- 0-3 : taille totale (bytes) de la section primer
- 4-5 : compression format du primer (doit être zéro)
- 6-9 : taille (bytes) du "even" primer
- 10-13 : taille (bytes) du "odd" primer

- Si la somme des tailles even+odd ne correspond pas à `primer_reserved_size`, le prochain header peut être trouvé `primer_reserved_size` octets depuis le début du primer header.
- Si `primer_reserved_size` == 0 et `primerZeroCompressFlag` est set, valeurs par défaut : even=19922, odd=21024 et les buffers doivent être remplis de zéros.
- Toute autre combinaison de flags est déclarée comme erreur (selon les commentaires).

== Palette ==

- Si le flag `hasPalette` est vrai, lire `paletteSize` octets comme une SCI HunkPalette; sinon, sauter `paletteSize` octets.

== Indexs et tables ==

- Video frame size index : pour v5 lire `total_frames` 16-bit ints; pour v6 lire 32-bit ints.
- Packet size index (tailles combinées compressées de video+audio par frame) : pareil (16-bit v5, 32-bit v6).
- Cue times : lire 256 entiers 32-bit (nombre de ticks depuis le début de la playback correspondants aux cue points).
- Cue values : lire 256 entiers 16-bit (valeurs remises au moteur de jeu quand cue demandée).

== Accès aux trames et seeking ==

- Pour atteindre la trame N, calculer l'adresse du paquet de trame en sommant les entrées de l'index de packet sizes jusqu'à N.
- Attention : le seeking aléatoire désactive normalement la lecture audio, car l'audio dans un paquet ne correspond pas forcément à la video du même paquet (commentaires explicites).

== Paquet de trame ==

- Format : [video data (taille = entrée video frame size index)] [optionnel audio data (taille = size of audio blocks)].

== Video data (format interne) ==

- Début : 0-2 : nombre de cels dans la trame (max 10)
- Puis : cels (chaque cel comprend header + data chunks)

Cel header (22 octets, `kCelHeaderSize = 22`) :
- 0 : unused
- 1 : vertical scale factor en pourcent (100 = pas de décimation ; 50 = 50% des lignes ont été supprimées)
- 2-3 : cel width
- 4-5 : cel height
- 6-9 : unused
- 10-11 : cel x-position (coord Robot)
- 12-13 : cel y-position (coord Robot)
- 14-15 : total cel data chunk size (bytes)
- 16-17 : nombre de data chunks

Cel data chunk :
- header (0-9) puis `cel data`

Cel data chunk header :
- 0-3 : compressed size
- 4-7 : decompressed size
- 8-9 : compression type (0 = LZS, 2 = non compressé)

- Un cel est construit à partir d'un ou plusieurs blocs contigus, chaque bloc pouvant être compressé avec LZS ou laissé non compressé.
- Une ligne décimée (vertical decimation) peut être appliquée : certaines lignes ont été supprimées à la compression et doivent être reconstruites par interpolation lors de la décompression (voir `expandCel` / `verticalScaleFactor`).
- Chaque cel inclut les coordonnées où il doit être placé dans la frame, par rapport au coin supérieur gauche.
- Après la décompression de chaque chunk (ou copie si non compressé), les données décompressées sont concaténées pour obtenir le bitmap final du cel.
- Si `verticalScaleFactor != 100`, effectuer un traitement d'expansion vertical (replicate/scale lines) d'après le facteur indiqué.
- Si `usePalette` est vrai, copier la palette lue précédemment dans la hunk palette du bitmap.

== Compression vidéo supportée ==

- `kCompressionLZS` (0) : les blocs LZS doivent être décompressés via le décompresseur LZS.
- `kCompressionNone` (2) : les données sont stockées en clair.

== Audio ==

Principes généraux (extraits de commentaires) :

- L'audio est codé en taille fixe : tous les audio blocks sauf le primer audio ont la même taille.
- Encodage : Sierra SOL DPCM16.
- L'audio est split en deux canaux ('even' et 'odd'), chacun à 11025Hz sample rate ; le signal original est restauré en intercalant les échantillons des deux canaux.
- `RobotAudioStream` expose : `kRobotSampleRate = 22050` et `kEOSExpansion = 2`.

Structure audio dans un paquet (d'après les commentaires) :
- audio data header (8 octets)
- DPCM runway (8..15) : 8 octets non écrits dans la sortie — utilisés pour positionner le signal (le 9ème échantillon commence la donnée valide)
- compressed audio data (16..n)

Audio data header :
- 0-3 : absolute position of audio in the audio stream
- 4-7 : size of the audio block (excl. header)

Traitement recommandé (extrait) pour un bloc audio :
1. Vérifier que la plage décompressée (`position * 2 + length * 4`) dépasse la fin du dernier paquet du même parité (even/odd). Si des données ont déjà été écrites au-delà pour ce canal, ou si la tête de lecture a déjà lu au-delà, jeter le bloc.
2. Appliquer la décompression DPCM sur tout le bloc, en commençant depuis le début du DPCM runway, en utilisant une valeur d'échantillon initiale de 0.
3. Copiez chaque échantillon du source décompressé situé en dehors du DPCM runway dans chaque *autre* échantillon du buffer final (1 -> 2, 2 -> 4, 3 -> 6, etc.). Ceci correspond au fait que chaque canal écrit alternativement dans l'audio final intercalé.
4. Pour tout échantillon manquant où le canal opposé n'a pas encore écrit, interpoler la zone manquante en faisant la moyenne (somme puis division par deux) des échantillons voisins de ce bloc. Ne pas écraser des échantillons "véritables" écrits par le canal opposé.

Remarques supplémentaires issues des commentaires de `robot.cpp` :
- `packet.position` est la position décompressée (dédoublée) du paquet, donc les valeurs seront divisibles soit par 2 (pair) soit par 4 (impair) — cela indique la parité (even/odd) et sert à décider du `bufferIndex` pour écriture.
- Packet 0 : premier primer, Packet 2 : second primer, Packet 4+ : données audio régulières.
- Les 8 premiers octets du next packet sont des données "garbage" (runway) utilisées pour amener la forme d'onde à la bonne position à cause du DPCM. Ne pas les écrire directement.
- Lorsque seule une partie d'un packet a pu être écrite dans le loop buffer (par manque de place), il faut renseigner le caller pour renvoyer ce packet plus tard (mécanique de rejets/retours).
- En cas d'absence de données pour un canal pendant une partie de la lecture, procéder à une interpolation (moyenne des voisins) pour garder de l'audio, dégradant éventuellement la qualité vers 11kHz si nécessaire.

== Gestion du primer / initialisation audio ==

- `primeAudio(startTick)` : si `startTick == 0`, lire les even/odd primer buffers et les pousser dans la queue audio. Sinon, calculer la position audio de départ et, si nécessaire, lire le même primer et soumettre des parties pour aligner la lecture.
- `firstAudioRecordPosition` est calculé à partir des tailles de primer.

== Points de prudence / erreurs détectées par les commentaires ==

- Le champ `version` sert aussi à valider la compatibilité (`_version` doit être 5 ou 6 dans le code). Versions inconnues => erreur.
- Compression primer != 0 => erreur.
- Combinations invalides des flags primer/audio => erreur.
- Si la lecture de vidéo ou d'audio échoue (`stream->err()`), considérer comme une erreur.
- Lors de seeking aléatoire : l'audio peut être désynchronisé / désactivé (les données audio dans un paquet ne correspondent pas nécessairement à la vidéo du même paquet).

== Données utiles (constantes mentionnées dans commentaires) ==

- `kRobotSampleRate = 22050` (taux d'échantillonnage final attendu).
- `kEOSExpansion = 2` (les paquets écrivent dans chaque autre échantillon).
- `kRobotFrameSize = 2048` (alignement secteur CD pour le premier bloc de trame).
- `kCelHeaderSize = 22`.
- Primer fallback sizes quand `primerZeroCompressFlag` vrai : even=19922, odd=21024.

== Flux d'implémentation recommandé (spécifié à partir des commentaires) ==

1. Ouvrir le flux et déterminer l'endianness via le champ `version`.
2. Valider la signature (`0x16` puis `"SOL\0"`).
3. Lire l'en-tête complet (audio block size, primer flags, nb de frames, paletteSize, primerReservedSize, résolutions, hasPalette, hasAudio, framerate, max skippable, max cels par frame, max cel areas...).
4. Si `hasAudio` et `primerReservedSize != 0`, lire le primer header et charger les buffers even/odd (ou sauter si incongru). Si `primerReservedSize == 0` et `primerZeroCompressFlag` est vrai, allouer et zero-fill selon les tailles par défaut.
5. Si `hasPalette`, lire `paletteSize` octets dans `rawPalette` ; sinon sauter `paletteSize` octets.
6. Lire l'index des tailles vidéo (16/32-bit selon version) et l'index des packet sizes (16/32-bit selon version).
7. Lire cue times (256 x 32-bit) et cue values (256 x 16-bit).
8. Aligner la position sur le prochain secteur 2048-octets.
9. Pour décoder une frame :
   - Lire `videoSize` octets pour la frame depuis le flux.
   - Lire nCels depuis les premiers octets de la frame.
   - Pour chaque cel : lire le header (22 octets) puis pour chaque data chunk : lire compressedSize, decompressedSize, compressionType; décompresser si LZS, copier si non compressé et concaténer.
   - Si `verticalScaleFactor != 100`, appliquer `expandCel` (interpolation/replication de lignes) pour restaurer la hauteur.
   - Copier la palette si `usePalette`.
   - Positionner la cel dans la frame selon les coordonnées fournies.
10. Gestion audio pendant la lecture :
   - Lire la partie audio du packet (audio header + runway + compressed data).
   - Pour chaque audio block : valider position/overlap recommandé, décompresser DPCM en partant du runway (initial sample 0), remplir un buffer temporaire, puis écrire chaque échantillon décompressé dans chaque *autre* échantillon du buffer final (interleaving even/odd).
   - Si des échantillons opposés manquent, interpoler par moyenne (sans écraser les vraies données déjà présentes).
11. Gérer un loop buffer d'audio (RobotAudioStream) qui prend en compte : positions absolues décompressées, parité (bufferIndex even/odd), primer handling (packets 0/2), et retours au caller si la mémoire tampon est pleine.
12. Lors du seeking aléatoire, avertir que l'audio peut être désactivé / requérir re-primer.

== Limitations explicites (tirées des commentaires) ==

- Certaines informations nécessaires au rendu correct (mapping de palette remapping, résolution des coordonnées, background/context) peuvent provenir du moteur du jeu et ne pas être présentes dans le fichier RBT ; le décodage hors moteur peut donc être partiel.
- Le format n'utilise pas d'interframe compression — chaque frame est décodée indépendamment.

== Annexes / Références (extraits de commentaires) ==

- `packet.position` est la position décompressée (doubled) ; utilisé pour décider si un paquet est `even` ou `odd` et calculer `bufferIndex`.
- Les 8 premiers octets d'un bloc audio sont utilisés comme runway pour le DPCM et ne doivent pas être copiés en sortie immédiate.
- Les étapes de validation/erreur indiquées dans les commentaires doivent être respectées pour détecter fichiers corrompus ou flags invalides.

---

Fichier généré uniquement à partir des commentaires présents dans `ScummVM/robot.h` et `ScummVM/robot.cpp` pour servir de spécification d'implémentation d'un décodeur RBT (images, vidéo, son, métadonnées). Pour implémenter, suivez le "Flux d'implémentation recommandé" et respectez strictement les structures de champs décrites ci-dessus.