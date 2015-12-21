/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "Precompiled.h"
#pragma hdrstop

#include "../jo_jpeg.cpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace renderer {

std::array<Vertex *, 4> ExtractQuadCorners(Vertex *vertices, const uint16_t *indices)
{
	std::array<uint16_t, 6> sorted;
	memcpy(sorted.data(), indices, sizeof(uint16_t) * sorted.size());
	std::sort(sorted.begin(), sorted.end());
	std::array<Vertex *, 4> corners;
	size_t cornerIndex = 0;

	for (size_t i = 0; i < sorted.size(); i++)
	{
		if (i == 0 || sorted[i] != sorted[i - 1])
			corners[cornerIndex++] = &vertices[sorted[i]];
	}

	assert(cornerIndex == 4); // Should be exactly 4 unique vertices.
	return corners;
}

void BgfxCallback::fatal(bgfx::Fatal::Enum _code, const char* _str)
{
	if (bgfx::Fatal::DebugCheck == _code)
	{
		bx::debugBreak();
	}
	else
	{
		ri.Error(ERR_FATAL, "0x%08x: %s", _code, _str);
	}
}

void BgfxCallback::traceVargs(const char* _filePath, uint16_t _line, const char* _format, va_list _argList)
{
	char temp[2048];
	char* out = temp;
	int32_t len   = bx::snprintf(out, sizeof(temp), "%s (%d): ", _filePath, _line);
	int32_t total = len + bx::vsnprintf(out + len, sizeof(temp)-len, _format, _argList);
	if ( (int32_t)sizeof(temp) < total)
	{
		out = (char*)alloca(total+1);
		memcpy(out, temp, len);
		bx::vsnprintf(out + len, total-len, _format, _argList);
	}
	out[total] = '\0';
	bx::debugOutput(out);
}

uint32_t BgfxCallback::cacheReadSize(uint64_t _id)
{
	return 0;
}

bool BgfxCallback::cacheRead(uint64_t _id, void* _data, uint32_t _size)
{
	return false;
}

void BgfxCallback::cacheWrite(uint64_t _id, const void* _data, uint32_t _size)
{
}

struct ImageWriteBuffer
{
	std::vector<uint8_t> *data;
	size_t bytesWritten;
};

static void ImageWriteCallback(void *context, void *data, int size)
{
	auto buffer = (ImageWriteBuffer *)context;

	if (buffer->data->size() < buffer->bytesWritten + size)
	{
		buffer->data->resize(buffer->bytesWritten + size);
	}

	memcpy(&buffer->data->data()[buffer->bytesWritten], data, size);
	buffer->bytesWritten += size;
}

static void ImageWriteCallbackConst(void *context, const void *data, int size)
{
	ImageWriteCallback(context, (void *)data, size);
}

void BgfxCallback::screenShot(const char* _filePath, uint32_t _width, uint32_t _height, uint32_t _pitch, const void* _data, uint32_t _size, bool _yflip)
{
	// Convert from BGRA to RGBA, and flip y if needed.
	if (screenShotDataBuffer_.size() < _size)
	{
		screenShotDataBuffer_.resize(_size);
	}

	for (uint32_t y = 0; y < _height; y++)
	{
		for (uint32_t x = 0; x < _width; x++)
		{
			auto colorIn = &((const uint8_t *)_data)[x * 4 + (_yflip ? _height - 1 - y : y) * _pitch];
			uint8_t *colorOut = &screenShotDataBuffer_[x * 4 + y * _pitch];
			colorOut[0] = colorIn[2];
			colorOut[1] = colorIn[1];
			colorOut[2] = colorIn[0];
			colorOut[3] = colorIn[3];
		}
	}

	// Write to file buffer.
	const char *extension = COM_GetExtension(_filePath);
	ImageWriteBuffer buffer;
	buffer.data = &screenShotFileBuffer_;
	buffer.bytesWritten = 0;

	if (!Q_stricmp(extension, "png"))
	{
		if (!stbi_write_png_to_func(ImageWriteCallback, &buffer, _width, _height, _pitch / _width, screenShotDataBuffer_.data(), _pitch))
		{
			ri.Printf(PRINT_ALL, "Screenshot: error writing png file\n");
			return;
		}
	}
	else if (!Q_stricmp(extension, "jpg"))
	{
		if (!jo_write_jpg_to_func(ImageWriteCallbackConst, &buffer, screenShotDataBuffer_.data(), _width, _height, _pitch / _width, g_main->cvars.screenshotJpegQuality->integer))
		{
			ri.Printf(PRINT_ALL, "Screenshot: error writing jpg file\n");
			return;
		}
	}
	else
	{
		if (!stbi_write_tga_to_func(ImageWriteCallback, &buffer, _width, _height, _pitch / _width, screenShotDataBuffer_.data()))
		{
			ri.Printf(PRINT_ALL, "Screenshot: error writing tga file\n");
			return;
		}
	}

	// Write file buffer to file.
	if (buffer.bytesWritten > 0)
	{
		ri.FS_WriteFile(_filePath, buffer.data->data(), buffer.bytesWritten);
	}
}

void BgfxCallback::captureBegin(uint32_t _width, uint32_t _height, uint32_t _pitch, bgfx::TextureFormat::Enum _format, bool _yflip)
{
}

void BgfxCallback::captureEnd()
{
}

void BgfxCallback::captureFrame(const void* _data, uint32_t _size)
{
}

bool DrawCall::operator<(const DrawCall &other) const
{
	assert(material);
	assert(other.material);

	if (material->sort < other.material->sort)
	{
		return true;
	}
	else if (material->sort == other.material->sort)
	{
		if (material->index < other.material->index)
			return true;
	}

	return false;
}

void Main::debugPrint(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	char text[1024];
	Q_vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	bgfx::dbgTextPrintf(0, debugTextY, 0x4f, text);
	debugTextY++;
}

