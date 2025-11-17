RBT — Description du décodage (basée uniquement sur le code de `robot.cpp` et `robot.h`)

But

Fournir une spécification technique, directement exploitable pour implémenter un décodeur/extracteur de fichiers `.rbt` en se fondant uniquement sur le code (lectures, constantes, appels de fonctions), sans utiliser les commentaires littéraux du dépôt.

Remarques générales (extraits du code)

- Constantes visibles dans le code :
  - `kRobotSampleRate = 22050` (sample rate final utilisé par le flux audio).
  - `kEOSExpansion = 2` (les paquets audio écrivent dans chaque "autre" échantillon du buffer final).
  - `kRobotFrameSize = 2048` (alignement secteur utilisé pour positionner le premier bloc de trame).
  - `kRobotZeroCompressSize = 2048` (taille utilisée pour remplir quand l'audio est "zero-compress").
  - `kAudioBlockHeaderSize = 8` (taille en octets de l'en-tête audio en flux).
  - `kCelHeaderSize = 22` (taille en octets de l'en-tête d'un cel).
  - Compression vidéo gérée : `kCompressionLZS = 0` et `kCompressionNone = 2`.

1) Ouverture / détermination d'endianness

- `initStream(robotId)` :
  - Ouverture d'un flux via `SearchMan.createReadStreamForMember(fileName)`.
  - Lecture initiale `id = stream->readUint16LE()` et contrôle `id == 0x16` (validation de signature initiale).
  - Seek vers offset 6 puis lecture `version = stream->readUint16BE()` (le code lit la version de cette façon pour déterminer l'endianness).
  - Ensuite on enveloppe le flux avec `SeekableReadStreamEndianWrapper(stream, bigEndian, DisposeAfterUse::YES)` et on positionne `_stream->seek(2, SEEK_SET)`.
  - Validation : `_stream->readUint32BE()` doit être égal à `MKTAG('S','O','L',0)` sinon erreur.

2) Lecture de l'en-tête principal (séquence de lectures observée dans `open`)

Après l'initialisation du flux adapté à l'endianness, le code lit les champs dans cet ordre (les opérateurs utilisés sont ceux du stream `_stream`):

- `_version = _stream->readUint16();`
- `_audioBlockSize = _stream->readUint16();`
- `_primerZeroCompressFlag = _stream->readSint16();`
- `_stream->seek(2, SEEK_CUR);` (deux octets ignorés)
- `_numFramesTotal = _stream->readUint16();`
- `const uint16 paletteSize = _stream->readUint16();`
- `_primerReservedSize = _stream->readUint16();`
- `_xResolution = _stream->readSint16();`
- `_yResolution = _stream->readSint16();`
- `const bool hasPalette = (bool)_stream->readByte();`
- `_hasAudio = (bool)_stream->readByte();`
- `_stream->seek(2, SEEK_CUR);` (2 octets ignorés)
- `_frameRate = _normalFrameRate = _stream->readSint16();`
- `_isHiRes = (bool)_stream->readSint16();`
- `_maxSkippablePackets = _stream->readSint16();`
- `_maxCelsPerFrame = _stream->readSint16();`
- `_maxCelArea.push_back(_stream->readSint32());` (4 fois pour 4 valeurs)
- `_stream->seek(8, SEEK_CUR);` (8 octets réservés ignorés)

Conséquence pratique : implémentez la lecture de l'en-tête dans exactement cet ordre et avec les mêmes largeurs de champs (16-bit signed/unsigned, 32-bit signés) — l'ordre et le type sont dictés par les appels.

3) Primer audio (lecture conditionnelle observée dans `initAudio` et `readPrimerData`)

