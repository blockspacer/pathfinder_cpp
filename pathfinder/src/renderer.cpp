// pathfinder/src/renderer.cpp
//
// Copyright © 2017 The Pathfinder Project Developers.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#include "renderer.h"

#include "resources.h"

#include "context.h"
#include "aa-strategy.h"
#include "gl-utils.h"
#include "buffer-texture.h"
#include "meshes.h"
#include "shader-loader.h"
#include "resources.h"
#include "platform.h"

#include <assert.h>

using namespace std;
using namespace kraken;

namespace pathfinder {

Renderer::Renderer(shared_ptr<RenderContext> renderContext)
 : mRenderContext(renderContext)
 , mGammaCorrectionMode(gcm_on)
 , mImplicitCoverInteriorVAO(0)
 , mImplicitCoverCurveVAO(0)
{
}

Renderer::~Renderer()
{
  if (mImplicitCoverInteriorVAO) {
    GLDEBUG(glDeleteVertexArrays(1, &mImplicitCoverInteriorVAO));
    mImplicitCoverInteriorVAO = 0;
  }
  if (mImplicitCoverCurveVAO) {
    GLDEBUG(glDeleteVertexArrays(1, &mImplicitCoverCurveVAO));
    mImplicitCoverCurveVAO = 0;
  }
}

bool
Renderer::init(AntialiasingStrategyName aaType,
              int aaLevel,
              AAOptions aaOptions)
{
  setAntialiasingOptions(aaType, aaLevel, aaOptions);

  GLDEBUG(glGenVertexArrays(1, &mImplicitCoverInteriorVAO)); // was vertexArrayObjectExt.createVertexArrayOES()
  GLDEBUG(glGenVertexArrays(1, &mImplicitCoverCurveVAO)); // was vertexArrayObjectExt.createVertexArrayOES

  return true;
}

void
Renderer::attachMeshes(vector<shared_ptr<PathfinderPackedMeshes>>& meshes)
{
  assert(mAntialiasingStrategy);
  mMeshes = meshes;
  mMeshBuffers.clear();
  for (shared_ptr<PathfinderPackedMeshes>& m: meshes) {
    mMeshBuffers.push_back(make_unique<PathfinderPackedMeshBuffers>(*m));
  }
  mAntialiasingStrategy->attachMeshes(*mRenderContext, *this);
}


void
Renderer::renderAtlas()
{
  if (mMeshBuffers.size() == 0) {
    return;
  }

  clearDestFramebuffer();

  assert(mAntialiasingStrategy);
  mAntialiasingStrategy->prepareForRendering(*this);

  int passCount = mAntialiasingStrategy->getPassCount();
  for (int pass = 0; pass < passCount; pass++) {
    if (mAntialiasingStrategy->getDirectRenderingMode() != drm_none) {
      mAntialiasingStrategy->prepareForDirectRendering(*this);
    }

    int objectCount = getObjectCount();
    for (int objectIndex = 0; objectIndex < objectCount; objectIndex++) {
      if (mAntialiasingStrategy->getDirectRenderingMode() != drm_none) {
        // Prepare for direct rendering.
        mAntialiasingStrategy->prepareToRenderObject(*this, objectIndex);

        // Clear.
        clearForDirectRendering(objectIndex);

        // Perform direct rendering (Loop-Blinn).
        directlyRenderObject(pass, objectIndex);
      }

      // Antialias.
      mAntialiasingStrategy->antialiasObject(*this, objectIndex);

      // Perform post-antialiasing tasks.
      mAntialiasingStrategy->finishAntialiasingObject(*this, objectIndex);

      mAntialiasingStrategy->resolveAAForObject(*this, objectIndex);
    }

    mAntialiasingStrategy->resolve(pass, *this);
  }
}

void
Renderer::setAntialiasingOptions(AntialiasingStrategyName aaType,
                                 int aaLevel,
                                 AAOptions aaOptions)
{
  mGammaCorrectionMode = aaOptions.gammaCorrection;

  mAntialiasingStrategy = createAAStrategy(aaType,
                                           aaLevel,
                                           aaOptions.subpixelAA,
                                           aaOptions.stemDarkening);

  mAntialiasingStrategy->init(*this);
  if (mMeshes.size() != 0) {
    mAntialiasingStrategy->attachMeshes(*mRenderContext, *this);
  }
  mAntialiasingStrategy->setFramebufferSize(*this);
}

void
Renderer::setFramebufferSizeUniform(PathfinderShaderProgram& aProgram)
{
  if (aProgram.hasUniform(uniform_uFramebufferSize)) {
    Vector2i destAllocatedSize = getAtlasAllocatedSize();
    GLDEBUG(glUniform2i(aProgram.getUniform(uniform_uFramebufferSize),
                        destAllocatedSize[0],
                        destAllocatedSize[1]));
  }
}

void
Renderer::setTransformAndTexScaleUniformsForDest(PathfinderShaderProgram& aProgram, TileInfo* tileInfo)
{
  Vector2 usedSize = getUsedSizeFactor();
  if (aProgram.hasUniform(uniform_uTexScale)) {
    GLDEBUG(glUniform2f(aProgram.getUniform(uniform_uTexScale), usedSize[0], usedSize[1]));
  }

  if (aProgram.hasUniform(uniform_uTransform)) {
    Vector2 tileSize;
    Vector2 tilePosition;
    if (tileInfo == nullptr) {
      tileSize = Vector2::One();
      tilePosition = Vector2::Zero();
    } else {
      tileSize = Vector2::Create((float)tileInfo->size.x, (float)tileInfo->size.y);
      tilePosition = Vector2::Create((float)tileInfo->position.x, (float)tileInfo->position.y);
    }

    Matrix4 transform = Matrix4::Identity();
    transform.scale(1.0f / tileSize[0], 1.0f / tileSize[1], 1.0f);
    transform.scale(2.0f * usedSize[0], 2.0f * usedSize[1], 1.0f);
    transform.translate(
      -1.0f + tilePosition[0] / tileSize[0] * 2.0f,
      -1.0f + tilePosition[1] / tileSize[1] * 2.0f,
      0.0f
    );
    
    GLDEBUG(glUniformMatrix4fv(aProgram.getUniform(uniform_uTransform), 1, GL_FALSE, transform.c));
  }

}

void
Renderer::setTransformSTAndTexScaleUniformsForDest(PathfinderShaderProgram& aProgram)
{
  Vector2 usedSize = getUsedSizeFactor();
  if (aProgram.hasUniform(uniform_uTransformST)) {
    GLDEBUG(glUniform4f(aProgram.getUniform(uniform_uTransformST), 2.0f * usedSize[0], 2.0f * usedSize[1], -1.0f, -1.0f));
  }
  if (aProgram.hasUniform(uniform_uTexScale)) {
    GLDEBUG(glUniform2f(aProgram.getUniform(uniform_uTexScale), usedSize[0], usedSize[1]));
  }
}

void
Renderer::setTransformUniform(PathfinderShaderProgram& aProgram, int pass, int objectIndex)
{
  if (aProgram.hasUniform(uniform_uTransform)) {
    Matrix4 transform = computeTransform(pass, objectIndex);
    GLDEBUG(glUniformMatrix4fv(aProgram.getUniform(uniform_uTransform), 1, GL_FALSE, transform.c));
  }
}

void
Renderer::setTransformSTUniform(PathfinderShaderProgram& aProgram, int objectIndex)
{
  if (aProgram.hasUniform(uniform_uTransformST)) {
    // FIXME(pcwalton): Lossy conversion from a 4x4 matrix to an ST matrix is ugly and fragile.
    // Refactor.

    Matrix4 transform = computeTransform(0, objectIndex);

    GLDEBUG(glUniform4f(aProgram.getUniform(uniform_uTransformST),
                        transform[0],
                        transform[5],
                        transform[12],
                        transform[13]));
  }
}

void
Renderer::setTransformAffineUniforms(PathfinderShaderProgram& aProgram, int objectIndex)
{
  // FIXME(pcwalton): Lossy conversion from a 4x4 matrix to an affine matrix is ugly and
  // fragile. Refactor.

  Matrix4 transform = computeTransform(0, objectIndex);

  if (aProgram.hasUniform(uniform_uTransformST)) {
    GLDEBUG(glUniform4f(aProgram.getUniform(uniform_uTransformST),
                        transform[0],
                        transform[5],
                        transform[12],
                        transform[13]));
  }
  if (aProgram.hasUniform(uniform_uTransformExt)) {
    GLDEBUG(glUniform2f(aProgram.getUniform(uniform_uTransformExt), transform[1], transform[4]));
  }
}

void
Renderer::uploadPathColors(int objectCount)
{
  mPathColorsBufferTextures.resize(objectCount, nullptr);
  for (int objectIndex = 0; objectIndex < objectCount; objectIndex++) {
    vector<__uint8_t> pathColors = pathColorsForObject(objectIndex);

    shared_ptr<PathfinderBufferTexture> pathColorsBufferTexture;
    pathColorsBufferTexture = mPathColorsBufferTextures[objectIndex];
    if (pathColorsBufferTexture == nullptr) {
      pathColorsBufferTexture = make_shared<PathfinderBufferTexture>(uniform_uPathColors, uniform_uPathColorsDimensions);
      mPathColorsBufferTextures[objectIndex] = pathColorsBufferTexture;
    }
    pathColorsBufferTexture->upload(pathColors);
  }
}

void
Renderer::uploadPathTransforms(int objectCount)
{
  mPathTransformBufferTextures.resize(objectCount, nullptr);
  for (int objectIndex = 0; objectIndex < objectCount; objectIndex++) {
    shared_ptr<PathTransformBuffers<vector<float>>> pathTransforms = pathTransformsForObject(objectIndex);

    shared_ptr<PathTransformBuffers<PathfinderBufferTexture>> pathTransformBufferTextures;
    pathTransformBufferTextures = mPathTransformBufferTextures[objectIndex];
    if (pathTransformBufferTextures == nullptr) {
      pathTransformBufferTextures = make_shared<PathTransformBuffers<PathfinderBufferTexture>>(
        make_shared<PathfinderBufferTexture>(uniform_uPathTransformST, uniform_uPathTransformSTDimensions),
        make_shared<PathfinderBufferTexture>(uniform_uPathTransformExt, uniform_uPathTransformExtDimensions));
      mPathTransformBufferTextures[objectIndex] = pathTransformBufferTextures;
    }

    pathTransformBufferTextures->st->upload(*(pathTransforms->st));
    pathTransformBufferTextures->ext->upload(*(pathTransforms->ext));
  }
}

void
Renderer::setPathColorsUniform(int objectIndex, PathfinderShaderProgram& aProgram, GLuint textureUnit)
{
  int meshIndex = meshIndexForObject(objectIndex);
  mPathColorsBufferTextures[meshIndex]->bind(aProgram, textureUnit);
}

void
Renderer::setEmboldenAmountUniform(int objectIndex, PathfinderShaderProgram& aProgram)
{
  if (!aProgram.hasUniform(uniform_uEmboldenAmount)) {
    return;
  }
  Vector2 emboldenAmount = getTotalEmboldenAmount();
  GLDEBUG(glUniform2f(aProgram.getUniform(uniform_uEmboldenAmount), emboldenAmount[0], emboldenAmount[1]));
}

int
Renderer::meshIndexForObject(int objectIndex)
{
  return objectIndex;
}

Range
Renderer::pathRangeForObject(int objectIndex)
{
  if (mMeshBuffers.size() == 0) {
    return Range(0, 0);
  }
  int length = (int)mMeshBuffers[objectIndex]->bQuadVertexPositionPathRanges.size();
  return Range(1, length + 1);
}

void
Renderer::bindGammaLUT(Vector3 bgColor, GLuint textureUnit, PathfinderShaderProgram& aProgram)
{
  if (aProgram.hasUniform(uniform_uGammaLUT)) {
    GLDEBUG(glActiveTexture(GL_TEXTURE0 + textureUnit));
    GLDEBUG(glBindTexture(GL_TEXTURE_2D, mRenderContext->getGammaLUTTexture()));
    GLDEBUG(glUniform1i(aProgram.getUniform(uniform_uGammaLUT), textureUnit));
  }

  if (aProgram.hasUniform(uniform_uBGColor)) {
    GLDEBUG(glUniform3f(aProgram.getUniform(uniform_uBGColor), bgColor[0], bgColor[1], bgColor[2]));
  }
}

void
Renderer::bindAreaLUT(GLuint textureUnit, PathfinderShaderProgram& aProgram)
{
  if (aProgram.hasUniform(uniform_uAreaLUT)) {
    GLDEBUG(glActiveTexture(GL_TEXTURE0 + textureUnit));
    GLDEBUG(glBindTexture(GL_TEXTURE_2D, mRenderContext->getAreaLUTTexture()));
    GLDEBUG(glUniform1i(aProgram.getUniform(uniform_uAreaLUT), textureUnit));
  }
}

void
Renderer::clearDestFramebuffer()
{
  Vector4 clearColor = getBGColor();
  Vector2i destAllocatedSize = getAtlasAllocatedSize();
  GLDEBUG(glBindFramebuffer(GL_FRAMEBUFFER, getAtlasFramebuffer()));
  GLDEBUG(glDepthMask(GL_TRUE));
  GLDEBUG(glViewport(0, 0, destAllocatedSize[0], destAllocatedSize[1]));
  GLDEBUG(glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]));
  GLDEBUG(glClearDepth(0.0));
  GLDEBUG(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
}