void Main::drawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, int materialIndex)
{
	auto mat = materialCache->getMaterial(materialIndex);

	if (stretchPicMaterial_ != mat)
	{
		flushStretchPics();
		stretchPicMaterial_ = mat;
	}

	auto firstVertex = (const uint16_t)stretchPicVertices_.size();
	size_t firstIndex = stretchPicIndices_.size();
	stretchPicVertices_.resize(stretchPicVertices_.size() + 4);
	stretchPicIndices_.resize(stretchPicIndices_.size() + 6);
	Vertex *v = &stretchPicVertices_[firstVertex];
	uint16_t *i = &stretchPicIndices_[firstIndex];
	v[0].pos = vec3(x, y, 0);
	v[1].pos = vec3(x + w, y, 0);
	v[2].pos = vec3(x + w, y + h, 0);
	v[3].pos = vec3(x, y + h, 0);
	v[0].texCoord = vec2(s1, t1);
	v[1].texCoord = vec2(s2, t1);
	v[2].texCoord = vec2(s2, t2);
	v[3].texCoord = vec2(s1, t2);
	v[0].color = v[1].color = v[2].color = v[3].color = stretchPicColor_;
	i[0] = firstVertex + 3; i[1] = firstVertex + 0; i[2] = firstVertex + 2;
	i[3] = firstVertex + 2; i[4] = firstVertex + 0; i[5] = firstVertex + 1;
}

void Main::drawStretchRaw(int x, int y, int w, int h, int cols, int rows, const byte *data, int client, bool dirty)
{
	// Make sure rows and cols are powers of 2.
	if (!math::IsPowerOfTwo(cols) || !math::IsPowerOfTwo(rows))
	{
		ri.Error(ERR_DROP, "Draw_StretchRaw: size not a power of 2: %i by %i", cols, rows);
	}

	// Upload texture
	uploadCinematic(w, h, cols, rows, data, client, dirty);

	// Setup view
	const uint8_t viewId = pushView(ViewFlags::None, vec4(x, y, w, h), mat4::identity, mat4::orthographicProjection(0, 1, 0, 1, -1, 1));

	// Fullscreen quad geometry
	bgfx::setVertexBuffer(fsVertexBuffer_.handle, 0, 4);
	bgfx::setIndexBuffer(fsIndexBuffer_.handle, 0, 6);

	// Bind shader and texture
	auto shaderProgramHandle = shaderCache->getHandle(ShaderProgramId::TextureColor);
	textureCache->getScratchTextures()[client]->setSampler(MaterialTextureBundleIndex::ColorMap);
	uniforms->color.set(vec4::white);

	// Submit
	bgfx::setState(BGFX_STATE_RGB_WRITE);
	bgfx::submit(viewId, shaderProgramHandle);
}

void Main::uploadCinematic(int w, int h, int cols, int rows, const byte *data, int client, bool dirty)
{
	auto scratch = textureCache->getScratchTextures()[client];
	
	if (cols != scratch->getWidth() || rows != scratch->getHeight())
	{
		scratch->resize(cols, rows);
		dirty = true;
	}

	if (dirty)
	{
		auto mem = bgfx::alloc(cols * rows * 4);
		memcpy(mem->data, data, mem->size);
		scratch->update(mem, 0, 0, cols, rows);
	}
}

void Main::loadWorld(const char *name)
{
	if (world.get())
	{
		ri.Error(ERR_DROP, "ERROR: attempted to redundantly load world map");
	}

	world = World::createBSP(name);
	world->load();
	mainVisCacheId = world->createVisCache();
	portalVisCacheId = world->createVisCache();
}

void Main::addDynamicLightToScene(DynamicLight light)
{
	sceneDynamicLights_.push_back(light);
}

void Main::addEntityToScene(const refEntity_t *entity)
{
	sceneEntities_.push_back({ *entity });
}

void Main::addPolyToScene(qhandle_t hShader, int nVerts, const polyVert_t *verts, int nPolys)
{
	const size_t firstVertex = scenePolygonVertices_.size();
	scenePolygonVertices_.insert(scenePolygonVertices_.end(), verts, &verts[nPolys * nVerts]);

	for (size_t i = 0; i < nPolys; i++)
	{
		Polygon p;
		p.material = materialCache->getMaterial(hShader); 
		p.firstVertex = firstVertex + i * nVerts;
		p.nVertices = nVerts;
		Bounds bounds;
		bounds.setupForAddingPoints();

		for (size_t i = 0; i < p.nVertices; i++)
		{
			bounds.addPoint(scenePolygonVertices_[p.firstVertex + i].xyz);
		}

		p.fogIndex = world->findFogIndex(bounds);
		scenePolygons_.push_back(p);
	}
}

void Main::renderScene(const refdef_t *def)
{
	flushStretchPics();
	time_ = def->time;
	floatTime_ = def->time * 0.001f;
	
	// Clamp view rect to screen.
	const int x = std::max(0, def->x);
	const int y = std::max(0, def->y);
	const int w = std::min(glConfig.vidWidth, x + def->width) - x;
	const int h = std::min(glConfig.vidHeight, y + def->height) - y;

	scenePosition = vec3(def->vieworg);
	sceneRotation = mat3(def->viewaxis);
	isWorldCamera = (def->rdflags & RDF_NOWORLDMODEL) == 0 && world.get();
	renderCamera(mainVisCacheId, scenePosition, scenePosition, sceneRotation, vec4(x, y, w, h), vec2(def->fov_x, def->fov_y), def->areamask);
	sceneDynamicLights_.clear();
	sceneEntities_.clear();
	scenePolygons_.clear();
	sortedScenePolygons_.clear();
	scenePolygonVertices_.clear();
}

