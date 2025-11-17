RBT — Spécification enrichie pour le décodage (documentation fusionnée)

But

Spécification technique complète et exploitable pour implémenter un décodeur/extracteur de fichiers `.rbt` (images/cels, vidéo, audio et métadonnées). Ce document fusionne :
- l'explication tirée des commentaires (`RBT_Decoder_Design.md`) et
- les détails comportementaux observés dans le code (`RBT_Decoder_Design_code_based.md`).

L'objectif est de fournir à la fois la structure du format et les règles de lecture/traitement réellement appliquées par le code de ScummVM, de manière à pouvoir implémenter un extracteur fidèle.

**Constantes et valeurs importantes**:
- `kRobotSampleRate = 22050` (taux d'échantillonnage final pour l'audio).
- `kEOSExpansion = 2` (les paquets audio écrivent dans chaque "autre" échantillon du buffer final).
- `kRobotFrameSize = 2048` (alignement secteur, utilisé pour aligner le premier bloc de trame).
- `kRobotZeroCompressSize = 2048` (taille pour remplir quand l'audio est zero-compress).
- `kAudioBlockHeaderSize = 8` (taille de l'en-tête audio dans les enregistrements).
- `kCelHeaderSize = 22` (taille en octets d'un header de cel)
- Compression vidéo reconnue : `kCompressionLZS = 0`, `kCompressionNone = 2`.

**0) Principes**
- Un fichier RBT est un conteneur AV paquetisé qui contient : en-tête principal, (optionnel) primer audio, (optionnelle) palette, indexs (taille vidéo, tailles complètes), tables de cues, puis paquets de trames (chaque paquet contient la vidéo de la trame et éventuellement de l'audio attaché).
- Le format ne fait pas d'interframe-compression (chaque frame est indépendante, la compression est appliquée par-chunk à l'intérieur d'un cel).
- Les entiers utilisent l'endianness du fichier : le code déduit l'endianness en lisant la valeur version d'une manière qui permet d'identifier ordre d'octets.

**1) Lecture initiale et détermination de l'endianness**
- Ouvrir le flux (membre ressource, p.ex. "%d.rbt").
- Lire 16 bits en little-endian initialement (`readUint16LE`) et vérifier la signature initiale `0x16`.
- Seek à offset 6 et lire 16 bits BE (`readUint16BE`) pour obtenir la `version` et en déduire si le fichier est big-endian ; ensuite envelopper le flux par un `SeekableReadStreamEndianWrapper(stream, bigEndian, ...)` pour les lectures suivantes.
- Après adaptation d'endianness, se positionner à offset 2 et vérifier l'étiquette `MKTAG('S','O','L',0)` (lecture 32-bit BE). Si non valide, erreur.

**2) Ordre et types des champs de l'en-tête (lecture exacte)**
Lire dans cet ordre (tous les champs lus via `_stream` après enveloppe d'endianness) :
- `_version = readUint16()`
- `_audioBlockSize = readUint16()`
- `_primerZeroCompressFlag = readSint16()`
- skip 2 octets
- `_numFramesTotal = readUint16()`
- `paletteSize = readUint16()`
- `_primerReservedSize = readUint16()`
- `_xResolution = readSint16()`
- `_yResolution = readSint16()`
- `hasPalette = readByte()`
- `_hasAudio = readByte()`
- skip 2 octets
- `_frameRate = readSint16()`
- `_isHiRes = readSint16()`
- `_maxSkippablePackets = readSint16()`
- `_maxCelsPerFrame = readSint16()`
- lire 4x `_maxCelArea.push_back(readSint32())`
- skip 8 octets

Remarque : respecter cet ordre et les largeurs de champ exactes (signed/unsigned) — le code de ScummVM s'appuie sur cet ordre.

**3) Primer audio — lecture et règles**
- Si `_hasAudio` :
  - Si `_primerReservedSize != 0` :
    - lire `_totalPrimerSize = readSint32()`
    - lire `compressionType = readSint16()` (le code attend 0, sinon erreur)
    - lire `_evenPrimerSize = readSint32()` et `_oddPrimerSize = readSint32()`
    - `_primerPosition = current stream pos`
    - si `_evenPrimerSize + _oddPrimerSize != _primerReservedSize` => seeker à `primerHeaderPosition + _primerReservedSize`.
  - Sinon si `_primerReservedSize == 0` et `_primerZeroCompressFlag` vrai : les tailles implicites utilisées par le code sont `_evenPrimerSize = 19922; _oddPrimerSize = 21024;` et les buffers sont remplis de zéros.
- Fonction de lecture : si `_totalPrimerSize != 0` on seek `_primerPosition` puis on lit `_evenPrimerSize` et `_oddPrimerSize` dans des buffers fournis ; sinon, si zero-compress flag on memset 0 les buffers ; sinon erreur.

**4) Palette**
- Après le primer, si `hasPalette` lire `paletteSize` octets dans `_rawPalette` ; sinon `seek(paletteSize, SEEK_CUR)`.
- Lors du rendu d'un cel si `usePalette` vrai, copier `_rawPalette` dans la hunk palette du bitmap.

