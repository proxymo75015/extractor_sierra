#define STB_IMAGE_WRITE_IMPLEMENTATION

// Correction pour les chemins Unicode sous Windows
#ifdef _WIN32
#define STBIW_WINDOWS_UTF8
#endif

#include "stb_image_write.h"