void Main::endFrame()
{
	// Clear the screen if there's no active views.
	if (firstFreeViewId_ == 0)
	{
		bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH);
		bgfx::setViewRect(0, 0, 0, glConfig.vidWidth, glConfig.vidHeight);
		bgfx::touch(0);
	}

	flushStretchPics();
	bgfx::frame();

	if (cvars.wireframe->modified || cvars.bgfx_stats->modified || cvars.debugText->modified)
	{
		uint32_t debug = 0;

		if (cvars.wireframe->integer != 0)
			debug |= BGFX_DEBUG_WIREFRAME;

		if (cvars.bgfx_stats->integer != 0)
			debug |= BGFX_DEBUG_STATS;

		if (cvars.debugText->integer != 0)
			debug |= BGFX_DEBUG_TEXT;

		bgfx::setDebug(debug);
		cvars.wireframe->modified = cvars.bgfx_stats->modified = cvars.debugText->modified = qfalse;
	}

	if (cvars.debugText->integer)
	{
		bgfx::dbgTextClear();
		debugTextY = 0;
	}

	firstFreeViewId_ = 0;
	frameNo_++;
}

bool Main::sampleLight(vec3 position, vec3 *ambientLight, vec3 *directedLight, vec3 *lightDir)
{
	if (!world->hasLightGrid())
		return false;

	world->sampleLightGrid(position, ambientLight, directedLight, lightDir);
	return true;
}

uint8_t Main::pushView(int flags, vec4 rect, const mat4 &viewMatrix, const mat4 &projectionMatrix)
{
	uint16_t clearFlags = BGFX_CLEAR_NONE;

	if ((flags & ViewFlags::ClearDepth) != 0)
	{
		clearFlags |= BGFX_CLEAR_DEPTH;
	}

	// Need to clear color in wireframe.
	if (firstFreeViewId_ == (cvars.softSprites->integer ? 1 : 0) && cvars.wireframe->integer != 0)
	{
		clearFlags |= BGFX_CLEAR_COLOR;
	}

	bgfx::setViewClear(firstFreeViewId_, clearFlags);
	bgfx::setViewSeq(firstFreeViewId_, true);

	if ((flags & ViewFlags::Ortho) != 0)
	{
		bgfx::setViewRect(firstFreeViewId_, 0, 0, glConfig.vidWidth, glConfig.vidHeight);
		bgfx::setViewTransform(firstFreeViewId_, NULL, mat4::orthographicProjection(0, glConfig.vidWidth, 0, glConfig.vidHeight, -1, 1).get());
	}
	else
	{
		bgfx::setViewRect(firstFreeViewId_, (uint16_t)rect[0], (uint16_t)rect[1], (uint16_t)rect[2], (uint16_t)rect[3]);
		bgfx::setViewTransform(firstFreeViewId_, viewMatrix.get(), projectionMatrix.get());
	}

	firstFreeViewId_++;
	return firstFreeViewId_ - 1;
}

void Main::flushStretchPics()
{
	if (!stretchPicIndices_.empty())
	{
		// Set time for 2D materials.
		time_ = ri.Milliseconds();
		floatTime_ = time_ * 0.001f;

		// Setup view.
		const uint8_t viewId = pushView(ViewFlags::Ortho);

		bgfx::TransientVertexBuffer tvb;
		bgfx::TransientIndexBuffer tib;

		if (!bgfx::allocTransientBuffers(&tvb, Vertex::decl, (uint32_t)stretchPicVertices_.size(), &tib, (uint32_t)stretchPicIndices_.size()))
		{
			WarnOnce(WarnOnceId::TransientBuffer);
		}
		else
		{
			memcpy(tvb.data, &stretchPicVertices_[0], sizeof(Vertex) * stretchPicVertices_.size());
			memcpy(tib.data, &stretchPicIndices_[0], sizeof(uint16_t) * stretchPicIndices_.size());
			stretchPicMaterial_->setTime(floatTime_);

			for (size_t i = 0; i < stretchPicMaterial_->getNumStages(); i++)
			{
				stretchPicMaterial_->setStageShaderUniforms(i);
				stretchPicMaterial_->setStageTextureSamplers(i);
				uint64_t state = BGFX_STATE_RGB_WRITE | BGFX_STATE_ALPHA_WRITE | stretchPicMaterial_->getStageState(i);

				// Depth testing and writing should always be off for 2D drawing.
				state &= ~BGFX_STATE_DEPTH_TEST_MASK;
				state &= ~BGFX_STATE_DEPTH_WRITE;
			
				bgfx::setState(state);
				bgfx::setVertexBuffer(&tvb);
				bgfx::setIndexBuffer(&tib);
				bgfx::submit(viewId, shaderCache->getHandle(ShaderProgramId::Generic));
			}
		}
	}

	stretchPicVertices_.clear();
	stretchPicIndices_.clear();
}

static void SetDrawCallGeometry(const DrawCall &dc)
{
	assert(dc.vb.nVertices);
	assert(dc.ib.nIndices);

	if (dc.vb.type == DrawCall::BufferType::Static)
	{
		bgfx::setVertexBuffer(dc.vb.staticHandle, dc.vb.firstVertex, dc.vb.nVertices);
	}
	else if (dc.vb.type == DrawCall::BufferType::Dynamic)
	{
		bgfx::setVertexBuffer(dc.vb.dynamicHandle, dc.vb.nVertices);
	}
	else if (dc.vb.type == DrawCall::BufferType::Transient)
	{
		bgfx::setVertexBuffer(&dc.vb.transientHandle, dc.vb.firstVertex, dc.vb.nVertices);
	}

	if (dc.ib.type == DrawCall::BufferType::Static)
	{
		bgfx::setIndexBuffer(dc.ib.staticHandle, dc.ib.firstIndex, dc.ib.nIndices);
	}
	else if (dc.ib.type == DrawCall::BufferType::Dynamic)
	{
		bgfx::setIndexBuffer(dc.ib.dynamicHandle, dc.ib.firstIndex, dc.ib.nIndices);
	}
	else if (dc.ib.type == DrawCall::BufferType::Transient)
	{
		bgfx::setIndexBuffer(&dc.ib.transientHandle, dc.ib.firstIndex, dc.ib.nIndices);
	}
}

