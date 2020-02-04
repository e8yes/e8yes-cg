#ifndef GEOMETRY_H
#define GEOMETRY_H

#include "obj.h"
#include "tensor.h"
#include <memory>
#include <string>
#include <vector>

namespace e8 {

typedef e8util::vec<3, unsigned> triangle;

class if_geometry : public if_operable_obj<if_geometry> {
  public:
    if_geometry(std::string const &name);
    virtual ~if_geometry() override;

    obj_protocol protocol() const override;

    /**
     * @brief vertices
     * @return
     */
    virtual std::vector<e8util::vec3> const &vertices() const = 0;

    /**
     * @brief normals
     * @return
     */
    virtual std::vector<e8util::vec3> const &normals() const = 0;

    /**
     * @brief texcoords Texture coordinate is optional for a geometry. It may return an empty
     * vector.
     * @return An array of texture coordinate if specified.
     */
    virtual std::vector<e8util::vec2> const &texcoords() const = 0;

    virtual std::vector<triangle> const &triangles() const = 0;

    struct surface_sample {
        e8util::vec3 p;  // Spatial position on the sampled surface.
        e8util::vec3 n;  // Normal vector at p.
        float area_dens; // Area probability density of the sample.
    };

    virtual surface_sample sample(e8util::rng *rng) const = 0;
    virtual float surface_area() const = 0;
    virtual e8util::aabb aabb() const = 0;
    virtual std::unique_ptr<if_geometry> copy() const override = 0;
    virtual std::unique_ptr<if_geometry> transform(e8util::mat44 const &trans) const override = 0;

  protected:
    if_geometry(obj_id_t id, std::string const &name);
};

class trimesh : public if_geometry {
  public:
    trimesh();
    trimesh(trimesh const &mesh);
    trimesh(std::string const &name);
    virtual ~trimesh() override;

    std::vector<e8util::vec3> const &vertices() const override;
    std::vector<e8util::vec3> const &normals() const override;
    std::vector<e8util::vec2> const &texcoords() const override;
    std::vector<triangle> const &triangles() const override;
    surface_sample sample(e8util::rng *rng) const override;
    float surface_area() const override;
    virtual e8util::aabb aabb() const override;
    std::unique_ptr<if_geometry> copy() const override;
    std::unique_ptr<if_geometry> transform(e8util::mat44 const &trans) const override;

    void vertices(std::vector<e8util::vec3> const &v);
    void normals(std::vector<e8util::vec3> const &n);
    void texcoords(std::vector<e8util::vec2> const &t);
    void triangles(std::vector<triangle> const &t);

    void update();

  protected:
    void update_aabb();
    void update_face_cdf();

    std::vector<e8util::vec3> m_verts;
    std::vector<e8util::vec3> m_norms;
    std::vector<e8util::vec2> m_texcoords;
    std::vector<triangle> m_tris;
    e8util::aabb m_aabb;
    std::vector<float> m_cum_area;
    float m_area;
};

class triangle_fragment : public trimesh {
  public:
    triangle_fragment(std::string const &name, e8util::vec3 const &a, e8util::vec3 const &b,
                      e8util::vec3 const &c);
    ~triangle_fragment();
};

class uv_sphere : public trimesh {
  public:
    uv_sphere(std::string const &name, e8util::vec3 const &o, float r, unsigned const res,
              bool flip_normal = false);
    ~uv_sphere();
};

} // namespace e8

#endif // GEOMETRY_H