Si `_hasAudio` est vrai :
- Si `_primerReservedSize != 0` :
  - `primerHeaderPosition = _stream->pos();`
  - `_totalPrimerSize = _stream->readSint32();`
  - `compressionType = _stream->readSint16();`
  - `_evenPrimerSize = _stream->readSint32();`
  - `_oddPrimerSize = _stream->readSint32();`
  - `_primerPosition = _stream->pos();`
  - Si `compressionType != 0` -> erreur (le code appelle `error("Unknown audio header compression type %d", compressionType);`).
  - Si `_evenPrimerSize + _oddPrimerSize != _primerReservedSize` alors le code fait `_stream->seek(primerHeaderPosition + _primerReservedSize, SEEK_SET);` (on saute à l'emplacement aligné attendu).
- Sinon (si `_primerReservedSize == 0` et `_primerZeroCompressFlag`), le code affecte des valeurs implicites : `_evenPrimerSize = 19922; _oddPrimerSize = 21024;`.

La fonction `readPrimerData(outEvenBuffer, outOddBuffer)` :
- Si `_primerReservedSize != 0` et `_totalPrimerSize != 0` : seek `_primerPosition` puis lire `_evenPrimerSize` octets dans `outEvenBuffer` puis `_oddPrimerSize` octets dans `outOddBuffer`.
- Sinon si `_primerReservedSize == 0` et `_primerZeroCompressFlag` vrai : remplir `outEvenBuffer`/`outOddBuffer` par zéros (memset).
- Autrement appeler `error("ReadPrimerData - Flags corrupt");`.

4) Palette

- Après les éventuels primers, le code test `if (hasPalette) _stream->read(_rawPalette, paletteSize); else _stream->seek(paletteSize, SEEK_CUR);`.
- `rawPalette` est copié ensuite dans le hunk palette des bitmaps si `usePalette` vaut vrai lors du rendu de cels (voir createCel5).

5) Indexs de tailles et positions des enregistrements (`initRecordAndCuePositions`)

- Selon `_version` :
  - si `_version == 5` :
    - pour i in 0.._numFramesTotal-1 : `_videoSizes.push_back(_stream->readUint16());`
    - pour i in 0.._numFramesTotal-1 : `recordSizes.push_back(_stream->readUint16());`
  - si `_version == 6` :
    - similar avec `_stream->readSint32()` (32-bit)
- Ensuite :
  - pour i in 0..kCueListSize-1 : `_cueTimes[i] = _stream->readSint32();`
  - pour i in 0..kCueListSize-1 : `_cueValues[i] = _stream->readUint16();`
- Puis alignement :
  - `bytesRemaining = (_stream->pos() - _fileOffset) % kRobotFrameSize;` si `bytesRemaining != 0` then `_stream->seek(kRobotFrameSize - bytesRemaining, SEEK_CUR);`.
- Construction de la table `_recordPositions` :
  - `position = _stream->pos(); _recordPositions.push_back(position);` puis cumul des `recordSizes[i]` pour ajouter les positions des trames suivantes.

6) Lecture et décodage d'une frame (flux observé dans `doVersion5` / `createCels5` / `createCel5`)

- Lecture :
  - `videoSize = _videoSizes[_currentFrameNo];`
  - `_doVersion5Scratch.resize(videoSize);`
  - `_stream->read(videoFrameData, videoSize);`
  - `screenItemCount = READ_SCI11ENDIAN_UINT16(videoFrameData);` (macro utilisée pour obtenir la valeur en endian SCI11 spécifique)
  - Appel `createCels5(videoFrameData + 2, screenItemCount, usePalette)`.

- `createCels5` :
  - `preallocateCelMemory(rawVideoData, numCels);` puis boucle `for i in 0..numCels-1` `rawVideoData += createCel5(rawVideoData, i, usePalette);`.