void Main::renderCamera(uint8_t visCacheId, vec3 pvsPosition, vec3 position, mat3 rotation, vec4 rect, vec2 fov, const uint8_t *areaMask)
{
	assert(areaMask);
	const float zMin = 4;
	float zMax = 2048;
	const float polygonDepthOffset = -0.001f;
	const bool isMainCamera = visCacheId == mainVisCacheId;
	cameraPosition = position;
	cameraRotation = rotation;

	if (isWorldCamera)
	{
		assert(world.get());
		world->updateVisCache(visCacheId, pvsPosition, areaMask);

		// Use dynamic z max.
		zMax = world->getBounds(visCacheId).calculateFarthestCornerDistance(cameraPosition);
	}

	const mat4 viewMatrix = toOpenGlMatrix * mat4::view(cameraPosition, cameraRotation);
	const mat4 projectionMatrix = mat4::perspectiveProjection(fov.x, fov.y, zMin, zMax);

	if (isWorldCamera && isMainCamera)
	{
		for (size_t i = 0; i < world->getNumPortalSurfaces(visCacheId); i++)
		{
			vec3 pvsPosition;
			Transform portalCamera;

			if (world->calculatePortalCamera(visCacheId, i, cameraPosition, cameraRotation, projectionMatrix * viewMatrix, sceneEntities_, &pvsPosition, &portalCamera, &isMirrorCamera, &portalPlane))
			{
				renderCamera(portalVisCacheId, pvsPosition, portalCamera.position, portalCamera.rotation, rect, fov, areaMask);
				isMirrorCamera = false;
				break;
			}
		}

		cameraPosition = position;
		cameraRotation = rotation;
	}

	// Build draw calls. Order doesn't matter.
	drawCalls_.clear();

	if (isWorldCamera)
	{
		Sky_Render(&drawCalls_, cameraPosition, visCacheId, zMax);
		world->render(&drawCalls_, visCacheId);
	}

	for (auto &entity : sceneEntities_)
	{
		if (isMainCamera && (entity.e.renderfx & RF_THIRD_PERSON) != 0)
			continue;

		if (!isMainCamera && (entity.e.renderfx & RF_FIRST_PERSON) != 0)
			continue;

		renderEntity(&drawCalls_, cameraPosition, cameraRotation, &entity);
	}

	if (!scenePolygons_.empty())
	{
		// Sort polygons by material and fogIndex for batching.
		for (auto &polygon : scenePolygons_)
		{
			sortedScenePolygons_.push_back(&polygon);
		}

		std::sort(sortedScenePolygons_.begin(), sortedScenePolygons_.end(), [](Polygon *a, Polygon *b)
		{
			if (a->material->index < b->material->index)
				return true;
			else if (a->material->index == b->material->index)
			{
				return a->fogIndex < b->fogIndex;
			}

			return false;
		});

		size_t batchStart = 0;
		
		for (;;)
		{
			uint32_t nVertices = 0, nIndices = 0;
			size_t batchEnd;

			// Find the last polygon index that matches the current material and fog. Count geo as we go.
			for (batchEnd = batchStart; batchEnd < sortedScenePolygons_.size(); batchEnd++)
			{
				const Polygon *p = sortedScenePolygons_[batchEnd];

				if (p->material != sortedScenePolygons_[batchStart]->material || p->fogIndex != sortedScenePolygons_[batchStart]->fogIndex)
					break;
				
				nVertices += p->nVertices;
				nIndices += (p->nVertices - 2) * 3;
			}

			batchEnd = std::max(batchStart, batchEnd - 1);

			// Got a range of polygons to batch. Build a draw call.
			bgfx::TransientVertexBuffer tvb;
			bgfx::TransientIndexBuffer tib;

			if (!bgfx::allocTransientBuffers(&tvb, Vertex::decl, nVertices, &tib, nIndices) )
			{
				WarnOnce(WarnOnceId::TransientBuffer);
				break;
			}

			auto vertices = (Vertex *)tvb.data;
			auto indices = (uint16_t *)tib.data;
			uint32_t currentVertex = 0, currentIndex = 0;

			for (size_t i = batchStart; i <= batchEnd; i++)
			{
				const Polygon *p = sortedScenePolygons_[i];
				const uint32_t firstVertex = currentVertex;

				for (size_t j = 0; j < p->nVertices; j++)
				{
					auto &v = vertices[currentVertex++];
					auto &pv = scenePolygonVertices_[p->firstVertex + j];
					v.pos = pv.xyz;
					v.texCoord = pv.st;
					v.color = vec4::fromBytes(pv.modulate);
				}

				for (size_t j = 0; j < p->nVertices - 2; j++)
				{
					indices[currentIndex++] = firstVertex + 0;
					indices[currentIndex++] = firstVertex + uint16_t(j) + 1;
					indices[currentIndex++] = firstVertex + uint16_t(j) + 2;
				}
			}

			DrawCall dc;
			dc.fogIndex = sortedScenePolygons_[batchStart]->fogIndex;
			dc.material = sortedScenePolygons_[batchStart]->material;
			dc.vb.type = dc.ib.type = DrawCall::BufferType::Transient;
			dc.vb.transientHandle = tvb;
			dc.vb.nVertices = nVertices;
			dc.ib.transientHandle = tib;
			dc.ib.nIndices = nIndices;
			drawCalls_.push_back(dc);

			// Iterate.
			batchStart = batchEnd + 1;

			if (batchStart >= sortedScenePolygons_.size())
				break;
		}
	}

	if (drawCalls_.empty())
		return;

	// Do material CPU deforms.
	for (DrawCall &dc : drawCalls_)
	{
		assert(dc.material);

		if (dc.material->hasCpuDeforms())
		{
			// Material requires CPU deforms, but geometry isn't available in system memory.
			if (dc.vb.type != DrawCall::BufferType::Transient || dc.ib.type != DrawCall::BufferType::Transient)
				continue;

			currentEntity = dc.entity;
			dc.material->setTime(floatTime_);
			dc.material->doCpuDeforms(&dc);
			currentEntity = nullptr;
		}
	}

	// Sort draw calls.
	std::sort(drawCalls_.begin(), drawCalls_.end());

	// Set portal clipping.
	if (!isMainCamera)
	{
		uniforms->portalClip.set(vec4(1, 0, 0, 0));
		uniforms->portalPlane.set(portalPlane);
	}
	else
	{
		uniforms->portalClip.set(vec4(0, 0, 0, 0));
	}

	// Setup dynamic lights.
	uniforms->nDynamicLights.set(vec4(sceneDynamicLights_.size(), 0, 0, 0));
	vec4 dlightColors[DynamicLight::max];
	vec4 dlightPositions[DynamicLight::max];

	for (size_t i = 0; i < sceneDynamicLights_.size(); i++)
	{
		const auto &dl = sceneDynamicLights_[i];
		dlightColors[i] = vec4(dl.color.r, dl.color.g, dl.color.b, dl.intensity);
		dlightPositions[i] = dl.position;
	}

	uniforms->dlightColors.set(dlightColors, DynamicLight::max);
	uniforms->dlightPositions.set(dlightPositions, DynamicLight::max);

	// Render depth.
	if (isWorldCamera)
	{
		const uint8_t viewId = pushView(ViewFlags::ClearDepth, rect, viewMatrix, projectionMatrix);
		bgfx::setViewFrameBuffer(viewId, depthFrameBuffer_);

		for (DrawCall &dc : drawCalls_)
		{
			// Material remapping.
			auto mat = dc.material->remappedShader ? dc.material->remappedShader : dc.material;

			if (mat->sort != MaterialSort::Opaque)
				continue;

			mat->setTime(floatTime_);
			const mat4 modelViewMatrix(viewMatrix * dc.modelMatrix);
			uniforms->depthRange.set(vec4(dc.zOffset, dc.zScale, zMin, zMax));
			mat->setFogShaderUniforms();

			// See if any of the stages use alpha testing.
			MaterialStage *alphaTestStage = nullptr;

			for (size_t i = 0; i < mat->getNumStages(); i++)
			{
				if (mat->stages[i].alphaTest != MaterialAlphaTest::None)
				{
					alphaTestStage = &mat->stages[i];
					break;
				}
			}

			SetDrawCallGeometry(dc);
			bgfx::setTransform(dc.modelMatrix.get());
			uint64_t state = BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_DEPTH_WRITE | BGFX_STATE_MSAA;

			if (mat->cullType != MaterialCullType::TwoSided)
			{
				bool cullFront = (mat->cullType == MaterialCullType::FrontSided);

				if (g_main->isMirrorCamera)
					cullFront = !cullFront;

				state |= cullFront ? BGFX_STATE_CULL_CCW : BGFX_STATE_CULL_CW;
			}

			bgfx::setState(state);

			if (alphaTestStage)
			{
				uniforms->alphaTest.set((float)alphaTestStage->alphaTest);
				alphaTestStage->bundles[MaterialTextureBundleIndex::DiffuseMap].textures[0]->setSampler(MaterialTextureBundleIndex::DiffuseMap);
				bgfx::submit(viewId, shaderCache->getHandle(ShaderProgramId::Depth_AlphaTest));
			}
			else
			{
				uniforms->alphaTest.set(vec4::empty);
				bgfx::submit(viewId, shaderCache->getHandle(ShaderProgramId::Depth));
			}
		}
	}

	const uint8_t viewId = pushView(ViewFlags::ClearDepth, rect, viewMatrix, projectionMatrix);

	for (DrawCall &dc : drawCalls_)
	{
		assert(dc.material);

		// Material remapping.
		auto mat = dc.material->remappedShader ? dc.material->remappedShader : dc.material;

		// Special case for skybox.
		if (dc.flags >= DrawCallFlags::SkyboxSideFirst && dc.flags <= DrawCallFlags::SkyboxSideLast)
		{
			uniforms->alphaTest.set(vec4::empty);
			uniforms->baseColor.set(vec4::white);
			uniforms->depthRange.set(vec4(dc.zOffset, dc.zScale, zMin, zMax));
			uniforms->generators.set(vec4::empty);
			uniforms->lightType.set(vec4::empty);
			uniforms->vertexColor.set(vec4::black);
			const int sky_texorder[6] = { 0, 2, 1, 3, 4, 5 };
			const int side = dc.flags - DrawCallFlags::SkyboxSideFirst;
			mat->sky.outerbox[sky_texorder[side]]->setSampler(MaterialTextureBundleIndex::DiffuseMap);
			SetDrawCallGeometry(dc);
			bgfx::setTransform(dc.modelMatrix.get());
			bgfx::setState(dc.state);
			bgfx::submit(viewId, shaderCache->getHandle(ShaderProgramId::Generic));
			continue;
		}

		currentEntity = dc.entity;
		mat->setTime(floatTime_);
		const vec3 localViewPosition = currentEntity ? currentEntity->localViewPosition : cameraPosition;
		const mat4 modelViewMatrix(viewMatrix * dc.modelMatrix);

		for (size_t i = 0; i < mat->getNumStages(); i++)
		{
			if (mat->polygonOffset)
			{
				uniforms->depthRange.set(vec4(dc.zOffset + polygonDepthOffset, dc.zScale, zMin, zMax));
			}
			else
			{
				uniforms->depthRange.set(vec4(dc.zOffset, dc.zScale, zMin, zMax));
			}

			uniforms->viewOrigin.set(cameraPosition);
			uniforms->localViewOrigin.set(localViewPosition);

			if (dc.fogIndex >= 0 && mat->stages[i].adjustColorsForFog != MaterialAdjustColorsForFog::None)
			{
				vec4 fogDistance, fogDepth;
				float eyeT;
				world->calculateFog(dc.fogIndex, dc.modelMatrix, modelViewMatrix, localViewPosition, nullptr, &fogDistance, &fogDepth, &eyeT);

				uniforms->fogEnabled.set(vec4(1, 0, 0, 0));
				uniforms->fogDistance.set(fogDistance);
				uniforms->fogDepth.set(fogDepth);
				uniforms->fogEyeT.set(eyeT);
				uniforms->fogColorMask.set(mat->calculateStageFogColorMask(i));
			}
			else
			{
				uniforms->fogEnabled.set(vec4::empty);
			}

			mat->setStageShaderUniforms(i);
			mat->setStageTextureSamplers(i);
			SetDrawCallGeometry(dc);
			bgfx::setTransform(dc.modelMatrix.get());
			ShaderProgramId shaderProgram;
			uint64_t state = dc.state | mat->getStageState(i);

			if (cvars.softSprites->integer && dc.softSpriteDepth > 0)
			{
				shaderProgram = ShaderProgramId::Generic_SoftSprite;
				bgfx::setTexture(MaterialTextureBundleIndex::Depth, g_main->uniforms->textures[MaterialTextureBundleIndex::Depth]->handle, depthTexture_);
				uniforms->softSpriteDepth.set(dc.softSpriteDepth);

				// Change additive blend from (1, 1) to (src alpha, 1) so the soft sprite shader can control alpha.
				if ((state & BGFX_STATE_BLEND_MASK) == BGFX_STATE_BLEND_ADD)
				{
					state &= ~BGFX_STATE_BLEND_MASK;
					state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE);
				}
			}
			else if (mat->stages[i].alphaTest != MaterialAlphaTest::None)
			{
				shaderProgram = ShaderProgramId::Generic_AlphaTest;
			}
			else
			{
				shaderProgram = ShaderProgramId::Generic;
			}

			bgfx::setState(state);
			bgfx::submit(viewId, shaderCache->getHandle(shaderProgram));
		}

		// Do fog pass.
		if (dc.fogIndex >= 0 && mat->fogPass != MaterialFogPass::None && (mat->surfaceFlags & SURF_SKY) == 0)
		{
			// Make sure fog shader is valid. Don't want to fall back to generic if it isn't, just don't draw the fog.
			auto fogShaderHandle = shaderCache->getHandle(ShaderProgramId::Fog, ShaderCache::GetHandleFlags::ReturnInvalid);

			if (bgfx::isValid(fogShaderHandle))
			{
				if (mat->polygonOffset)
				{
					uniforms->depthRange.set(vec4(dc.zOffset + polygonDepthOffset, dc.zScale, zMin, zMax));
				}
				else
				{
					uniforms->depthRange.set(vec4(dc.zOffset, dc.zScale, zMin, zMax));
				}

				mat->setFogShaderUniforms();
				vec4 fogColor, fogDistance, fogDepth;
				float eyeT;
				world->calculateFog(dc.fogIndex, dc.modelMatrix, modelViewMatrix, localViewPosition, &fogColor, &fogDistance, &fogDepth, &eyeT);
				uniforms->color.set(fogColor);
				uniforms->fogDistance.set(fogDistance);
				uniforms->fogDepth.set(fogDepth);
				uniforms->fogEyeT.set(eyeT);
				SetDrawCallGeometry(dc);
				bgfx::setTransform(dc.modelMatrix.get());
				uint64_t state = dc.state | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

				if (mat->fogPass == MaterialFogPass::Equal)
				{
					state |= BGFX_STATE_DEPTH_TEST_EQUAL;
				}
				else
				{
					state |= BGFX_STATE_DEPTH_TEST_LEQUAL;
				}

				bgfx::setState(state);
				bgfx::submit(viewId, fogShaderHandle);
			}
		}

		currentEntity = nullptr;
	}
}

