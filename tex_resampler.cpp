#include "ext/ufbx.h"
#include "ext/rtk.h"
#include "ext/stb_image.h"
#include "ext/stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>

#include "sf/Base.h"
#include "sf/Array.h"
#include "sf/Vector.h"
#include "sf/HashMap.h"

bool g_verbose;

sf_inline float unormToFloat(uint8_t v)
{
	return (float)v * (1.0f / 255.0f);
}

sf_inline uint8_t floatToUnorm(float v)
{
	return (uint8_t)(v * 255.5f);
}

float linearToSrgb(float x)
{
	x = sf::clamp(x, 0.0f, 1.0f);
	if (x <= 0.0031308f)
		return 12.92f * x;
	else
		return 1.055f*powf(x, (1.0f / 2.4f)) - 0.055f;
}

sf::Vec3 linearToSrgb(const sf::Vec3 &v)
{
	return sf::Vec3(linearToSrgb(v.x), linearToSrgb(v.y), linearToSrgb(v.z));
}

float srgbToLinear(float x)
{
	x = sf::clamp(x, 0.0f, 1.0f);
	if (x <= 0.04045f)
		return 0.0773993808f * x;
	else
		return powf(x*0.947867298578f+0.0521327014218f, 2.4f);
}

sf::Vec3 srgbToLinear(const sf::Vec3 &v)
{
	return sf::Vec3(srgbToLinear(v.x), srgbToLinear(v.y), srgbToLinear(v.z));
}

uint8_t linearToSrgbUnorm(float v)
{
	return floatToUnorm(linearToSrgb(v));
}

void linearToSrgbUnorm(uint8_t dst[3], const sf::Vec3 &v)
{
	dst[0] = floatToUnorm(linearToSrgb(v.x));
	dst[1] = floatToUnorm(linearToSrgb(v.y));
	dst[2] = floatToUnorm(linearToSrgb(v.z));
}

float srgbToLinearUnorm(uint8_t v)
{
	return srgbToLinear(unormToFloat(v));
}

sf::Vec3 srgbToLinearUnorm(const uint8_t v[3])
{
	return sf::Vec3(
		srgbToLinear(unormToFloat(v[0])),
		srgbToLinear(unormToFloat(v[1])),
		srgbToLinear(unormToFloat(v[2])));
}

sf::Vec3 srgbToLinearHex(uint32_t hex)
{
	return sf::Vec3(
		srgbToLinear(unormToFloat((hex >> 16) & 0xff)),
		srgbToLinear(unormToFloat((hex >> 8) & 0xff)),
		srgbToLinear(unormToFloat(hex & 0xff)));
}


struct Vertex
{
	sf::Vec3 pos;
	sf::Vec3 normal;
	sf::Vec2 uv;

	bool operator==(const Vertex &rhs) const { return !memcmp(this, &rhs, sizeof(Vertex)); }
};

uint32_t hash(const Vertex &v) { return sf::hashBuffer(&v, sizeof(v)); }

struct Model
{
	sf::Array<sf::Vec3> pos;
	sf::Array<sf::Vec3> normals;
	sf::Array<sf::Vec2> uv;
	sf::Array<uint32_t> indices;
};

sf::Vec2 fromUFBX(const ufbx_vec2 &v) { return { (float)v.x, (float)v.y }; }
sf::Vec3 fromUFBX(const ufbx_vec3 &v) { return { (float)v.x, (float)v.y, (float)v.z }; }
sf::Vec2 fromRTK(const rtk_vec2 &v) { return { (float)v.x, (float)v.y }; }
sf::Vec3 fromRTK(const rtk_vec3 &v) { return { (float)v.x, (float)v.y, (float)v.z }; }
rtk_vec2 toRTK(const sf::Vec2 &v) { return { (float)v.x, (float)v.y }; }
rtk_vec3 toRTK(const sf::Vec3 &v) { return { (float)v.x, (float)v.y, (float)v.z }; }

Model loadModel(const char *path)
{
	ufbx_scene *scene = ufbx_load_file(path, NULL, NULL);
	sf_assert(scene);

	Model model;

	sf::HashMap<Vertex, uint32_t> vertexMap;
	sf::SmallArray<uint32_t, 64> tris;
	sf::SmallArray<uint32_t, 64> faceIndices;

	for (ufbx_mesh &mesh : scene->meshes) {
		for (uint32_t faceI = 0; faceI < mesh.num_faces; faceI++) {
			ufbx_face face = mesh.faces[faceI];
			uint32_t numTris = face.num_indices - 2;
			tris.resizeUninit(numTris * 3);
			faceIndices.resizeUninit(face.num_indices);

			ufbx_matrix normalMat = ufbx_get_normal_matrix(&mesh.node.to_root);

			for (uint32_t i = 0; i < face.num_indices; i++) {
				Vertex v;
				ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh.vertex_position, face.index_begin + i);
				ufbx_vec3 nrm = ufbx_get_vertex_vec3(&mesh.vertex_normal, face.index_begin + i);
				pos = ufbx_transform_position(&mesh.node.to_root, pos);
				nrm = ufbx_transform_direction(&normalMat, nrm);
				v.pos = fromUFBX(pos);
				v.normal = fromUFBX(nrm);
				v.uv = fromUFBX(ufbx_get_vertex_vec2(&mesh.vertex_uv, face.index_begin + i));
				auto res = vertexMap.insert(v, model.pos.size);
				faceIndices[i] = res.entry.val;
				if (res.inserted) {
					model.pos.push(v.pos);
					model.normals.push(v.normal);
					model.uv.push(v.uv);
				}
			}

			bool ok = ufbx_triangulate(tris.data, tris.size, &mesh, face);
			sf_assert(ok);

			for (uint32_t ix : tris) {
				model.indices.push(faceIndices[ix - face.index_begin]);
			}
		}
	}

	return model;
}