- `createCel5` (lecture stricte des champs depuis `rawVideoData`) :
  - `_verticalScaleFactor = rawVideoData[1];`
  - `celWidth = READ_SCI11ENDIAN_UINT16(rawVideoData + 2);`
  - `celHeight = READ_SCI11ENDIAN_UINT16(rawVideoData + 4);`
  - `celPosition.x = READ_SCI11ENDIAN_UINT16(rawVideoData + 10);`
  - `celPosition.y = READ_SCI11ENDIAN_UINT16(rawVideoData + 12);`
  - `dataSize = READ_SCI11ENDIAN_UINT16(rawVideoData + 14);`
  - `numDataChunks = READ_SCI11ENDIAN_UINT16(rawVideoData + 16);`
  - `rawVideoData += kCelHeaderSize;`

  - Pour chaque chunk i in 0..numDataChunks-1 :
    - `compressedSize = READ_SCI11ENDIAN_UINT32(rawVideoData);`
    - `decompressedSize = READ_SCI11ENDIAN_UINT32(rawVideoData + 4);`
    - `compressionType = READ_SCI11ENDIAN_UINT16(rawVideoData + 8);`
    - `rawVideoData += 10;`
    - Si `compressionType == kCompressionLZS` :
      - `MemoryReadStream videoDataStream(rawVideoData, compressedSize, DisposeAfterUse::NO);`
      - `_decompressor.unpack(&videoDataStream, targetBuffer, compressedSize, decompressedSize);`
    - Sinon si `compressionType == kCompressionNone` :
      - `Common::copy(rawVideoData, rawVideoData + decompressedSize, targetBuffer);`
    - `rawVideoData += compressedSize; targetBuffer += decompressedSize;`

  - Si `_verticalScaleFactor != 100` :
    - `expandCel(bitmap.getPixels(), _celDecompressionBuffer.begin(), celWidth, celHeight);` qui adapte verticalement le bitmap.
  - Si `usePalette` vrai :
    - `Common::copy(_rawPalette, _rawPalette + kRawPaletteSize, bitmap.getHunkPalette());`
  - Retourne `kCelHeaderSize + dataSize`.

Remarques d'implémentation :
- Le flux vidéo est un agencement de cels (header + chunks). Chaque chunk transporte `compressedSize` octets suivis d'une taille effective `decompressedSize` et d'un `compressionType`.
- Le seul décompresseur appelé est `_decompressor.unpack(...)` pour LZS ; sinon les bytes sont copiés en clair.
- Le code place le résultat des chunks consécutifs dans `targetBuffer` (concaténation pour construire l'image du cel).
- L'expansion verticale se fait après concaténation dans un buffer intermediaire si nécessaire.

7) Audio — lecture des enregistrements et format (observé dans `readAudioDataFromRecord`, `AudioList`, `RobotAudioStream`)