void
Renderer::clearForDirectRendering(int objectIndex)
{
  DirectRenderingMode renderingMode = mAntialiasingStrategy->getDirectRenderingMode();

  Vector4 clearColor = clearColorForObject(objectIndex);
  // FINDME!! KIP!! HACK!! -- RE-enable below, and make clear color optional...
  //
  // if (clearColor == Vector4::Zero()) {
  //   return;
  // }

  GLDEBUG(glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]));
  GLDEBUG(glClearDepth(0.0));
  GLDEBUG(glDepthMask(GL_TRUE));
  GLDEBUG(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
}

Matrix4
Renderer::getModelviewTransform(int pathIndex)
{
  return Matrix4::Identity();
}

Range
Renderer::instanceRangeForObject(int objectIndex)
{
  return Range(0, 1);
}

shared_ptr<PathTransformBuffers<vector<float>>>
Renderer::createPathTransformBuffers(int pathCount)
{
  pathCount += 1;
  return make_shared<PathTransformBuffers<vector<float>>>(
    make_shared<vector<float>>(pathCount * 4),
    make_shared<vector<float>>((pathCount + (pathCount & 1)) * 2));
}

void
Renderer::directlyRenderObject(int pass, int objectIndex)
{
  if (mMeshBuffers.size() == 0 || mMeshes.size() == 0) {
    return;
  }

  DirectRenderingMode renderingMode = mAntialiasingStrategy->getDirectRenderingMode();
  int objectCount = getObjectCount();

  Range instanceRange = instanceRangeForObject(objectIndex);
  if (instanceRange.isEmpty()) {
    return;
  }

  Range pathRange = pathRangeForObject(objectIndex);
  int meshIndex = meshIndexForObject(objectIndex);

  shared_ptr<PathfinderPackedMeshBuffers> meshes = mMeshBuffers[meshIndex];
  shared_ptr<PathfinderPackedMeshes> meshData = mMeshes[meshIndex];

  // Set up implicit cover state.
  GLDEBUG(glDepthFunc(GL_GREATER));
  GLDEBUG(glDepthMask(GL_TRUE));
  GLDEBUG(glEnable(GL_DEPTH_TEST));
  GLDEBUG(glDisable(GL_BLEND));
  GLDEBUG(glCullFace(GL_BACK));
  GLDEBUG(glFrontFace(GL_CCW));
  GLDEBUG(glEnable(GL_CULL_FACE));

  // Set up the implicit cover interior VAO.
  ProgramID directInteriorProgramName = getDirectInteriorProgramName(renderingMode);
  shared_ptr<PathfinderShaderProgram> directInteriorProgram = mRenderContext->getShaderManager().getProgram(directInteriorProgramName);
  GLDEBUG(glBindVertexArray(mImplicitCoverInteriorVAO)); // was vertexArrayObjectExt.bindVertexArrayOES
  initImplicitCoverInteriorVAO(objectIndex, instanceRange, renderingMode);

  // Draw direct interior parts.
  if (renderingMode == drm_conservative) {
    setTransformAffineUniforms(*directInteriorProgram, objectIndex);
  } else {
    setTransformUniform(*directInteriorProgram, pass, objectIndex);
  }
  setFramebufferSizeUniform(*directInteriorProgram);
  setHintsUniform(*directInteriorProgram);
  setPathColorsUniform(objectIndex, *directInteriorProgram, 0);
  setEmboldenAmountUniform(objectIndex, *directInteriorProgram);
  mPathTransformBufferTextures[meshIndex]->st->bind(*directInteriorProgram, 1);
  mPathTransformBufferTextures[meshIndex]->ext->bind(*directInteriorProgram, 2);
  Range bQuadInteriorRange = getMeshIndexRange(meshes->bQuadVertexInteriorIndexPathRanges,
                                               pathRange);
  if (!getPathIDsAreInstanced()) {
    GLDEBUG(glDrawElements(GL_TRIANGLES,
                   bQuadInteriorRange.length(),
                   GL_UNSIGNED_INT,
                   (GLvoid*)(bQuadInteriorRange.start * sizeof(__uint32_t))));
  } else {
    GLDEBUG(glDrawElementsInstanced(GL_TRIANGLES,
                                    bQuadInteriorRange.length(),
                                    GL_UNSIGNED_INT,
                                    0,
                                    instanceRange.length()
                                    )); // was instancedArraysExt.drawElementsInstancedANGLE
  }

  GLDEBUG(glDisable(GL_CULL_FACE));

  // Render curves, if applicable.
  if (renderingMode != drm_conservative) {
      // Set up direct curve state.
      GLDEBUG(glDepthMask(GL_FALSE));
      GLDEBUG(glEnable(GL_BLEND));
      GLDEBUG(glBlendEquation(GL_FUNC_ADD));
      GLDEBUG(glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE));

      // Set up the direct curve VAO.
      //
      // TODO(pcwalton): Cache these.
      ProgramID directCurveProgramName = getDirectCurveProgramName();
      shared_ptr<PathfinderShaderProgram> directCurveProgram = mRenderContext->getShaderManager().getProgram(directCurveProgramName);
      // was vertexArrayObjectExt.bindVertexArrayOES
      GLDEBUG(glBindVertexArray(mImplicitCoverCurveVAO));
      initImplicitCoverCurveVAO(objectIndex, instanceRange);

      // Draw direct curve parts.
      setTransformUniform(*directCurveProgram, pass, objectIndex);
      setFramebufferSizeUniform(*directCurveProgram);
      setHintsUniform(*directCurveProgram);
      setPathColorsUniform(objectIndex, *directCurveProgram, 0);
      setEmboldenAmountUniform(objectIndex, *directCurveProgram);
      mPathTransformBufferTextures[meshIndex]
          ->st
          ->bind(*directCurveProgram, 1);
      mPathTransformBufferTextures[meshIndex]
          ->ext
          ->bind(*directCurveProgram, 2);
      Range coverCurveRange = getMeshIndexRange(meshes->bQuadVertexPositionPathRanges,
                                                pathRange);
      if (!getPathIDsAreInstanced()) {
        GLDEBUG(glDrawArrays(GL_TRIANGLES, coverCurveRange.start * 6, coverCurveRange.length() * 6));
      } else {
        // was instancedArraysExt.drawArraysInstancedANGLE
        GLDEBUG(glDrawArraysInstanced(GL_TRIANGLES, 0, coverCurveRange.length() * 6, instanceRange.length()));
      }
  }

  // was vertexArrayObjectExt.bindVertexArrayOES
  GLDEBUG(glBindVertexArray(0));

  // Finish direct rendering. Right now, this performs compositing if necessary.
  mAntialiasingStrategy->finishDirectlyRenderingObject(*this, objectIndex);
}

