// pathfinder/src/render-context.h
//
// Copyright © 2017 The Pathfinder Project Developers.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#ifndef PATHFINDER_RENDER_CONTEXT_H
#define PATHFINDER_RENDER_CONTEXT_H

#include "platform.h"

#include "gl-utils.h"

#include <vector>
#include <memory>

namespace pathfinder {

class PathfinderShaderProgram;

class RenderContext
{
public:
  RenderContext();
  void initQuadVAO(std::map<std::string, GLint>& attributes);

  ColorAlphaFormat getColorAlphaFormat() const {
    return mColorAlphaFormat;
  }

  std::vector<std::shared_ptr<PathfinderShaderProgram>>& shaderPrograms() {
    return mShaderPrograms;
  }

  GLuint quadPositionsBuffer() {
    return mQuadPositionsBuffer;
  }
  GLuint quadElementsBuffer() {
    return mQuadElementsBuffer;
  }
  GLuint quadTexCoordsBuffer() {
    return mQuadTexCoordsBuffer;
  }

  virtual void setDirty() = 0;
    
private:
    //     /// The OpenGL context.
  
    //readonly gl: WebGLRenderingContext;
    ColorAlphaFormat mColorAlphaFormat;
    std::vector<std::shared_ptr<PathfinderShaderProgram>> mShaderPrograms;
    GLuint mQuadPositionsBuffer;
    GLuint mQuadTexCoordsBuffer;
    GLuint mQuadElementsBuffer;
};

} // namespace pathfinder

#endif // PATHFINDER_RENDER_CONTEXT_H
