#ifndef PATHFINDER_RENDERER_H
#define PATHFINDER_RENDERER_H

#include <GL/glew.h>
#include <kraken-math.h>
#include <vector>
#include <memory>

#include "utils.h"

namespace pathfinder {

const int MAX_PATHS = 65535;

const int MAX_VERTICES = 4 * 1024 * 1024;

const int TIME_INTERVAL_DELAY = 32;

const int B_LOOP_BLINN_DATA_SIZE = 4;
const int B_LOOP_BLINN_DATA_TEX_COORD_OFFSET = 0;
const int B_LOOP_BLINN_DATA_SIGN_OFFSET = 2;

template <class T>
struct PathTransformBuffers {
  PathTransformBuffers(std::shared_ptr<T> aSt, std::shared_ptr<T> aExt)
   : st(aSt)
   , ext(aExt)
  { }
  std::shared_ptr<T> st;
  std::shared_ptr<T> ext;
};

class RenderContext;
class PathfinderBufferTexture;
class PathfinderPackedMeshBuffers;
class PathfinderPackedMeshes;
class UniformMap;

class Renderer
{
public:
  Renderer(std::shared_ptr<RenderContext> renderContext);
  ~Renderer();
  std::shared_ptr<RenderContext> getRenderContext() const {
    return mRenderContext;
  }
  kraken::Vector2 getEmboldenAmount() const {
    return kraken::Vector2::Zero();
  }
  kraken::Vector4 getBGColor() const {
    return kraken::Vector4::One();
  }
  kraken::Vector4 getFGColor() const {
    return kraken::Vector4::Zero();
  }
  kraken::Vector4 getBackgroundColor() const {
    return kraken::Vector4::One();
  }
  bool getMeshesAttached() const {
    return mMeshBuffers.size() > 0 && mMeshes.size() > 0;
  }
  virtual bool getIsMulticolor() const = 0;
  virtual bool getNeedsStencil() const = 0;
  virtual GLuint getDestFramebuffer() const = 0;
  virtual kraken::Vector2 getDestAllocatedSize() const = 0;
  virtual kraken::Vector2 getDestUsedSize() const = 0;

  void attachMeshes(std::vector<std::shared_ptr<PathfinderPackedMeshes>>& meshes);

  virtual std::vector<float> pathBoundingRects(int objectIndex) = 0;
  virtual void setHintsUniform(const UniformMap& uniforms) = 0;
  void redraw();

  void setAntialiasingOptions(AntialiasingStrategyName aaType,
                              int aaLevel,
                              AAOptions aaOptions);
  void canvasResized();
  void setFramebufferSizeUniform(const UniformMap& uniforms);
  void setTransformAndTexScaleUniformsForDest(const UniformMap& uniforms, TileInfo* tileInfo);
  void setTransformSTAndTexScaleUniformsForDest(const UniformMap& uniforms);
  void setTransformUniform(const UniformMap& uniforms, int pass, int objectIndex);
  void setTransformSTUniform(const UniformMap& uniforms, int objectIndex);
  void setTransformAffineUniforms(const UniformMap& uniforms, int objectIndex);
  void uploadPathColors(int objectCount);
  void uploadPathTransforms(int objectCount);
  void setPathColorsUniform(int objectIndex, const UniformMap& uniforms, GLuint textureUnit);
  void setEmboldenAmountUniform(int objectIndex, const UniformMap& uniforms);
  int meshIndexForObject(int objectIndex);
  Range pathRangeForObject(int objectIndex);

protected:
  std::shared_ptr<AntialiasingStrategy> mAntialiasingStrategy;
  std::vector<std::shared_ptr<PathfinderBufferTexture>> mPathColorsBufferTextures;
  GammaCorrectionMode mGammaCorrectionMode;
  bool getPathIDsAreInstanced() {
    return false;
  }

  virtual int getObjectCount() const = 0;
  virtual kraken::Vector2 getUsedSizeFactor() const = 0;
  virtual kraken::Matrix4 getWorldTransform() const = 0;
  kraken::Vector4 clearColorForObject(int objectIndex) {
    return kraken::Vector4::Zero();
  }
  void bindGammaLUT(kraken::Vector3 bgColor, GLuint textureUnit, const UniformMap& uniforms);
  virtual std::shared_ptr<AntialiasingStrategy> createAAStrategy(AntialiasingStrategyName aaType,
                                        int aaLevel,
                                        SubpixelAAType subpixelAA,
                                        StemDarkeningMode stemDarkening) = 0;
    virtual void compositeIfNecessary() = 0;
    virtual std::vector<__uint8_t> pathColorsForObject(int objectIndex) = 0;
    virtual std::shared_ptr<PathTransformBuffers<std::vector<__uint8_t>>> pathTransformsForObject(int objectIndex) = 0;

    virtual int directCurveProgramName() = 0;
    virtual int directInteriorProgramName(DirectRenderingMode renderingMode) = 0;

    virtual void drawSceneryIfNecessary() {}
    void clearDestFramebuffer();
    void clearForDirectRendering(int objectIndex);
    kraken::Matrix4 getModelviewTransform(int pathIndex);

    /// If non-instanced, returns instance 0. An empty range skips rendering the object entirely.
    Range instanceRangeForObject(int objectIndex);

    std::shared_ptr<PathTransformBuffers<std::vector<float>>> createPathTransformBuffers(int pathCount);

private:

  void directlyRenderObject(int pass, int objectIndex);

  std::shared_ptr<RenderContext> mRenderContext;
  std::vector<std::shared_ptr<PathTransformBuffers<PathfinderBufferTexture>>> mPathTransformBufferTextures;
  std::vector<std::shared_ptr<PathfinderPackedMeshBuffers>> mMeshBuffers;
  std::vector<std::shared_ptr<PathfinderPackedMeshes>> mMeshes;


  GLuint mImplicitCoverInteriorVAO;
  GLuint mImplicitCoverCurveVAO;
  GLuint mGammaLUTTexture;
  GLuint mInstancedPathIDVBO;
  GLuint mVertexIDVBO;
};

} // namespace pathfinder

#endif // PATHFINDER_RENDERER_H