void Main::renderEntity(DrawCallList *drawCallList, vec3 viewPosition, mat3 viewRotation, Entity *entity)
{
	assert(drawCallList);
	assert(entity);

	// Calculate the viewer origin in the model's space.
	// Needed for fog, specular, and environment mapping.
	const vec3 delta = viewPosition - vec3(entity->e.origin);

	// Compensate for scale in the axes if necessary.
	float axisLength = 1;

	if (entity->e.nonNormalizedAxes)
	{
		axisLength = 1.0f / vec3(entity->e.axis[0]).length();
	}

	entity->localViewPosition =
	{
		vec3::dotProduct(delta, entity->e.axis[0]) * axisLength,
		vec3::dotProduct(delta, entity->e.axis[1]) * axisLength,
		vec3::dotProduct(delta, entity->e.axis[2]) * axisLength
	};

	switch (entity->e.reType)
	{
	case RT_BEAM:
		break;

	case RT_LIGHTNING:
		renderLightningEntity(drawCallList, viewPosition, viewRotation, entity);
		break;

	case RT_MODEL:
		{
			auto model = modelCache->getModel(entity->e.hModel);
			setupEntityLighting(entity);
			model->render(drawCallList, entity);
		}
		break;
	
	case RT_RAIL_CORE:
		renderRailCoreEntity(drawCallList, viewPosition, viewRotation, entity);
		break;

	case RT_RAIL_RINGS:
		renderRailRingsEntity(drawCallList, entity);
		break;

	case RT_SPRITE:
		renderSpriteEntity(drawCallList, viewRotation, entity);
		break;

	default:
		break;
	}
}