void
Renderer::initImplicitCoverCurveVAO(int objectIndex, Range instanceRange)
{
  if (mMeshBuffers.size() == 0) {
    return;
  }

  int meshIndex = meshIndexForObject(objectIndex);
  shared_ptr<PathfinderPackedMeshBuffers> meshes = mMeshBuffers[meshIndex];
  shared_ptr<PathfinderPackedMeshes> meshData = mMeshes[meshIndex];

  ProgramID directCurveProgramName = getDirectCurveProgramName();
  shared_ptr<PathfinderShaderProgram> directCurveProgram = mRenderContext->getShaderManager().getProgram(directCurveProgramName);
  GLDEBUG(glUseProgram(directCurveProgram->getProgram()));
  GLDEBUG(glBindBuffer(GL_ARRAY_BUFFER, meshes->bQuadVertexPositions));
  GLDEBUG(glVertexAttribPointer(directCurveProgram->getAttribute(attribute_aPosition), 2, GL_FLOAT, GL_FALSE, 0, 0));
  GLDEBUG(glBindBuffer(GL_ARRAY_BUFFER, mRenderContext->getVertexIDVBO()));
  GLDEBUG(glVertexAttribPointer(directCurveProgram->getAttribute(attribute_aVertexID), 1, GL_FLOAT, GL_FALSE, 0, 0));

  if (getPathIDsAreInstanced()) {
    GLDEBUG(glBindBuffer(GL_ARRAY_BUFFER, mRenderContext->getInstancedPathIDVBO()));
  } else {
    GLDEBUG(glBindBuffer(GL_ARRAY_BUFFER, meshes->bQuadVertexPositionPathIDs));
  }
  GLDEBUG(glVertexAttribPointer(directCurveProgram->getAttribute(attribute_aPathID),
                                1,
                                GL_UNSIGNED_SHORT,
                                GL_FALSE,
                                0,
                                (GLvoid*)(instanceRange.start * sizeof(__uint16_t))));
  if (getPathIDsAreInstanced()) {
    // was instancedArraysExt.vertexAttribDivisorANGLE
    GLDEBUG(glVertexAttribDivisor(directCurveProgram->getAttribute(attribute_aPathID), 1));
  }

  GLDEBUG(glEnableVertexAttribArray(directCurveProgram->getAttribute(attribute_aPosition)));
  GLDEBUG(glEnableVertexAttribArray(directCurveProgram->getAttribute(attribute_aVertexID)));
  GLDEBUG(glEnableVertexAttribArray(directCurveProgram->getAttribute(attribute_aPathID)));
}