- `readAudioDataFromRecord(frameNo, outBuffer, outAudioPosition, outAudioSize)` :
  - `_stream->seek(_recordPositions[frameNo] + _videoSizes[frameNo], SEEK_SET);` (l'audio suit immédiatement la vidéo pour cette trame)
  - `position = _stream->readSint32();` (position compressée/absolue de l'audio)
  - `size = _stream->readSint32();` (taille du bloc audio, hors header)
  - Si `position == 0` -> retourne false (pas d'audio pour cette trame)
  - Si `size != _expectedAudioBlockSize` :
    - `memset(outBuffer, 0, kRobotZeroCompressSize);`
    - `_stream->read(outBuffer + kRobotZeroCompressSize, size);`
    - `size += kRobotZeroCompressSize;`
  - Sinon : `_stream->read(outBuffer, size);`
  - `outAudioPosition = position; outAudioSize = size; return !_stream->err();`

- `AudioList::AudioBlock::submit(startOffset)` : crée `RobotAudioStream::RobotAudioPacket packet(_data, _size, (_position - startOffset) * 2);` puis appelle `g_sci->_audio32->playRobotAudio(packet);` et renvoie son booléen de succès.
  - Noter la multiplication par 2 dans la position envoyée : `(position - startOffset) * 2`.

- `RobotAudioStream` (implémentation) :
  - Constructeur alloue `_loopBuffer` de taille `bufferSize` et initialisations diverses.
  - `addPacket(const RobotAudioPacket &packet)` :
    - `const int8 bufferIndex = packet.position % 4 ? 1 : 0;` (décision pair/impair utilisée pour écrire dans le canal "even" ou "odd").
    - Si `packet.position <= 2 && _firstPacketPosition == -1` : initialisation/primer handling (positions, jointMin, flags), puis `fillRobotBuffer(packet, bufferIndex);` et return true.
    - Calcule `packetEndByte = packet.position + (packet.dataSize * (sizeof(int16) + kEOSExpansion));`
    - Si `packetEndByte <= MAX(_readHeadAbs, _jointMin[bufferIndex])` -> packet rejeté (déjà lu/ou dépassé)
    - Si `_maxWriteAbs <= _jointMin[bufferIndex]` -> buffer plein -> return false (demander renvoi plus tard)
    - Appel `fillRobotBuffer(packet, bufferIndex);`
    - Si `_firstPacketPosition != -1 && _firstPacketPosition != packet.position` -> fin du waiting flag.
    - Si `packetEndByte > _maxWriteAbs` -> partial read -> return false
    - Sinon return true

  - `fillRobotBuffer(const RobotAudioPacket &packet, const int8 bufferIndex)` :
    - `decompressedSize = packet.dataSize * sizeof(int16);`
    - Si `_decompressionBufferPosition != packet.position` :
      - (re)alloue `_decompressionBuffer` à `decompressedSize` si nécessaire
      - `deDPCM16Mono((int16*)_decompressionBuffer, packet.data, packet.dataSize, carry);` (appel à une fonction de DPCM qui produit le flux PCM 16-bit dans `_decompressionBuffer`)
      - `_decompressionBufferPosition = packet.position;`
    - Calcule nombres d'octets/positions, bornes `startByte/endByte/maxWriteByte` etc. (logique de calcul d'intervalle pour écrire dans `_loopBuffer` en respectant jointMin / wrap)
    - Utilise `copyEveryOtherSample((int16*)(_loopBuffer + targetBytePosition), (int16*)(_decompressionBuffer + sourceByte), n)` pour écrire chaque échantillon dans chaque "autre" position du buffer cible (dû à `kEOSExpansion==2`).
    - Utilise `interpolateChannel` pour interpoler (moyenne) des valeurs lorsqu'un canal manque d'écriture pour certaines zones.
    - Met à jour `_jointMin[bufferIndex]` et `_writeHeadAbs`.

  - `interpolateMissingSamples(numSamples)` : essaie de remplir les portions non écrites quand audio est demandé mais données manquantes par interpolation ou mise à zéro si nécessaire; met à jour `_jointMin`.

  - `readBuffer(outBuffer, numSamples)` :
    - Si `_waiting` return 0
    - Calcule `maxNumSamples` disponibles=( _writeHeadAbs - _readHeadAbs)/sizeof(sample)
    - `numSamples = MIN(numSamples, maxNumSamples)`
    - `interpolateMissingSamples(numSamples)` puis copie depuis `_loopBuffer` en gérant l'edge wrap, met à jour `_readHead`, `_readHeadAbs`, `_maxWriteAbs` et retourne `numSamples` lus.

Synthèse comportementale pour l'audio (algorithme à implémenter)

- Les paquets audio sont DPCM 16-bit mono compressés (fonction `deDPCM16Mono` inverse la compression en un buffer de `int16`).
- Chaque paquet a une `position` qui détermine sa place dans le flux audio final ; `bufferIndex` = 0 (even) ou 1 (odd) est calculé depuis `packet.position % 4`.
- Après décompression (dans `_decompressionBuffer`), on écrit les échantillons valides (hors runway) dans un buffer circulaire `_loopBuffer` en écrivant chaque échantillon dans `every other sample` (p.ex. copyEveryOtherSample) — l'autre moitié des échantillons sera fournie par le canal pair/impair opposé.
- Le code gère collisions, écritures partielles (lorsque `_loopBuffer` est plein) et rejets pour renvoi ultérieur.
- Si un canal n'a pas encore écrit pour une zone, l'autre canal pourra interpoler cette région (moyenne des voisins) au moment de la lecture audio.

8) Seeking et synchronisation AV (observé dans `doRobot`, `frameNowVisible`, `primeAudio`)

- `seekToFrame(frameNo)` -> `_stream->seek(_recordPositions[frameNo], SEEK_SET)` (positionne le flux au début du paquet de trame).
- `doRobot` appelle `seekToFrame(_currentFrameNo)` puis `doVersion5()` pour décoder la frame ; si `_hasAudio` il soumet aussi des blocs audio via `_audioList`.
- `primeAudio(startTick)` : selon `startTick` construit la queue audio initiale :
  - Si `startTick == 0` : lire primer even/odd et les `addBlock(0/1, size, buffer)`.
  - Sinon : calculer `audioStartPosition` et, si nécessaire, lire primer et/ou lire des enregistrements partiels à partir d'un enregistrement audio antérieur (`readPartialAudioRecordAndSubmit`) puis ajouter les blocs jusqu'à `videoStartFrame`.

- `frameNowVisible` démarre l'audio en appelant `_audioList.startAudioNow()` si `_syncFrame` était vrai, puis met à jour le timestamp de synchronisation et effectue des contrôles AV périodiques.

9) Méta-données et tables disponibles (exposées par le code)