void Main::renderLightningEntity(DrawCallList *drawCallList, vec3 viewPosition, mat3 viewRotation, Entity *entity)
{
	assert(drawCallList);
	const vec3 start(entity->e.oldorigin), end(entity->e.origin);
	vec3 dir = (end - start);
	const float length = dir.normalize();

	// Compute side vector.
	const vec3 v1 = (start - viewPosition).normal();
	const vec3 v2 = (end - viewPosition).normal();
	vec3 right = vec3::crossProduct(v1, v2).normal();

	for (int i = 0; i < 4; i++)
	{
		renderRailCore(drawCallList, start, end, right, length, cvars.railCoreWidth->integer, materialCache->getMaterial(entity->e.customShader), vec4::fromBytes(entity->e.shaderRGBA), entity);
		right = right.rotatedAroundDirection(dir, 45);
	}
}

void Main::renderRailCoreEntity(DrawCallList *drawCallList, vec3 viewPosition, mat3 viewRotation, Entity *entity)
{
	assert(drawCallList);
	const vec3 start(entity->e.oldorigin), end(entity->e.origin);
	vec3 dir = (end - start);
	const float length = dir.normalize();

	// Compute side vector.
	const vec3 v1 = (start - viewPosition).normal();
	const vec3 v2 = (end - viewPosition).normal();
	const vec3 right = vec3::crossProduct(v1, v2).normal();

	renderRailCore(drawCallList, start, end, right, length, cvars.railCoreWidth->integer, materialCache->getMaterial(entity->e.customShader), vec4::fromBytes(entity->e.shaderRGBA), entity);
}

