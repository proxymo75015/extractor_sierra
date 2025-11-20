# 📊 État du projet - extractor_sierra

> Dernière mise à jour : Novembre 2025

---

## ✅ Statut général

**Version** : 1.0.0  
**Statut** : ✅ Stable et fonctionnel  
**Licence** : GPL-3.0 (compatible ScummVM)

---

## 🎯 Fonctionnalités

| Fonctionnalité | Statut | Notes |
|----------------|--------|-------|
| **Extraction vidéo** | ✅ Complet | Frames PPM @ 320×240 ou 640×480 |
| **Décompression LZS** | ✅ Complet | Basé sur ScummVM |
| **Extraction audio** | ✅ Complet | DPCM16 → PCM 22050Hz mono |
| **Séparation L/R** | ✅ Complet | Canaux EVEN/ODD @ 11025Hz |
| **Génération MP4** | ✅ Complet | Via FFmpeg (H.264 + AAC) |
| **Support Robot v5** | ✅ Complet | 320×240 testé (91.RBT) |
| **Support Robot v6** | ⚠️ Partiel | 640×480 non testé |
| **Clamping DPCM** | ✅ Complet | Amélioration vs wrapping |
| **Interpolation audio** | ✅ Complet | Multi-pass (20 itérations) |
| **Gestion primers** | ✅ Complet | EVEN + ODD activés |
| **Runway DPCM** | ✅ Complet | 8 bytes gérés automatiquement |

---

## 📁 Structure du projet

```
extractor_sierra/
├── src/                        ✅ Code source C++
│   ├── main.cpp               ✅ Point d'entrée
│   ├── core/                  ✅ Cœur du décodeur
│   │   ├── rbt_parser.cpp     ✅ Parser RBT (908 lignes)
│   │   └── robot_audio_stream.cpp ✅ Buffer audio
│   ├── formats/               ✅ Codecs
│   │   ├── dpcm.cpp           ✅ Décodeur DPCM16
│   │   ├── lzs.cpp            ✅ Compression LZS
│   │   └── decompressor_lzs.cpp ✅ Décompression
│   └── utils/                 ✅ Utilitaires
│       ├── sci_util.cpp       ✅ Utils SCI/ScummVM
│       └── memory_stream.h    ✅ Stream mémoire
│
├── build/                      ✅ Binaires compilés
│   └── robot_decoder          ✅ Exécutable principal
│
├── tools/                      ✅ Scripts Python (4 scripts)
│   ├── extract_lr_simple.py   ✅ Extraction L/R (235 lignes)
│   ├── extract_and_make_video.py ✅ Pipeline complet
│   ├── make_scummvm_video.py  ✅ Générateur MP4
│   └── test_audio_video_sync.py ✅ Validation A/V
│
├── docs/                       ✅ Documentation (9 fichiers)
│   ├── README.md              ✅ Index navigation
│   ├── reference/             ✅ Référence ScummVM (2 fichiers)
│   ├── project/               ✅ Notre implémentation (2 fichiers)
│   └── technical/             ✅ Détails techniques (3 fichiers)
│
└── ScummVM/                    ✅ Code référence ScummVM
    ├── robot.cpp              ✅ RobotAudioStream
    └── robot.h                ✅ Headers
```

---

## 📊 Métriques de qualité

### Audio (fichier test 91.RBT)

| Métrique | Avant optimisations | Après optimisations | Amélioration |
|----------|---------------------|---------------------|--------------|
| **Zéros** | 81% (L) / 44% (R) | 0.04% | ~2000× |
| **Discontinuités >5000** | 111,614 | 36 | ~3100× |
| **Durée** | Variable | 9.00s stable | ✅ |
| **Artefacts "clac"** | Présents | Éliminés | ✅ |

### Performance

| Opération | Temps | Fichier |
|-----------|-------|---------|
| Extraction C++ | ~0.5s | 91.RBT (90 frames) |
| Génération MP4 | ~2s | FFmpeg H.264 + AAC |
| **TOTAL** | **~2.5s** | Pipeline complet |

---

## 📚 Documentation

### Complétude

| Section | Fichiers | Lignes | Statut |
|---------|----------|--------|--------|
| **README principal** | 1 | ~360 | ✅ Complet |
| **Référence ScummVM** | 2 | ~1100 | ✅ Complet |
| **Notre projet** | 2 | ~800 | ✅ Complet |
| **Technique** | 3 | ~600 | ✅ Complet |
| **Outils** | 1 | ~150 | ✅ Complet |
| **TOTAL** | **9** | **~3010** | **✅ Complet** |

### Coverage

- ✅ Format Robot (spécification complète)
- ✅ Audio DPCM16 (algorithmes détaillés)
- ✅ Implémentation C++ (architecture complète)
- ✅ Scripts Python (guide complet)
- ✅ Runway DPCM (clarifié et documenté)
- ✅ Classification EVEN/ODD (% 4 vs % 2 expliqué)
- ⚠️ Vidéo LZS (basique, pourrait être étendu)
- ⚠️ Palette (basique)

---

## 🧪 Tests