int main(int argc, char **argv)
{
	bool showHelp = false;
	bool flipInput = false;
	bool flipOutput = false;
	bool mergeOutput = false;
	bool noAlpha = false;
	const char *inputFile = NULL;
	const char *outputFile = NULL;
	const char *srcFile = NULL;
	const char *dstFile = NULL;
	int threads = 1;
	uint32_t outWidth = 256;
	uint32_t outHeight = 256;

	// -- Parse arguments

	for (int argi = 1; argi < argc; argi++) {
		const char *arg = argv[argi];
		int left = argc - argi - 1;

		if (!strcmp(arg, "-v") || !strcmp(arg, "--verbose")) {
			g_verbose = true;
		} else if (!strcmp(arg, "--help")) {
			showHelp = true;
		} else if (!strcmp(arg, "--flip-input")) {
			flipInput = true;
		} else if (!strcmp(arg, "--flip-output")) {
			flipOutput = true;
		} else if (!strcmp(arg, "--merge-output")) {
			mergeOutput = true;
		} else if (!strcmp(arg, "--no-alpha")) {
			noAlpha = true;
		} else if (left >= 1) {
			if (!strcmp(arg, "-i") || !strcmp(arg, "--input")) {
				inputFile = argv[++argi];
			} else if (!strcmp(arg, "-o") || !strcmp(arg, "--output")) {
				outputFile = argv[++argi];
			} else if (!strcmp(arg, "-s") || !strcmp(arg, "--source")) {
				srcFile = argv[++argi];
			} else if (!strcmp(arg, "-d") || !strcmp(arg, "--destination")) {
				dstFile = argv[++argi];
			} else if (!strcmp(arg, "-j") || !strcmp(arg, "--threads")) {
				threads = atoi(argv[++argi]);
			}
		}
	}

	if (showHelp) {
		printf("%s",
			"Usage: tex-resampler -i <input> -o <output> -s <source-mesh> -d <destination-mesh>\n"
		);

		return 0;
	}

	Model srcModel = loadModel(srcFile);
	Model dstModel = loadModel(dstFile);

	rtk_scene *srcScene = NULL;
	rtk_scene *dstScene = NULL;

	{
		rtk_mesh mesh = { };
		mesh.vertices = (rtk_vec3*)srcModel.pos.data;
		mesh.uvs = (rtk_vec2*)srcModel.uv.data;
		mesh.indices = srcModel.indices.data;
		mesh.num_triangles = srcModel.indices.size / 3;
		mesh.transform = rtk_identity;

		rtk_scene_desc desc = { };
		desc.meshes = &mesh;
		desc.num_meshes = 1;

		srcScene = rtk_create_scene(&desc);
	}

	{
		sf::Array<rtk_vec3> vertices;
		vertices.resizeUninit(dstModel.uv.size);
		rtk_vec3 *v = vertices.data;
		for (const sf::Vec2 &uv : dstModel.uv) {
			v->x = uv.x;
			v->y = uv.y;
			v->z = 0.0f;
			v++;
		}

		rtk_mesh mesh = { };
		mesh.vertices = vertices.data;
		mesh.normals = (rtk_vec3*)dstModel.normals.data;
		mesh.indices = dstModel.indices.data;
		mesh.num_triangles = dstModel.indices.size / 3;
		mesh.transform = rtk_identity;

		rtk_scene_desc desc = { };
		desc.meshes = &mesh;
		desc.num_meshes = 1;

		dstScene = rtk_create_scene(&desc);
	}

	sf::Array<sf::Vec4> inData;

	int inWidth, inHeight;
	stbi_uc *inU8 = stbi_load(inputFile, &inWidth, &inHeight, NULL, 4);
	sf_assert(inU8);
	inData.resize(inWidth * inHeight * 4);
	for (int y = 0; y < inHeight; y++)
	for (int x = 0; x < inWidth; x++) {
		stbi_uc *u8 = inU8 + (y * inWidth + x) * 4;
		sf::Vec4 *f32 = inData.data + (flipInput ? inHeight - 1 - y : y) * inWidth + x;
		*f32 = sf::Vec4(srgbToLinearUnorm(u8), unormToFloat(u8[3]));
	}
	stbi_image_free(inU8);

	sf::Array<uint8_t> outData;
	outData.resizeUninit(outWidth * outHeight * 4);

	if (mergeOutput) {
		int tmpWidth, tmpHeight;
		stbi_uc *tmpU8 = stbi_load(outputFile, &tmpWidth, &tmpHeight, NULL, 4);
		sf_assert(tmpWidth == outWidth);
		sf_assert(tmpHeight == outHeight);
		memcpy(outData.data, tmpU8, outWidth * outHeight * 4);
		stbi_image_free(tmpU8);
	}

	sf::Vec2 outRcpRes = sf::Vec2(1.0f) / sf::Vec2((float)outWidth, (float)outHeight);
	for (uint32_t y = 0; y < outHeight; y++)
	for (uint32_t x = 0; x < outWidth; x++)
	{
		sf::Vec2 uv = sf::Vec2((float)x + 0.5f, (float)y + 0.5f) * outRcpRes;
		if (flipOutput) {
			uv.y = 1.0f - uv.y;
		}

		uint8_t *pixel = outData.data + (y * outWidth + x) * 4;
		if (!mergeOutput) {
			pixel[0] = 0;
			pixel[1] = 0;
			pixel[2] = 0;
			pixel[3] = 0;
		}

		rtk_ray dstRay;
		dstRay.origin = toRTK(sf::Vec3(uv.x, uv.y, 0.0f));
		dstRay.direction = toRTK(sf::Vec3(0.0f, 0.0f, 1.0f));
		dstRay.min_t = -1.0f;
		rtk_hit dstHit;
		if (rtk_raytrace(dstScene, &dstRay, &dstHit, 2.0f)) {
			uint32_t i0 = dstHit.vertex_index[0];
			uint32_t i1 = dstHit.vertex_index[1];
			uint32_t i2 = dstHit.vertex_index[2];

			sf::Vec3 p0 = dstModel.pos[i0];
			sf::Vec3 p1 = dstModel.pos[i1];
			sf::Vec3 p2 = dstModel.pos[i2];

			float w = 1.0f - dstHit.geom.u - dstHit.geom.v;
			sf::Vec3 p = p0 * dstHit.geom.u + p1 * dstHit.geom.v + p2 * w;
			sf::Vec3 n = fromRTK(dstHit.interp.normal);

			float minDist = 200.0f;
			float maxDist = 2000.0f;

			rtk_hit bestHit;
			bestHit.t = RTK_INF;

			rtk_ray srcRay;
			rtk_hit srcHit;
			srcRay.origin = toRTK(p + n * minDist);
			srcRay.direction = toRTK(-n);
			srcRay.min_t = 0.0f;
			if (rtk_raytrace(srcScene, &srcRay, &srcHit, maxDist)) {
				bestHit = srcHit;
			}

#if 0
			srcRay.direction = toRTK(-n);
			if (rtk_raytrace(srcScene, &srcRay, &srcHit, maxDist)) {
				if (srcHit.t < bestHit.t) {
					bestHit = srcHit;
				}
			}
#endif

			if (bestHit.t < RTK_INF) {
				sf::Vec2 uv = sf::Vec2(bestHit.interp.u, bestHit.interp.v);
				float x = uv.x * inWidth;
				float y = uv.y * inHeight;
				float fx = floorf(x);
				float fy = floorf(y);
				int ix0 = ((int)fx % inWidth + inWidth) % inWidth;
				int iy0 = ((int)fy % inHeight + inHeight) % inHeight;
				int ix1 = (ix0 + 1) % inWidth;
				int iy1 = (iy0 + 1) % inHeight;
				float dx = x - fx;
				float dy = y - fy;

				sf::Vec4 *row0 = inData.data + iy0 * inWidth;
				sf::Vec4 *row1 = inData.data + iy1 * inWidth;
				sf::Vec4 v0 = sf::lerp(row0[ix0], row0[ix1], dx);
				sf::Vec4 v1 = sf::lerp(row0[ix0], row1[ix1], dx);
				sf::Vec4 v = sf::clamp(sf::lerp(v0, v1, dy), sf::Vec4(0.0f), sf::Vec4(1.0f));

				for (uint32_t i = 0; i < 4; i++) {
					sf_assert(v.v[i] >= 0.0f && v.v[i] <= 1.0f);
				}

				linearToSrgbUnorm(pixel, sf::Vec3(v.v));
				if (noAlpha) {
					pixel[3] = 0xff;
				} else {
					pixel[3] = floatToUnorm(v.w);
				}
			}
		}
	}

	stbi_write_png(outputFile, (int)outWidth, (int)outHeight, 4, outData.data, 0);

	return 0;
}