void
Renderer::initImplicitCoverInteriorVAO(int objectIndex, Range instanceRange, DirectRenderingMode renderingMode)
{
  if (mMeshBuffers.size() == 0) {
    return;
  }

  int meshIndex = meshIndexForObject(objectIndex);
  shared_ptr<PathfinderPackedMeshBuffers> meshes = mMeshBuffers[meshIndex];

  ProgramID directInteriorProgramName = getDirectInteriorProgramName(renderingMode);
  shared_ptr<PathfinderShaderProgram> directInteriorProgram = mRenderContext->getShaderManager().getProgram(directInteriorProgramName);
  GLDEBUG(glUseProgram(directInteriorProgram->getProgram()));
  GLDEBUG(glBindBuffer(GL_ARRAY_BUFFER, meshes->bQuadVertexPositions));
  GLDEBUG(glVertexAttribPointer(directInteriorProgram->getAttribute(attribute_aPosition),
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        0,
                        0));

  if (getPathIDsAreInstanced()) {
    GLDEBUG(glBindBuffer(GL_ARRAY_BUFFER, mRenderContext->getInstancedPathIDVBO()));
  } else {
    GLDEBUG(glBindBuffer(GL_ARRAY_BUFFER, meshes->bQuadVertexPositionPathIDs));
  }
  GLDEBUG(glVertexAttribPointer(directInteriorProgram->getAttribute(attribute_aPathID),
                        1,
                        GL_UNSIGNED_SHORT,
                        GL_FALSE,
                        0,
                        (GLvoid*)(instanceRange.start * sizeof(__uint16_t))));
  if (getPathIDsAreInstanced()) {
    // was instancedArraysExt.vertexAttribDivisorANGLE
    GLDEBUG(glVertexAttribDivisor(directInteriorProgram->getAttribute(attribute_aPathID), 1));
  }

  if (directInteriorProgramName == program_conservativeInterior) {
    GLDEBUG(glBindBuffer(GL_ARRAY_BUFFER, mRenderContext->getVertexIDVBO()));
    GLDEBUG(glVertexAttribPointer(directInteriorProgram->getAttribute(attribute_aVertexID),
                          1,
                          GL_FLOAT,
                          GL_FALSE,
                          0,
                          0));
  }

  GLDEBUG(glEnableVertexAttribArray(directInteriorProgram->getAttribute(attribute_aPosition)));
  GLDEBUG(glEnableVertexAttribArray(directInteriorProgram->getAttribute(attribute_aPathID)));
  if (directInteriorProgramName == program_conservativeInterior) {
    GLDEBUG(glEnableVertexAttribArray(directInteriorProgram->getAttribute(attribute_aVertexID)));
  }
  GLDEBUG(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, meshes->bQuadVertexInteriorIndices));
}

