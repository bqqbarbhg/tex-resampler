#define SDT_DEAD_RECKONING_IMPLEMENTATION
#include "ext/sdt_dead_reckoning.h"

#include "ext/stb_image.h"
#include "ext/stb_image_write.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

bool g_verbose;

void failf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	vfprintf(stderr, fmt, args);
	putc('\n', stderr);

	va_end(args);

	exit(1);
}

uint8_t *loadImage(const char *name, int numChannels, uint8_t missing, uint32_t resolution)
{
	uint8_t *result = (uint8_t*)malloc(numChannels * resolution * resolution);
	if (!name) {
		memset(result, (int)(uint32_t)missing, numChannels * resolution * resolution);
		return result;
	}

	int w, h;
	uint8_t *input = stbi_load(name, &w, &h, NULL, 4);
	if (!input) {
		failf("Failed to load input: %s", name);
	}
	if (w != resolution || h != resolution) {
		failf("Input file has invalid resolution %dx%d, expected %ux%u", w, h, resolution, resolution);
	}


	int *closest = (int*)malloc(resolution * resolution * sizeof(int));
	sdt_dead_reckoning(w, h, 254, input + 3, 4, NULL, closest);
	for (size_t i = 0; i < resolution * resolution; i++) {
		uint8_t *dst = result + i*numChannels;
		const uint8_t *src = input + i*4;
		uint8_t alpha = src[3];
		if (alpha < 255) {
			src = input + closest[i] * 4;
		}
		for (int ci = 0; ci < numChannels; ci++) {
			dst[ci] = src[ci];
		}
		dst[1] = src[1];
		dst[2] = src[2];
		dst[3] = src[3];
	}

	free(closest);

	return result;
}

int main(int argc, char **argv)
{
	bool showHelp = false;
	const char *outputFile = nullptr;
	const char *albedoFile = nullptr;
	const char *aoFile = nullptr;
	const char *roughnessFile = nullptr;
	const char *metallicFile = nullptr;

	// -- Parse arguments

	for (int argi = 1; argi < argc; argi++) {
		const char *arg = argv[argi];
		int left = argc - argi - 1;

		if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
			g_verbose = true;
		} else if (!strcmp(arg, "--help")) {
			showHelp = true;
		} else if (left >= 1) {
			if (!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
				outputFile = argv[++argi];
			} else if (!strcmp(arg, "--albedo")) {
				albedoFile = argv[++argi];
			} else if (!strcmp(arg, "--ao")) {
				aoFile = argv[++argi];
			} else if (!strcmp(arg, "--roughness")) {
				roughnessFile = argv[++argi];
			} else if (!strcmp(arg, "--metallic")) {
				metallicFile = argv[++argi];
			}
		}
	}

	if (showHelp) {
		printf("%s",
			"Usage: tex-resampler -i <input> -o <output> -s <source-mesh> -d <destination-mesh>\n"
		);

		return 0;
	}

	uint32_t resolution = 256;
	uint8_t *albedoData = loadImage(albedoFile, 3, 0xff, resolution);
	uint8_t *aoData = loadImage(aoFile, 1, 0xff, resolution);
	uint8_t *roughnessData = loadImage(roughnessFile, 1, 0xaa, resolution);
	uint8_t *metallicData = loadImage(metallicFile, 1, 0x00, resolution);

	uint8_t *resultData = (uint8_t*)malloc(resolution * resolution * 4);
	for (uint32_t i = 0; i < resolution*resolution; i++) {
		uint8_t *result = resultData + i * 4;
		uint8_t *albedo = albedoData + i * 3;
		uint8_t *ao = aoData + i;
		uint8_t *roughness = roughnessData + i;
		uint8_t *metallic = metallicData + i;

		uint32_t factor = ((uint32_t)ao[0] * (255u - (uint32_t)metallic[0])) / 255u;
		result[0] = (uint8_t)((uint32_t)albedo[0] * factor / 255);
		result[1] = (uint8_t)((uint32_t)albedo[1] * factor / 255);
		result[2] = (uint8_t)((uint32_t)albedo[2] * factor / 255);
		result[3] = roughness[0];
	}

	stbi_write_png(outputFile, (int)resolution, (int)resolution, 4, resultData, NULL);

	return 0;
}
