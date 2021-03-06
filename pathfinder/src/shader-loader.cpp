// pathfinder/src/shader-loader.cpp
//
// Copyright © 2017 The Pathfinder Project Developers.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.
#include "shader-loader.h"

#include <assert.h>

using namespace std;

namespace pathfinder {

ShaderManager::ShaderManager()
{
  mPrograms.resize(program_count, nullptr);
}

ShaderManager::~ShaderManager()
{
}

GLuint
ShaderManager::loadShader(const char* aName, const char* aSource, GLenum aType)
{
  const char* shader_source[2];
  shader_source[0] = shader_common;
  shader_source[1] = aSource;

  GLuint shader = 0;

  GLDEBUG(shader = glCreateShader(aType));
  GLDEBUG(glShaderSource(shader, 2, shader_source, NULL));
  GLDEBUG(glCompileShader(shader));

  // Report any compile issues to stderr
  GLint compile_success = GL_FALSE;
  GLDEBUG(glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_success));

  if (!compile_success) {
    // Report any compile issues to stderr
    fprintf(stderr, "Failed to compile %s shader: %s\n",
            aType == GL_VERTEX_SHADER ? "vertex": "fragment",
            aName);
    GLsizei logLength = 0; // In case glGetShaderiv fails
    GLDEBUG(glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength));
    if (logLength > 0) {
      GLchar *log = (GLchar *)malloc(logLength + 1);
      assert(log != NULL);
      log[0] = '\0'; // In case glGetProgramInfoLog fails
      GLDEBUG(glGetShaderInfoLog(shader, logLength, &logLength, log));
      log[logLength] = '\0';
      fprintf(stderr, "Shader compile log:\n%s", log);
      free(log);
    }
    GLDEBUG(glDeleteShader(shader));
    shader = 0;
  }


  return shader;
}

bool
ShaderManager::init()
{
  bool succeeded = true;
  GLuint vertexShaders[vs_count];
  GLuint fragmentShaders[fs_count];
  memset(vertexShaders, 0, sizeof(vertexShaders));
  memset(fragmentShaders, 0, sizeof(fragmentShaders));

  for (int i = 0; i < vs_count && succeeded; i++) {
    vertexShaders[i] = loadShader(VertexShaderNames[i],
                                  VertexShaderSource[i],
                                  GL_VERTEX_SHADER);
    if (vertexShaders[i] == 0) {
      succeeded = false;
    }
  }
  for (int i = 0; i < fs_count && succeeded; i++) {
    fragmentShaders[i] = loadShader(FragmentShaderNames[i],
                                  FragmentShaderSource[i],
                                  GL_FRAGMENT_SHADER);
    if (fragmentShaders[i] == 0) {
      succeeded = false;
    }
  }

  for (int i = 0; i < program_count && succeeded; i++) {
    shared_ptr<PathfinderShaderProgram> program = make_shared<PathfinderShaderProgram>();
    if(program->load(PROGRAM_INFO[i].name,
                     vertexShaders[PROGRAM_INFO[i].vertex],
                     fragmentShaders[PROGRAM_INFO[i].fragment])) {
      mPrograms[i] = program;
    } else {
      succeeded = false;
    }
  }

  for(int i = 0; i < vs_count; i++) {
    GLDEBUG(glDeleteShader(vertexShaders[i]));
  }
  for(int i = 0; i < fs_count; i++) {
    GLDEBUG(glDeleteShader(fragmentShaders[i]));
  }
  return succeeded;
}

std::shared_ptr<PathfinderShaderProgram>
ShaderManager::getProgram(ProgramID aProgramID)
{
  assert(aProgramID >= 0);
  assert(aProgramID < program_count);

  return mPrograms[aProgramID];
}

PathfinderShaderProgram::PathfinderShaderProgram()
  : mProgram(0)
{
  for (int i = 0; i < uniform_count; i++) {
    mUniforms[i] = -1;
  }
}

PathfinderShaderProgram::~PathfinderShaderProgram()
{
  if (mProgram) {
    GLDEBUG(glDeleteProgram(mProgram));
  }
}

bool
PathfinderShaderProgram::load(const char* aProgramName,
                              GLuint aVertexShader,
                              GLuint aFragmentShader)
{
  // TODO(kearwood) replace stderr output with logging function
  mProgramName = aProgramName;
  GLDEBUG(mProgram = glCreateProgram());
  GLDEBUG(glAttachShader(mProgram, aVertexShader));
  GLDEBUG(glAttachShader(mProgram, aFragmentShader));
  GLDEBUG(glLinkProgram(mProgram));

  // Report any link issues to stderr
  GLint link_success = GL_FALSE;
  GLDEBUG(glGetProgramiv(mProgram, GL_LINK_STATUS, &link_success));
  
  if (!link_success) {
    // Report any linking issues to stderr
    fprintf(stderr, "Failed to link shader program: %s\n", aProgramName);
    GLsizei logLength = 0; // In case glGetProgramiv fails
    GLDEBUG(glGetProgramiv(mProgram, GL_INFO_LOG_LENGTH, &logLength));
    if (logLength > 0) {
      GLchar *log = (GLchar *)malloc(logLength + 1);
      assert(log != NULL);
      log[0] = '\0'; // In case glGetProgramInfoLog fails
      GLDEBUG(glGetProgramInfoLog(mProgram, logLength, &logLength, log));
      log[logLength] = '\0';
      fprintf(stderr, "Program link log:\n%s", log);
      free(log);
    }
    GLDEBUG(glDeleteProgram(mProgram));
    mProgram = 0;
    return false;
  }

  for (int i = 0; i < uniform_count; i++) {
    GLDEBUG(mUniforms[i] = glGetUniformLocation(mProgram, UNIFORM_NAMES[i]));
  }
  for (int i = 0; i < attribute_count; i++) {
    GLDEBUG(mAttributes[i] = glGetAttribLocation(mProgram, ATTRIBUTE_NAMES[i]));
  }

  return true;
}

GLint
PathfinderShaderProgram::getUniform(UniformID aUniformID)
{
  if (aUniformID < 0 || aUniformID >= uniform_count) {
    assert(false);
    return -1;
  }
  assert(mUniforms[aUniformID] != -1);
  return mUniforms[aUniformID];
}

GLint
PathfinderShaderProgram::getAttribute(AttributeID aAttributeID)
{
  if (aAttributeID < 0 || aAttributeID >= attribute_count) {
    assert(false);
    return -1;
  }
  assert(mAttributes[aAttributeID] != -1);
  return mAttributes[aAttributeID];
}

bool
PathfinderShaderProgram::hasUniform(UniformID aUniformID)
{
  if (aUniformID < 0 || aUniformID >= uniform_count) {
    return false;
  }
  return mUniforms[aUniformID] != -1;
}

bool
PathfinderShaderProgram::hasAttribute(AttributeID aAttributeID)
{
  if (aAttributeID < 0 || aAttributeID >= attribute_count) {
    return false;
  }
  return mAttributes[aAttributeID] != -1;
}



} // namepsace pathfinder