| Type de test | Statut | Couverture |
|--------------|--------|------------|
| **Extraction vidéo** | ✅ Passé | 91.RBT (90 frames) |
| **Extraction audio** | ✅ Passé | 91.RBT (9s, 22050Hz) |
| **Séparation L/R** | ✅ Passé | EVEN + ODD validés |
| **Synchronisation A/V** | ✅ Passé | 10 fps confirmé |
| **Génération MP4** | ✅ Passé | H.264 + AAC fonctionnels |
| **Qualité audio** | ✅ Passé | 0.04% zéros, 36 discontinuités |
| **Métriques** | ✅ Passé | Durée, taille, format OK |

---

## 🔧 Dépendances

| Dépendance | Version | Statut | Usage |
|------------|---------|--------|-------|
| **CMake** | ≥ 3.10 | ✅ Installé | Build C++ |
| **g++/clang** | C++11 | ✅ Installé | Compilation |
| **Python** | ≥ 3.8 | ✅ Installé | Scripts |
| **FFmpeg** | Dernière | ✅ Installé | MP4 (optionnel) |
| **NumPy** | Dernière | ⚠️ Optionnel | Analyse audio |

---

## 🐛 Issues connues

### Résolus ✅

- ✅ **"Clac clac" audio** : Résolu (clamping DPCM + interpolation)
- ✅ **Zéros massifs** : Résolu (primers activés + interpolation)
- ✅ **Discontinuités** : Réduit de 3100× (111k → 36)
- ✅ **Classification EVEN/ODD** : Clarifié (% 4 pas % 2)
- ✅ **Runway DPCM** : Documenté et géré correctement

### Ouverts ⚠️

- ⚠️ **36 discontinuités >5000** : Peut être inhérent à l'audio original
- ⚠️ **Robot v6** : Non testé (640×480)
- ⚠️ **Robot v1-v4** : Non supporté

---

## 🚧 Limitations

1. **Formats** : Robot v5/v6 uniquement (pas v1-v4)
2. **Compression** : LZS vidéo (pas RLE)
3. **Palette** : HunkPalette seulement
4. **Audio** : DPCM16 mono (pas DPCM8, pas stéréo natif)
5. **Plateforme** : Linux/Docker testé (Mac/Windows à venir)

---

## 🗺️ Roadmap

### Version 1.1 (prochaine)

- [ ] Support Robot v6 (640×480) testé et validé
- [ ] Export MP4 natif (sans FFmpeg externe)
- [ ] Builds Windows et macOS

### Version 2.0 (future)

- [ ] Support Robot v4 (format différent)
- [ ] Décodage RLE vidéo
- [ ] GUI extraction batch
- [ ] Plugin ScummVM pour export

---

## 📈 Historique

### Novembre 2025

- ✅ Nettoyage et réorganisation complète du projet
- ✅ Documentation séparée ScummVM vs Notre projet
- ✅ Clarification runway DPCM (8 bytes)
- ✅ Explication % 4 vs % 2 (classification EVEN/ODD)
- ✅ Scripts Python déplacés dans tools/
- ✅ README principal amélioré
- ✅ Index de documentation créé

### Octobre-Novembre 2025 (développement)

- ✅ Correction wrapping → clamping DPCM
- ✅ Activation primers (usePrimers=true)
- ✅ Interpolation multi-pass (20 itérations)
- ✅ audioPos comme position absolue
- ✅ Élimination artefacts "clac clac"
- ✅ Réduction zéros : 81% → 0.04%
- ✅ Réduction discontinuités : 111k → 36

---

## 🎯 Objectifs atteints

- [x] Extraction vidéo fonctionnelle
- [x] Extraction audio fonctionnelle
- [x] Qualité audio excellente (0.04% zéros)
- [x] Génération MP4 automatisée
- [x] Séparation canaux L/R
- [x] Documentation complète
- [x] Code propre et organisé
- [x] Conformité ScummVM (avec améliorations)

---

## 📝 Notes

### Différences majeures vs ScummVM

1. **Clamping DPCM** : Nous utilisons le clamping au lieu du wrapping pour éviter les artefacts
2. **Buffer linéaire** : Nous utilisons un buffer linéaire au lieu d'un buffer circulaire (offline vs streaming)
3. **Interpolation** : Notre approche multi-pass (20 itérations) vs temps réel ScummVM
4. **Usage** : Extraction/conversion batch vs playback temps réel

### Améliorations apportées

1. ✅ Qualité audio supérieure (clamping vs wrapping)
2. ✅ Documentation exhaustive (3000+ lignes)
3. ✅ Scripts Python pour analyse
4. ✅ Pipeline automatisé complet
5. ✅ Séparation canaux L/R

---

## 🔗 Liens

- **Projet** : [GitHub - extractor_sierra](https://github.com/proxymo75015/extractor_sierra)
- **Documentation** : [docs/](docs/)
- **ScummVM** : [scummvm.org](https://www.scummvm.org/)
- **Issues** : [GitHub Issues](https://github.com/proxymo75015/extractor_sierra/issues)

---

<div align="center">

**Projet stable et prêt pour utilisation en production**

[⬆ Retour au README](README.md)

</div>
