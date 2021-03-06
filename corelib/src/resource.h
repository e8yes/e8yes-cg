#ifndef RESOURCE_H
#define RESOURCE_H

#include "camera.h"
#include "geometry.h"
#include "light.h"
#include "material.h"
#include "obj.h"
#include <exception>
#include <string>
#include <vector>

namespace e8util {

class res_io_exception : public std::exception {
  public:
    res_io_exception(std::string const &cause);
    char const *what() const noexcept override;

  private:
    std::string m_cause;
};

class if_resource {
  public:
    if_resource();
    virtual ~if_resource();
    virtual std::vector<std::shared_ptr<e8::if_obj>> load_roots();
    virtual void save_roots(std::vector<std::shared_ptr<e8::if_obj>> const &roots);
};

class wavefront_obj : public if_resource {
  public:
    wavefront_obj(std::string const &location);
    ~wavefront_obj() override;

    std::vector<std::shared_ptr<e8::if_obj>> load_roots() override;
    void save_roots(std::vector<std::shared_ptr<e8::if_obj>> const &roots) override;
    std::shared_ptr<e8::if_geometry> load_geometry() const;
    bool save_geometry(e8::if_geometry const *geo);

  private:
    std::string m_location;
};

class cornell_scene : public if_resource {
  public:
    cornell_scene();
    std::vector<std::shared_ptr<e8::if_obj>> load_roots() override;
};

class gltf_scene_internal;

/**
 * @brief The gltf_scene class
 * The implementation of this class is an adaptation of the work
 * done in tinygltf
 (https://github.com/syoyo/tinygltf/blob/326d7ea310497cedd7f778426df8af983330499b/examples/raytrace/gltf-loader.cc).
 *
 * MIT License
 *
 * Copyright (c) 2019 Chifeng Wen
 * Copyright (c) 2017 Syoyo Fujita, Aurélien Chatelain and many contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
class gltf_scene : public if_resource {
  public:
    gltf_scene(std::string const &location);
    ~gltf_scene() override;

    std::vector<std::shared_ptr<e8::if_material>> load_materials() const;
    std::vector<std::shared_ptr<e8::if_light>> load_lights() const;
    std::vector<std::shared_ptr<e8::if_light>> load_virtual_lights() const;

    std::vector<std::shared_ptr<e8::if_obj>> load_roots() override;

  private:
    std::unique_ptr<gltf_scene_internal> m_pimpl;
};

} // namespace e8util

#endif // RESOURCE_H