**5) Index des tailles et positions**
- Pour `_version == 5` : lire `_numFramesTotal` valeurs 16-bit pour `_videoSizes`, puis `_numFramesTotal` valeurs 16-bit pour `recordSizes`.
- Pour `_version == 6` : les mêmes tables mais en 32-bit signés.
- Lire ensuite `kCueListSize` (=256) valeurs 32-bit pour `_cueTimes` puis 256 valeurs 16-bit pour `_cueValues`.
- Aligner la position du flux au prochain multiple de `kRobotFrameSize` (2048) :
  - `bytesRemaining = (pos - fileOffset) % kRobotFrameSize` ; si != 0 seek `kRobotFrameSize - bytesRemaining`.
- Construire `_recordPositions` : initialiser `position = stream.pos()` et pour chaque frame pousser `position` puis `position += recordSizes[i]`.

**6) Structure d'un paquet de trame (frame packet)**
- Le paquet de trame contient en premier la `video data` de taille `videoSize` (issue de `_videoSizes`), suivie optionnellement d'une partie audio attachée pour cette trame (taille = audio block size lus depuis l'en-tête).

**7) Décodage de la vidéo (frames/cels)**
- Lire `videoSize` octets pour la frame; lire les 2 premiers octets (macro READ_SCI11ENDIAN_UINT16) pour obtenir `screenItemCount` (nombre de cels dans la frame).
- Appeler `createCels5` avec le pointeur sur la donnée vidéo (après ces 2 octets) et `screenItemCount`.

createCels5 :
- Appeler `preallocateCelMemory(rawVideoData, numCels)` pour préparer les buffers/bitmaps nécessaires.
- Boucler `numCels` fois en appelant `createCel5` et en avançant `rawVideoData` de la valeur retournée par chaque `createCel5`.

createCel5 (procédure de lecture et décompression) :
- Lire depuis `rawVideoData` :
  - `_verticalScaleFactor = rawVideoData[1]` (octet)
  - `celWidth = READ_SCI11ENDIAN_UINT16(rawVideoData + 2)`
  - `celHeight = READ_SCI11ENDIAN_UINT16(rawVideoData + 4)`
  - `celPosition.x = READ_SCI11ENDIAN_UINT16(rawVideoData + 10)`
  - `celPosition.y = READ_SCI11ENDIAN_UINT16(rawVideoData + 12)`
  - `dataSize = READ_SCI11ENDIAN_UINT16(rawVideoData + 14)`
  - `numDataChunks = READ_SCI11ENDIAN_UINT16(rawVideoData + 16)`