- Nombre total de frames : `_numFramesTotal`.
- Table des tailles vidéo : `_videoSizes` (taille compressée en bytes pour chaque frame).
- Table des positions des enregistrements (offsets) : `_recordPositions` (byte offsets au sein du flux pour chaque frame).
- Table `_cueTimes` (32-bit per cue) et `_cueValues` (16-bit) — utilisé par `getCue()` pour renvoyer une valeur au moteur de jeu.
- Palette brute si présente : `_rawPalette` (copiée ensuite dans le hunk palette des bitmaps lors du rendu si `usePalette`).

10) Contraintes observées dans le code (à respecter dans une implémentation)

- Les champs sont lus dans un ordre précis et avec des largeurs précises (16/32 bits signés/unsiged) : respecter exactement l'ordre.
- L'alignement à `kRobotFrameSize` est appliqué avant le premier bloc de trame ; ne pas oublier cette padding_seek.
- Le décompresseur LZS est appelé via `_decompressor.unpack(...)` ; le code attend que le décompressseur produise exactement `decompressedSize` octets.
- Lors de la lecture d'un bloc audio, si `size != _expectedAudioBlockSize`, la portion initiale de `kRobotZeroCompressSize` octets doit être considérée comme remplie de zéros et la donnée lue placée après.
- Position audio et sa conversion : l'envoi à `RobotAudioStream` multiplie par 2 la position lors de la création du `RobotAudioPacket` dans `AudioBlock::submit`.
- `RobotAudioStream::addPacket` calcule `bufferIndex` sur `packet.position % 4` pour déterminer pair/impair et primer detection `packet.position <= 2`.

11) Sortie / extraction recommandée (étapes concrètes)

À partir du code observé, pour produire un extracteur minimal :