void Main::renderRailCore(DrawCallList *drawCallList, vec3 start, vec3 end, vec3 up, float length, float spanWidth, Material *mat, vec4 color, Entity *entity)
{
	assert(drawCallList);
	const uint32_t nVertices = 4, nIndices = 6;
	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer tib;

	if (!bgfx::allocTransientBuffers(&tvb, Vertex::decl, nVertices, &tib, nIndices)) 
	{
		WarnOnce(WarnOnceId::TransientBuffer);
		return;
	}

	auto vertices = (Vertex *)tvb.data;
	vertices[0].pos = start + up * spanWidth;
	vertices[1].pos = start + up * -spanWidth;
	vertices[2].pos = end + up * spanWidth;
	vertices[3].pos = end + up * -spanWidth;

	const float t = length / 256.0f;
	vertices[0].texCoord = vec2(0, 0);
	vertices[1].texCoord = vec2(0, 1);
	vertices[2].texCoord = vec2(t, 0);
	vertices[3].texCoord = vec2(t, 1);

	vertices[0].color = vec4(color.xyz() * 0.25f, 1);
	vertices[1].color = vertices[2].color = vertices[3].color = color;

	auto indices = (uint16_t *)tib.data;
	indices[0] = 0; indices[1] = 1; indices[2] = 2;
	indices[3] = 2; indices[4] = 1; indices[5] = 3;

	DrawCall dc;
	dc.entity = entity;
	dc.fogIndex = isWorldCamera ? world->findFogIndex(entity->e.origin, entity->e.radius) : -1;
	dc.material = mat;
	dc.vb.type = dc.ib.type = DrawCall::BufferType::Transient;
	dc.vb.transientHandle = tvb;
	dc.vb.nVertices = nVertices;
	dc.ib.transientHandle = tib;
	dc.ib.nIndices = nIndices;
	drawCallList->push_back(dc);
}

void Main::renderRailRingsEntity(DrawCallList *drawCallList, Entity *entity)
{
	assert(drawCallList);
	const vec3 start(entity->e.oldorigin), end(entity->e.origin);
	vec3 dir = (end - start);
	const float length = dir.normalize();
	vec3 right, up;
	dir.toNormalVectors(&right, &up);
	dir *= cvars.railSegmentLength->value;
	int nSegments = std::max(1.0f, length / cvars.railSegmentLength->value);

	if (nSegments > 1)
		nSegments--;

	if (!nSegments)
		return;

	const float scale = 0.25f;
	const int spanWidth = cvars.railWidth->integer;
	vec3 positions[4];

	for (int i = 0; i < 4; i++)
	{
		const float c = cos(DEG2RAD(45 + i * 90));
		const float s = sin(DEG2RAD(45 + i * 90));
		positions[i] = start + (right * c + up * s) * scale * spanWidth;

		if (nSegments)
		{
			// Offset by 1 segment if we're doing a long distance shot.
			positions[i] += dir;
		}
	}

	const uint32_t nVertices = 4 * nSegments, nIndices = 6 * nSegments;
	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer tib;

	if (!bgfx::allocTransientBuffers(&tvb, Vertex::decl, nVertices, &tib, nIndices)) 
	{
		WarnOnce(WarnOnceId::TransientBuffer);
		return;
	}

	for (int i = 0; i < nSegments; i++)
	{
		for (int j = 0; j < 4; j++ )
		{
			auto vertex = &((Vertex *)tvb.data)[i * 4 + j];
			vertex->pos = positions[j];
			vertex->texCoord[0] = j < 2;
			vertex->texCoord[1] = j && j != 3;
			vertex->color = vec4::fromBytes(entity->e.shaderRGBA);
			positions[j] += dir;
		}

		auto index = &((uint16_t *)tib.data)[i * 6];
		const uint16_t offset = i * 4;
		index[0] = offset + 0; index[1] = offset + 1; index[2] = offset + 3;
		index[3] = offset + 3; index[4] = offset + 1; index[5] = offset + 2;
	}

	DrawCall dc;
	dc.entity = entity;
	dc.fogIndex = isWorldCamera ? world->findFogIndex(entity->e.origin, entity->e.radius) : -1;
	dc.material = materialCache->getMaterial(entity->e.customShader);
	dc.vb.type = dc.ib.type = DrawCall::BufferType::Transient;
	dc.vb.transientHandle = tvb;
	dc.vb.nVertices = nVertices;
	dc.ib.transientHandle = tib;
	dc.ib.nIndices = nIndices;
	drawCallList->push_back(dc);
}