- Avancer `rawVideoData += kCelHeaderSize` (22 octets)
- Pour chaque chunk (0..numDataChunks-1) :
  - `compressedSize = READ_SCI11ENDIAN_UINT32(rawVideoData)`
  - `decompressedSize = READ_SCI11ENDIAN_UINT32(rawVideoData + 4)`
  - `compressionType = READ_SCI11ENDIAN_UINT16(rawVideoData + 8)`
  - `rawVideoData += 10` (taille de l'entête chunk)
  - Si `compressionType == kCompressionLZS` : décompresser avec l'API LZS du projet ( `_decompressor.unpack(&videoDataStream, targetBuffer, compressedSize, decompressedSize)` )
  - Si `compressionType == kCompressionNone` : `copy(rawVideoData, rawVideoData + decompressedSize, targetBuffer)`
  - Avancer `rawVideoData += compressedSize; targetBuffer += decompressedSize`
- Après avoir concaténé les chunks, si `_verticalScaleFactor != 100` appeler `expandCel` pour restaurer la hauteur correcte (replication/interpolation de lignes depuis le buffer intermédiaire vers le bitmap final).
- Si `usePalette` est vrai, copier `_rawPalette` -> bitmap hunk palette.
- `createCel5` retourne `kCelHeaderSize + dataSize` (taille consommée du flux pour ce cel).

Remarques pratiques vidéo :
- Les données de chunks sont concaténées pour reconstituer le bitmap brut du cel ; LZS est le seul algorithme de compression appelé depuis le code.
- Les coordonnées du cel sont en "Robot coordinates" ; le moteur (ou l'implémentation d'extraction) peut devoir convertir ces valeurs selon la résolution cible et flags `isHiRes`/conversion de coordonnées.

**8) Audio : format, lecture et reconstruction**
- Quand on veut lire l'audio attaché à une trame : dans `readAudioDataFromRecord(frameNo, outBuffer, outAudioPosition, outAudioSize)` le code :
  - Seek à `_recordPositions[frameNo] + _videoSizes[frameNo]` (audio suit immédiatement la vidéo dans le paquet)
  - `position = readSint32()` (position absolue compressée de l'audio dans le flux audio)
  - `size = readSint32()` (taille du bloc audio, hors header)
  - Si `position == 0` -> pas d'audio pour cette trame
  - Si `size != _expectedAudioBlockSize` : le code prépend `kRobotZeroCompressSize` octets de zéros à `outBuffer` et lit ensuite `size` octets ; augmente `size += kRobotZeroCompressSize`.
  - Sinon : lit `size` octets directement dans `outBuffer`.
  - Retourne `outAudioPosition = position`, `outAudioSize = size`.

- Le code gère un système d'AudioList qui stocke des AudioBlocks; lors de la soumission au moteur audio chaque AudioBlock crée un `RobotAudioPacket` via `RobotAudioPacket(_data, _size, (_position - startOffset) * 2)` — noter la multiplication par 2.

- RobotAudioStream (loop buffer) : comportement observé important :
  - `addPacket(packet)` calcule `bufferIndex` par `packet.position % 4 ? 1 : 0` pour déterminer l'écriture dans le côté "even" ou "odd". Les primers (packets 0 et 2) initialisent l'état.
  - `fillRobotBuffer(packet, bufferIndex)` :
    - Décompresse DPCM en appelant `deDPCM16Mono` et stocke le flux PCM 16-bit dans `_decompressionBuffer`.
    - Calcule bornes d'écriture et écrit dans `_loopBuffer` en utilisant `copyEveryOtherSample` pour écrire chaque échantillon du buffer source dans "every other sample" dans la cible (c'est l'interleaving des deux canaux).
    - Si le canal opposé n'a pas écrit pour certaines zones, utiliser `interpolateChannel` pour générer des échantillons par moyenne (éviter d'écraser les vrais échantillons de l'autre canal).
    - Gérer cas de buffer plein et lectures partielles (retourner `false` pour indiquer au caller que le paquet doit être renvoyé).
  - `readBuffer(outBuffer, numSamples)` effectue interpolation manquante, copie des échantillons en gérant wrap-around et avance `_readHeadAbs`.

- Détails DPCM : la fonction `deDPCM16Mono(int16 *out, const byte *in, const uint32 numBytes, int16 &sample)` est appelée par le code ; elle reconstruit des échantillons 16-bit à partir du flux compressé SOL/DPCM. Il faut réutiliser/implémenter cette routine pour obtenir les `int16` bruts avant interleaving.

**9) Synchronisation AV et seeking**
- Le code calcule `_recordPositions` pour permettre `seekToFrame(frameNo)` via `stream.seek(_recordPositions[frameNo], SEEK_SET)`.
- Le seeking aléatoire (jump direct à une trame) fonctionne mais peut casser l'audio : l'audio dépend du primer et des positions absolues — l'implémentation du code prend en charge des opérations de priming (`primeAudio`) et de lecture partielle d'enregistrements pour resynchroniser.
- `primeAudio(startTick)` construit la queue initiale d'audio en lisant le primer (ou zero-filled primer), puis ajoute des blocs audio pour atteindre le point de départ souhaité en calculant `audioStartPosition` et en utilisant `readPartialAudioRecordAndSubmit` si nécessaire.

**10) Métadonnées et tables utiles**
- `_numFramesTotal` (nombre total de frames).
- `_videoSizes` (taille compressée de la partie vidéo pour chaque frame).
- `_recordPositions` (offsets byte pour chaque frame dans le flux).
- `_cueTimes` (256 x 32-bit) et `_cueValues` (256 x 16-bit) — tables cue.
- `_rawPalette` et `paletteSize`.
- `_audioBlockSize`, `_expectedAudioBlockSize`, `_primerReservedSize`, `_evenPrimerSize`, `_oddPrimerSize`.