kraken::Matrix4 Renderer::computeTransform(int pass, int objectIndex)
{
  Matrix4 transform;
  if (mAntialiasingStrategy) {
    transform = mAntialiasingStrategy->getWorldTransformForPass(*this, pass);
  } else {
    transform = Matrix4::Identity();
  }
  transform *= getModelviewTransform(objectIndex);
  transform *= getWorldTransform();
  
  return transform;
}

Range getMeshIndexRange(const vector<Range>& indexRanges, Range pathRange)
{
  if (indexRanges.size() == 0) {
    return Range(0, 0);
  }

  Range lastIndexRange = indexRanges.back();
  bool descending = indexRanges[0].start > lastIndexRange.start;

  pathRange = Range(pathRange.start - 1, pathRange.end - 1);

  int startIndex;
  if (pathRange.start >= indexRanges.size()) {
    startIndex = lastIndexRange.end;
  } else if (!descending) {
    startIndex = indexRanges[pathRange.start].start;
  } else {
    startIndex = indexRanges[pathRange.start].end;
  }

  int endIndex;
  if (descending) {
      endIndex = indexRanges[pathRange.end - 1].start;
  } else if (pathRange.end >= indexRanges.size()) {
      endIndex = lastIndexRange.end;
  } else {
      endIndex = indexRanges[pathRange.end].start;
  }

  if (descending) {
      int tmp = startIndex;
      startIndex = endIndex;
      endIndex = tmp;
  }

  return Range(startIndex, endIndex);
}

} // namepsace pathfinder