- Ouvrir le `.rbt` en lecture binaire ; reproduire la logique `initStream` pour déterminer l'endianness et valider la signature `0x16` et `"SOL\0"`.
- Lire l'en-tête exactement comme dans `open()` pour obtenir : version, audioBlockSize, primer flags, numFramesTotal, paletteSize, primerReservedSize, x/y resolution, hasPalette, hasAudio, framerate, maxSkippable, maxCelsPerFrame, 4 * maxCelArea.
- Si `_hasAudio` : lire primer header selon la logique de `initAudio` et `readPrimerData` -> stocker even/odd primer buffers.
- Lire palette si `hasPalette`.
- Lire `videoSizes` et `recordSizes` selon `_version` (16-bit pour v5, 32-bit pour v6).
- Lire `cueTimes` (256 x 32-bit) et `cueValues` (256 x 16-bit).
- Calculer l'alignement et seek sur la frontière `kRobotFrameSize` comme fait dans `initRecordAndCuePositions`.
- Construire `_recordPositions` cumulativement en commençant par `_stream->pos()`.
- Pour chaque frame i :
  - Seek `_recordPositions[i]` et lire `videoSize = _videoSizes[i]` octets.
  - `screenItemCount = READ_SCI11ENDIAN_UINT16(buf)` puis pour chaque cel appliquer la logique `createCel5` : lire header, pour chaque chunk lire compressed/decompressed sizes et type, si LZS appeler votre décompresseur LZS, sinon copier directement, concaténer, puis si `_verticalScaleFactor != 100` appliquer expansion verticale. Sauver les bitmaps résultants (ex: PNG) et associer position (celPosition.x,y).
  - Si présence d'audio attaché à la trame (`readAudioDataFromRecord`), lire `position` et `size` et sauvegarder le bloc compressé tel quel ou le décompresser via la routine DPCM observée (utiliser la fonction inverse DPCM) puis reconstituer l'interleaving final en utilisant la technique `copyEveryOtherSample` et `interpolateChannel` observée dans `fillRobotBuffer` pour reconstruire un flux PCM intercalé 22050 Hz.

12) Points techniques précis à implémenter (liste d'actions codées dans le dépôt)

- Décompresseur LZS : appeler la routine équivalente (ou implémenter LZS) et s'assurer que `_decompressor.unpack` produit exactement `decompressedSize` octets.
- DPCM : implémenter `deDPCM16Mono(int16 *out, const byte *in, const uint32 numBytes, int16 &sample)` (ou réutiliser l'implémentation du dépôt) pour obtenir un buffer d'int16 de taille `packet.dataSize * sizeof(int16)`.
- Interleaving audio : pour écrire un bloc décompressé dans l'audio final, copier chaque échantillon du buffer source vers `every other sample` dans la cible (cf. `copyEveryOtherSample`).
- Gestion du loop buffer audio : reproduire la logique de `fillRobotBuffer` pour respecter `_readHeadAbs`, `_writeHeadAbs`, `_jointMin[2]`, wrap-around, rejets si buffer plein, et interpolation quand nécessaire.
- Conversion des positions : noter que `AudioBlock::submit` envoie `(_position - startOffset) * 2` comme position dans le packet transmis au flux audio.

13) Métadonnées exploitables

- `numFramesTotal` : nombre de frames dans la ressource.
- `frameRate` : ` _frameRate` lu depuis l'en-tête.
- `cueTimes`/`cueValues` : tables de cues consultables.
- `paletteSize` et palette brute `_rawPalette` stockée.
- `videoSizes` et `recordPositions` : permettent de seek/arbitrer l'extraction frame par frame.

14) Résumé rapide (implémentation minimale pour extraction)

- Lire en-tête (ordre et types exactement comme le code).
- Lire palette et indexs.
- Aligner au secteur 2048.
- Pour chaque frame : lire `videoSize`, décoder cels (chunks LZS ou raw), appliquer expansion verticale si nécessaire, sauvegarder bitmaps et positions.
- En option : lire bloc audio associé (position,size) et reconstruire PCM 16-bit 22050Hz en appliquant DPCM inverse puis interleaving even/odd selon `packet.position` et `kEOSExpansion`.

Fin

Le document ci-dessus est construit uniquement à partir d'appels de fonctions, constantes et séquences de lecture présents dans `robot.cpp` et `robot.h`. Il évite d'utiliser les textes de commentaires présents dans les fichiers source et décrit les opérations effectives que le code réalise.