**11) Règles d'erreur et validations observées**
- Version hors plage (le code accepte 5 ou 6) => erreur.
- `compressionType` dans primer header différent de 0 => erreur.
- Si flags primer/audio incohérents, le code déclenche une erreur.
- En cas d'erreur de lecture (`stream->err()`), le décodeur renvoie erreur/échoue.

**12) Recommandations d'implémentation pour un extracteur**
- Reproduire strictement l'ordre et le type des lectures de l'en-tête.
- Gérer l'endianness comme le code : lire le champ `version` via `readUint16BE` après un repositionnement initial pour inférer `bigEndian` puis envelopper le flux pour lectures ultérieures.
- Implémenter / réutiliser :
  - décompresseur LZS compatible LZS utilisé par `_decompressor.unpack` ;
  - fonction `deDPCM16Mono` pour reconstruire les `int16` à partir des données SOL/DPCM ;
  - routines `copyEveryOtherSample` et `interpolateChannel` pour reconstruire l'audio intercalé et combler les trous.
- Pour l'audio final : reconstituer un flux PCM 16-bit à 22050 Hz en intercalant les sorties even/odd et en appliquant la logique d'interpolation décrite.
- Sauvegarder pour chaque frame :
  - tous les cels décodés en bitmaps (format PNG recommandé), avec leurs positions (x,y), leur largeur/hauteur et l'éventuelle palette associée ;
  - les données audio associées (soit sous forme de blocs compressés + position, soit décompressées dans un fichier WAV reconstruit 22050Hz).

**13) Exemples d'algorithmes (pseudo-résumé rapide)**
- Lecture d'une frame i :
  - seek `_recordPositions[i]`
  - lire `videoSize = _videoSizes[i]`
  - buffer := read(videoSize) ; nCels := READ_SCI11ENDIAN_UINT16(buffer)
  - pour chaque cel : appliquer `createCel5` (lire header, parcourir chunks, LZS ou copy, concaténer, expand vertical si nécessaire)
  - si audio présent : `seek(_recordPositions[i] + videoSize)` ; lire `position,size` ; lire `size` octets ; stocker/décompresser et soumettre au pipeline audio.

- Reconstruction audio (bloc unique) :
  - si `size != expected` : prepend zero-buffer de `kRobotZeroCompressSize` avant les données lues ;
  - appliquer `deDPCM16Mono` sur `packet.data` pour obtenir `int16[]` décompressé ;
  - écrire via `copyEveryOtherSample` dans la cible à la position calculée ;
  - laisser le mécanisme d'interpolation remplir les trous si le canal opposé manque.

**14) Limitations et éléments hors-fichier**
- Le rendu exact peut dépendre d'informations provenant du moteur SCI (remapping palette, conversions de résolution, planes, backgrounds). Un extracteur indépendant peut produire images et audio mais ne restituera pas toujours les mêmes effets de palette/remap que le jeu.
- Certaines décisions opérationnelles (p.ex. allocation de bitmaps, mapping vers planes) utilisent les services du moteur (SegManager, gfxFrameout) et devront être adaptées pour un outil standalone.

**15) Conclusion et livrables recommandés pour un prototype**
- Outil CLI minimal (C++ ou Python) qui :
  - valide signature et endianness ;
  - extrait palette et cue tables ;
  - construit `_recordPositions` et `_videoSizes` ;
  - pour chaque frame : décompresse les cels, crée images PNG et exporte un manifest JSON listant cels et positions ;
  - optionnel : reconstitue l'audio en WAV 16-bit 22050Hz en appliquant `deDPCM16Mono` et l'interleaving even/odd.

Fichiers rédigés / fusionnés :
- `docs/RBT_Decoder_Design_enriched.md` (ce fichier) — fusion des informations présentes dans :
  - `docs/RBT_Decoder_Design.md` (basée sur commentaires) et
  - `docs/RBT_Decoder_Design_code_based.md` (basée uniquement sur le comportement du code).

Si vous le souhaitez, je peux maintenant :
- implémenter un prototype CLI (C++ respectant les utilitaires actuels du dépôt, ou Python autonome) pour extraire frames et audio ;
- ou générer un plan d'implémentation détaillé (fichiers à créer, dépendances, tests).

Indiquez la langue de l'outil souhaitée (C++ ou Python) et j'implémente un prototype minimal qui traite un `.rbt` et exporte PNG + WAV + manifest JSON.