void Main::renderSpriteEntity(DrawCallList *drawCallList, mat3 viewRotation, Entity *entity)
{
	assert(drawCallList);

	// Calculate the positions for the four corners.
	vec3 left, up;

	if (entity->e.rotation == 0)
	{
		left = viewRotation[1] * entity->e.radius;
		up = viewRotation[2] * entity->e.radius;
	}
	else
	{
		const float ang = M_PI * entity->e.rotation / 180;
		const float s = sin(ang);
		const float c = cos(ang);
		left = viewRotation[1] * (c * entity->e.radius) + viewRotation[2] * (-s * entity->e.radius);
		up = viewRotation[2] * (c * entity->e.radius) + viewRotation[1] * (s * entity->e.radius);
	}

	if (isMirrorCamera)
		left = -left;

	const uint32_t nVertices = 4, nIndices = 6;
	bgfx::TransientVertexBuffer tvb;
	bgfx::TransientIndexBuffer tib;

	if (!bgfx::allocTransientBuffers(&tvb, Vertex::decl, nVertices, &tib, nIndices)) 
	{
		WarnOnce(WarnOnceId::TransientBuffer);
		return;
	}

	auto vertices = (Vertex *)tvb.data;
	const vec3 position(entity->e.origin);
	vertices[0].pos = position + left + up;
	vertices[1].pos = position - left + up;
	vertices[2].pos = position - left - up;
	vertices[3].pos = position + left - up;

	// Constant normal all the way around.
	vertices[0].normal = vertices[1].normal = vertices[2].normal = vertices[3].normal = -viewRotation[0];

	// Standard square texture coordinates.
	vertices[0].texCoord = vertices[0].texCoord2 = vec2(0, 0);
	vertices[1].texCoord = vertices[1].texCoord2 = vec2(1, 0);
	vertices[2].texCoord = vertices[2].texCoord2 = vec2(1, 1);
	vertices[3].texCoord = vertices[3].texCoord2 = vec2(0, 1);

	// Constant color all the way around.
	vertices[0].color = vertices[1].color = vertices[2].color = vertices[3].color = vec4::fromBytes(entity->e.shaderRGBA);

	auto indices = (uint16_t *)tib.data;
	indices[0] = 0; indices[1] = 1; indices[2] = 3;
	indices[3] = 3; indices[4] = 1; indices[5] = 2;

	DrawCall dc;

	if (g_main->cvars.softSprites->integer)
	{
		dc.softSpriteDepth = entity->e.radius / 2.0f;
	}

	dc.entity = entity;
	dc.fogIndex = isWorldCamera ? world->findFogIndex(entity->e.origin, entity->e.radius) : -1;
	dc.material = materialCache->getMaterial(entity->e.customShader);
	dc.vb.type = dc.ib.type = DrawCall::BufferType::Transient;
	dc.vb.transientHandle = tvb;
	dc.vb.nVertices = nVertices;
	dc.ib.transientHandle = tib;
	dc.ib.nIndices = nIndices;
	drawCallList->push_back(dc);
}

void Main::setupEntityLighting(Entity *entity)
{
	assert(entity);

	// Trace a sample point down to find ambient light.
	vec3 lightPosition;
	
	if (entity->e.renderfx & RF_LIGHTING_ORIGIN)
	{
		// Seperate lightOrigins are needed so an object that is sinking into the ground can still be lit, and so multi-part models can be lit identically.
		lightPosition = entity->e.lightingOrigin;
	}
	else
	{
		lightPosition = entity->e.origin;
	}

	// If not a world model, only use dynamic lights (menu system, etc.)
	if (isWorldCamera && world->hasLightGrid())
	{
		world->sampleLightGrid(lightPosition, &entity->ambientLight, &entity->directedLight, &entity->lightDir);
	}
	else
	{
		entity->ambientLight = vec3(identityLight * 150);
		entity->directedLight = vec3(identityLight * 150);
	}

	// Bonus items and view weapons have a fixed minimum add.
	//if (entity->e.renderfx & RF_MINLIGHT)
	{
		// Give everything a minimum light add.
		entity->ambientLight += vec3(identityLight * 32);
	}

	// Clamp ambient.
	for (size_t i = 0; i < 3; i++)
	{
		entity->ambientLight[i] = std::min(entity->ambientLight[i], (float)identityLightByte);
	}

	// Save out the byte packet version.
	((uint8_t *)&entity->ambientLightInt)[0] = ri.ftol(entity->ambientLight[0]);
	((uint8_t *)&entity->ambientLightInt)[1] = ri.ftol(entity->ambientLight[1]);
	((uint8_t *)&entity->ambientLightInt)[2] = ri.ftol(entity->ambientLight[2]);
	((uint8_t *)&entity->ambientLightInt)[3] = 0xff;
	
	// Transform the direction to local space.
	entity->modelLightDir[0] = vec3::dotProduct(entity->lightDir, entity->e.axis[0]);
	entity->modelLightDir[1] = vec3::dotProduct(entity->lightDir, entity->e.axis[1]);
	entity->modelLightDir[2] = vec3::dotProduct(entity->lightDir, entity->e.axis[2]);
}

} // namespace renderer
