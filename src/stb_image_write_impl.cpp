//
// Implémentation unique de stb_image_write
// - Fournit les symboles pour l'API déclarée dans include/stb_image_write.h
// - Active le support UTF-8 sous Windows pour gérer correctement les chemins
//   Unicode lorsque des PNG sont écrits.
//
// Aucun autre code ne doit définir STB_IMAGE_WRITE_IMPLEMENTATION.
//

#define STB_IMAGE_WRITE_IMPLEMENTATION

// Correction pour les chemins Unicode sous Windows
#ifdef _WIN32
#define STBIW_WINDOWS_UTF8
#endif


#include "stb_image_write.h"
