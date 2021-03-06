#include "pathtracer.h"
#include "light.h"
#include "lightsources.h"
#include <iostream>

namespace {

/**
 * @brief The sampled_pathlet struct
 * Element of the smallest parition of a path.
 */
struct sampled_pathlet {
    // The path vector pointing away from the vertex. Note that the vector's direction does not
    // represent light path's traveling direction.
    e8util::vec3 v;

    // The density which this pathlet is being selected, conditioned on all previous pathlets.
    float dens = 0.0f;

    // The vertex to anchor the vector in space. Note that, the end of the vector is anchored rather
    // than the beginning.
    e8::intersect_info vert;

    e8::if_light const *light = nullptr;

    sampled_pathlet() {}

    sampled_pathlet(e8util::vec3 const &away, e8::intersect_info const &vert,
                    e8::if_light const *light, float dens)
        : v(away), dens(dens), vert(vert), light(light) {}

    e8util::vec3 towards_prev() const { return v; }
    e8util::vec3 towards() const { return -v; }

    e8util::vec3 sample_brdf(e8util::rng *rng, float *dens,
                             e8::if_material_container const &mats) const {
        e8::if_material const &mat = mats.find(vert.geo->material_id());
        return mat.sample(rng, dens, vert.uv, vert.normal, towards_prev());
    }
};

e8util::vec3 sample_brdf(e8util::rng *rng, float *dens, e8::intersect_info const &vert,
                         e8util::vec3 const &o, e8::if_material_container const &mats) {
    e8::if_material const &mat = mats.find(vert.geo->material_id());
    return mat.sample(rng, dens, vert.uv, vert.normal, o);
}

e8util::color3 brdf(e8::intersect_info const &vert, e8util::vec3 const &o, e8util::vec3 const &i,
                    e8::if_material_container const &mats) {
    e8::if_material const &mat = mats.find(vert.geo->material_id());
    return mat.eval(vert.uv, vert.normal, o, i);
}

e8util::color3 projected_brdf(sampled_pathlet const &current, sampled_pathlet const &next,
                              e8::if_material_container const &mats) {
    e8::if_material const &mat = mats.find(current.vert.geo->material_id());
    float cos_w = current.vert.normal.inner(next.towards());
    return mat.eval(current.vert.uv, current.vert.normal, next.towards(), current.towards_prev()) *
           cos_w;
}

e8util::color3 projected_adjoint_brdf(sampled_pathlet const &current, sampled_pathlet const &next,
                                      e8::if_material_container const &mats) {
    e8::if_material const &mat = mats.find(current.vert.geo->material_id());
    float cos_w = current.vert.normal.inner(next.towards());
    return mat.eval(current.vert.uv, current.vert.normal, current.towards_prev(), next.towards()) *
           cos_w;
}

/**
 * @brief sample_path Recursively sample then concatenate pathlets to form a path sample.
 */
unsigned sample_path(e8util::rng *rng, sampled_pathlet *sampled_path,
                     e8::if_path_space const &path_space, e8::if_material_container const &mats,
                     unsigned depth, unsigned max_depth) {
    if (depth == max_depth)
        return depth;

    float w_dens;
    e8util::vec3 i = sampled_path[depth - 1].sample_brdf(rng, &w_dens, mats);
    if (e8util::equals(w_dens, 0.0f)) {
        return depth;
    }

    e8::intersect_info next_vert =
        path_space.intersect(e8util::ray(sampled_path[depth - 1].vert.vertex, i));
    if (next_vert.valid() && next_vert.normal.inner(-i) > 0) {
        sampled_path[depth] =
            sampled_pathlet(-i, next_vert, /*light=*/nullptr,
                            w_dens); // Only the first hit requires to check light hit.
        return sample_path(rng, sampled_path, path_space, mats, depth + 1, max_depth);
    } else {
        return depth;
    }
}

/**
 * @brief sample_path Sample a path X conditioned on X0 = r0 and max_depth.
 * @param rng Random number generator.
 * @param sampled_path Result, path sample.
 * @param r0 The bootstrap path to condition on.
 * @param dens0 The density of the pathlet r0.
 * @param path_space The path space to sample from.
 * @param mats Material container.
 * @param max_depth Maximum path length condition.
 * @return Actual path length of the sampled_path. It may not be max_depth in the case when the
 * light escapes out of the path_space during sampling.
 */
unsigned sample_path(e8util::rng *rng, sampled_pathlet *sampled_path, e8util::ray const &r0,
                     float dens0, e8::if_path_space const &path_space,
                     e8::if_material_container const &mats, unsigned max_depth) {
    e8::intersect_info const &vert0 = path_space.intersect(r0);
    if (!vert0.valid() || vert0.normal.inner(-r0.v()) <= 0.0f || max_depth == 0) {
        return 0;
    } else {
        sampled_path[0] = sampled_pathlet(-r0.v(), vert0, /*light=*/nullptr,
                                          dens0); // Only the first hit requires to check light hit.
        return sample_path(rng, sampled_path, path_space, mats, 1, max_depth);
    }
}

/**
 * @brief sample_path The same as the sample_path() above, but this allows the specification of the
 * first hit.
 * @param first_hit The first deterministic intersection which bootstraps the sampling process.
 */
unsigned sample_path(e8util::rng *rng, sampled_pathlet *sampled_path, e8util::ray const &r0,
                     e8::if_path_tracer::first_hits::hit const &hit,
                     e8::if_path_space const &path_space, e8::if_material_container const &mats,
                     unsigned max_depth) {
    if (!hit.intersect.valid() || max_depth == 0) {
        return 0;
    } else {
        sampled_path[0] = sampled_pathlet(-r0.v(), hit.intersect, hit.light, /*dens=*/1.0f);
        return sample_path(rng, sampled_path, path_space, mats, 1, max_depth);
    }
}

/**
 * @brief transport_illum_source Connect a p_illum from the light source to
 * the
 * target_vertex, then compute the light transport of the connection.
 * @param light The light source definition where p_illum is on.
 * @param p_illum A spatial point on the light source.
 * @param n_illum The normal at p_illum.
 * @param target_vert The target where p_illum is connecting to.
 * @param target_o_ray The reflected light ray at target_vert.
 * @param path_space Path space container.
 * @param mats Material container.
 * @return The amount of radiance transported.
 */
e8util::color3 transport_illum_source(e8::if_light const &light, e8util::vec3 const &p_illum,
                                      e8util::vec3 const &n_illum,
                                      e8::intersect_info const &target_vert,
                                      e8util::vec3 const &target_o_ray,
                                      e8::if_path_space const &path_space,
                                      e8::if_material_container const &mats) {
    // construct light path.
    e8util::vec3 l = target_vert.vertex - p_illum;
    e8util::color3 illum = light.eval(l, n_illum, target_vert.normal);
    if (e8util::equals(illum, e8util::vec3(0.0f))) {
        return 0.0f;
    }

    float distance = l.norm();
    e8util::vec3 i = -l / distance;

    // evaluate.
    e8util::ray light_ray(target_vert.vertex, i);
    float t;
    if (!path_space.has_intersect(light_ray, 1e-4f, distance - 1e-3f, t)) {
        return illum * brdf(target_vert, target_o_ray, i, mats);
    } else {
        return 0.0f;
    }
}

/**
 * @brief The light_sample struct
 */
struct light_sample {
    // The selected light sample.
    e8::if_light const *light;

    // Emission sampled from the selected light.
    e8::if_light::emission_surface_sample emission;
    unsigned align0;
};

/**
 * @brief sample_light_source Sample a point on a light source as well as the geometric information
 * local to that point.
 * @param rng Random number generator.
 * @param light_sources The set of all light sources.
 * @return light_sample.
 */
light_sample sample_light_source(e8util::rng &rng, e8::intersect_info const & /*target_vert*/,
                                 e8::if_light_sources const &light_sources) {
    light_sample sample;

    // Sample light.
    float light_prob_mass;
    e8::if_light const *light = light_sources.sample_light(&rng, &light_prob_mass);

    // Sample emission.
    sample.emission = light->sample_emssion_surface(&rng);
    sample.emission.surface.area_dens *= light_prob_mass;

    sample.light = light;

    return sample;
}

/**
 * @brief transport_direct_illum Connect a point in space to a point sample on a light surface then
 * compute light transportation.
 * @param rng Random number generator used to compute a light sample.
 * @param target_o_ray The light ray that exits the target point in space.
 * @param target_vert The target point in space where the radiance is transported.
 * @param path_space Path space container.
 * @param mats Material container.
 * @param light_sources The set of all light sources.
 * @param multi_light_samps The number of transportation samples used to compute the estimate.
 * @return A direct illumination radiance estimate.
 */
e8util::color3 transport_direct_illum(e8util::rng &rng, e8util::vec3 const &target_o_ray,
                                      e8::intersect_info const &target_vert,
                                      e8::if_path_space const &path_space,
                                      e8::if_material_container const &mats,
                                      e8::if_light_sources const &light_sources,
                                      unsigned multi_light_samps) {
    e8util::color3 rad;
    for (unsigned k = 0; k < multi_light_samps; k++) {
        light_sample sample;
        sample = sample_light_source(rng, target_vert, light_sources);
        rad += transport_illum_source(*sample.light, sample.emission.surface.p,
                                      sample.emission.surface.n, target_vert, target_o_ray,
                                      path_space, mats) /
               sample.emission.surface.area_dens;
    }
    return rad / multi_light_samps;
}

/**
 * @brief The light_transport_info class
 */
template <bool IMPORTANCE> class light_transport_info {
  public:
    /**
     * @brief light_transport_info Pre-compute a light transport over all prefixes of the specified
     * path, as well as conditional area density that every vertex was generated. This makes
     * transport() a constant time computation and conditional_density() a smaller constant
     * (complexity).
     * Template variable IMPORTANCE specifies whether to transport light importance or radiance.
     * @param path The path sample that the light transport is to compute on.
     * @param len Length of the path.
     * @param mats Material container.
     */
    light_transport_info(sampled_pathlet const *path, unsigned len,
                         e8::if_material_container const &mats)
        : m_prefix_transport(len), m_cond_density(len), m_path(path) {
        if (len == 0)
            return;

        // Pre-compute light transport.
        e8util::color3 transport = 1.0f;
        m_prefix_transport[0] = 1.0f;
        for (unsigned k = 0; k < len - 1; k++) {
            if (IMPORTANCE) {
                transport *= projected_brdf(path[k], path[k + 1], mats) / path[k + 1].dens;
            } else {
                transport *= projected_adjoint_brdf(path[k], path[k + 1], mats) / path[k + 1].dens;
            }
            m_prefix_transport[k + 1] = transport;
        }

        // Pre-compute conditional density on vertex generation.
        for (unsigned k = 0; k < len; k++) {
            m_cond_density[k] = path[k].dens * path[k].vert.normal.inner(path[k].towards_prev()) /
                                (path[k].vert.t * path[k].vert.t);
        }
    }

    /**
     * @brief transport Compute a light transport sample over the sampled_path[:sub_path_len].
     * @param sub_path_len The length of the prefix subpath taken from sampled_path.
     * @param appending_ray Append a pathlet vector to the ending of the sampled_path.
     * @param appending_ray_dens The density of the appended pathlet.
     * @return The amount of radiance transported.
     */
    e8util::color3 transport(unsigned subpath_len) const { return m_prefix_transport[subpath_len]; }

    /**
     * @brief conditional_density The conditional pathspace density of sampled_path[i]
     * @param i The ith pathlet to compute on.
     * @return The conditional density of that the ith vertex is generated.
     */
    float conditional_density(unsigned i) const { return m_cond_density[i]; }

  private:
    std::vector<e8util::color3> m_prefix_transport;
    std::vector<float> m_cond_density;
    sampled_pathlet const *m_path;
};

/**
 * @brief subpath_density The path density of sampled_path[path_start:path_end]
 * @param src_dens The density of the initial vertex.
 * @param sampled_path The path sample that the sub-path is to be taken from.
 * @param path_start See the brief description.
 * @param path_end See the brief description.
 * @return The probability density of the subpath sampled_path[path_start:path_end].
 */
float subpath_density(float src_point_dens, /*float src_cos,*/ sampled_pathlet const *sampled_path,
                      unsigned path_end) {
    if (path_end == 0)
        return 0.0f;
    float dens = src_point_dens * sampled_path[0].dens *
                 sampled_path[0].vert.normal.inner(sampled_path[0].towards_prev()) /
                 sampled_path[0].vert.t * sampled_path[0].vert.t;
    for (unsigned k = 1; k < path_end; k++) {
        float d = sampled_path[k].dens *
                  sampled_path[k - 1].vert.normal.inner(sampled_path[k].towards()) *
                  sampled_path[k].vert.normal.inner(sampled_path[k].towards_prev()) /
                  (sampled_path[k].vert.t * sampled_path[k].vert.t);
        dens *= d;
    }
    return dens;
}

/**
 * @brief transport_all_connectible_subpaths Two subpaths are conectible iff. they joins the
 * camera
 * and the light source by adding exactly one connection pathlet. The sum of the transportation
 * of
 * the connnected subpaths of different length is a lower bound estimate (sample) to the
 * measurement
 * function. It's a lower bound because it computes transportation only on finite path lengths.
 * @param cam_path The subpath originated from the camera.
 * @param max_cam_path_len The total length of the camera subpath.
 * @param light_path The subpath originated from the light source.
 * @param max_light_path_len The total length of the light subpath.
 * @param light_p The position on the light's surface that the light subpath was emerged from.
 * @param light_n The normal at light_p.
 * @param light_p_dens The probability density at light_p.
 * @param light The light source on which light_p is attached.
 * @param path_space Path space container.
 * @param mats Material container.
 * @return A lower bound sample of the measure function.
 */
e8util::color3
transport_all_connectible_subpaths(sampled_pathlet const *cam_path, unsigned max_cam_path_len,
                                   sampled_pathlet const *light_path, unsigned max_light_path_len,
                                   e8::if_light::emission_sample const &emission,
                                   e8::if_light const &light, e8::if_path_space const &path_space,
                                   e8::if_material_container const &mats) {
    if (max_cam_path_len == 0) {
        // Nothing to sample;
        return 0.0f;
    }

    light_transport_info</*IMPORTANCE=*/false> cam_transport(cam_path, max_cam_path_len, mats);
    light_transport_info</*IMPORTANCE=*/true> light_transport(light_path, max_light_path_len, mats);

    e8util::color3 rad;

    // sweep all path lengths and strategies by pairing camera and light subpaths.
    // no camera vertex generation, yet. both cam_plen, light_plen are one-offset path lengths.
    for (unsigned plen = 1; plen <= max_cam_path_len + max_light_path_len + 1; plen++) {
        unsigned cam_plen = std::min(plen - 1, max_cam_path_len);
        unsigned light_plen = plen - 1 - cam_plen;

        e8util::color3 partition_rad_sum = 0.0f;
        float partition_weight_sum = 0.0f;
        float cur_path_weight = 1.0f;
        while (static_cast<int>(cam_plen) >= 0 && light_plen <= max_light_path_len) {
            if (light_plen == 0 && cam_plen == 0) {
                // We have only the connection path, if it exists, which connects one vertex from
                // the camera and one vertex from the light.
                if (cam_path[0].light != nullptr) {
                    e8util::color3 path_rad = cam_path[0].light->radiance(
                        cam_path[0].towards_prev(), cam_path[0].vert.normal);
                    partition_rad_sum += cur_path_weight * path_rad;
                }
                partition_weight_sum += cur_path_weight;
            } else if (light_plen == 0) {
                sampled_pathlet cam_join_vert = cam_path[cam_plen - 1];
                e8util::color3 transported_importance =
                    transport_illum_source(light, emission.surface.p, emission.surface.n,
                                           cam_join_vert.vert, cam_join_vert.towards_prev(),
                                           path_space, mats) /
                    emission.surface.area_dens; // direction was not chosen by random process.

                // compute light transportation for camera subpath.
                e8util::color3 path_rad = transported_importance *
                                          cam_transport.transport(cam_plen - 1) / cam_path[0].dens;

                partition_rad_sum += cur_path_weight * path_rad;
                partition_weight_sum += cur_path_weight;
            } else if (cam_plen == 0) {
                // The chance of the light path hitting the camera is zero.
                ;
            } else {
                sampled_pathlet light_join_vert = light_path[light_plen - 1];
                sampled_pathlet cam_join_vert = cam_path[cam_plen - 1];
                e8util::vec3 join_path = cam_join_vert.vert.vertex - light_join_vert.vert.vertex;
                float join_distance = join_path.norm();
                join_path = join_path / join_distance;

                e8util::ray join_ray(light_join_vert.vert.vertex, join_path);
                float cos_wo = light_join_vert.vert.normal.inner(join_path);
                float cos_wi = cam_join_vert.vert.normal.inner(-join_path);
                float t;
                if (cos_wo > 0.0f && cos_wi > 0.0f &&
                    !path_space.has_intersect(join_ray, 1e-3f, join_distance - 1e-3f, t)) {
                    // compute light transportation for light subpath.
                    e8util::color3 light_emission =
                        light.projected_radiance(light_path[0].towards(), emission.surface.n) /
                        (light_path[0].dens * emission.surface.area_dens);
                    e8util::color3 light_subpath_importance =
                        light_emission * light_transport.transport(light_plen - 1);

                    // transport light over the join path.
                    float to_area_differential = cos_wi * cos_wo / (join_distance * join_distance);
                    e8util::color3 light_join_weight =
                        brdf(light_join_vert.vert, join_path, light_join_vert.towards_prev(), mats);
                    e8util::color3 cam_join_weight =
                        brdf(cam_join_vert.vert, cam_join_vert.towards_prev(), -join_path, mats);
                    e8util::color3 transported_importance = light_subpath_importance *
                                                            light_join_weight * cam_join_weight *
                                                            to_area_differential;

                    // compute light transportation for camera subpath.
                    e8util::color3 cam_subpath_radiance = transported_importance *
                                                          cam_transport.transport(cam_plen - 1) /
                                                          cam_path[0].dens;

                    partition_rad_sum += cur_path_weight * cam_subpath_radiance;
                }
                partition_weight_sum += cur_path_weight;
            }

            light_plen++;
            cam_plen--;
        }

        if (partition_weight_sum > 0) {
            rad += partition_rad_sum / partition_weight_sum;
        }
    }
    return rad;
}

} // namespace

e8::if_path_tracer::first_hits
e8::if_path_tracer::compute_first_hit(std::vector<e8util::ray> const &rays,
                                      if_path_space const &path_space,
                                      if_light_sources const &light_sources) {
    first_hits results(rays.size());
    for (unsigned i = 0; i < rays.size(); i++) {
        results.hits[i].intersect = path_space.intersect(rays[i]);
        if (results.hits[i].intersect.normal.inner(-rays[i].v()) <= 0) {
            results.hits[i].intersect = intersect_info();
        } else {
            if (results.hits[i].intersect.valid()) {
                results.hits[i].light = light_sources.obj_light(*results.hits[i].intersect.geo);
            }
        }
    }
    return results;
}

std::vector<e8util::vec3>
e8::position_tracer::sample(e8util::rng & /*rng*/, std::vector<e8util::ray> const & /*rays*/,
                            first_hits const &first_hits, if_path_space const &path_space,
                            if_material_container const & /*mats*/,
                            if_light_sources const & /*light_sources*/) const {
    e8util::aabb const &aabb = path_space.aabb();
    e8util::vec3 const &range = aabb.max() - aabb.min();
    std::vector<e8util::vec3> rad(first_hits.hits.size());
    for (unsigned i = 0; i < first_hits.hits.size(); i++) {
        if (first_hits.hits[i].intersect.valid()) {
            e8util::vec3 const &p = (first_hits.hits[i].intersect.vertex - aabb.min()) / range;
            rad[i] = e8util::vec3({p(0), p(1), p(2)});
        } else
            rad[i] = 0.0f;
    }
    return rad;
}

std::vector<e8util::vec3>
e8::normal_tracer::sample(e8util::rng & /*rng*/, std::vector<e8util::ray> const & /*rays*/,
                          first_hits const &first_hits, if_path_space const & /*path_space*/,
                          if_material_container const & /*mats*/,
                          if_light_sources const & /*light_sources*/) const {
    std::vector<e8util::vec3> rad(first_hits.hits.size());
    for (unsigned i = 0; i < first_hits.hits.size(); i++) {
        if (first_hits.hits[i].intersect.valid()) {
            e8util::vec3 const &p = (first_hits.hits[i].intersect.normal + 1.0f) / 2.0f;
            rad[i] = e8util::vec3({p(0), p(1), p(2)});
        } else
            rad[i] = 0.0f;
    }
    return rad;
}

std::vector<e8util::color3>
e8::direct_path_tracer::sample(e8util::rng &rng, std::vector<e8util::ray> const &rays,
                               first_hits const &first_hits, if_path_space const &path_space,
                               if_material_container const &mats,
                               if_light_sources const &light_sources) const {
    std::vector<e8util::color3> rad(first_hits.hits.size());
    for (unsigned i = 0; i < rays.size(); i++) {
        if (first_hits.hits[i].intersect.valid()) {
            // compute radiance.
            rad[i] = transport_direct_illum(rng, -rays[i].v(), first_hits.hits[i].intersect,
                                            path_space, mats, light_sources,
                                            /*multi_light_samps=*/1);
            if (first_hits.hits[i].light != nullptr)
                rad[i] += first_hits.hits[i].light->projected_radiance(
                    -rays[i].v(), first_hits.hits[i].intersect.normal);
        }
    }
    return rad;
}

e8util::vec3 e8::unidirect_path_tracer::sample_indirect_illum(
    e8util::rng &rng, e8util::vec3 const &o, e8::intersect_info const &vert,
    if_path_space const &path_space, if_material_container const &mats,
    if_light_sources const &light_sources, unsigned depth) const {
    static const int mutate_depth = 2;
    float p_survive = 0.5f;
    if (depth >= mutate_depth) {
        if (rng.draw() >= p_survive) {
            return 0.0f;
        }
    } else {
        p_survive = 1;
    }

    // Direct.
    if_light const *light = light_sources.obj_light(*vert.geo);
    e8util::vec3 light_emission;
    if (light != nullptr) {
        light_emission = light->radiance(o, vert.normal);
    }

    // Indirect.
    float proj_solid_dens;
    e8util::vec3 i = sample_brdf(&rng, &proj_solid_dens, vert, o, mats);
    if (proj_solid_dens == 0.0f) {
        return light_emission / p_survive;
    }
    e8::intersect_info indirect_vert = path_space.intersect(e8util::ray(vert.vertex, i));
    if (!indirect_vert.valid() || indirect_vert.normal.inner(-i) <= 0.0f) {
        return light_emission / p_survive;
    }

    e8util::color3 p_depth_to_inf =
        sample_indirect_illum(rng, -i, indirect_vert, path_space, mats, light_sources, depth + 1);
    float cos_w = vert.normal.inner(i);
    e8util::color3 indirect = p_depth_to_inf * brdf(vert, o, i, mats) * cos_w / proj_solid_dens;

    return (light_emission + indirect) / p_survive;
}

std::vector<e8util::color3>
e8::unidirect_path_tracer::sample(e8util::rng &rng, std::vector<e8util::ray> const &rays,
                                  first_hits const &first_hits, if_path_space const &path_space,
                                  if_material_container const &mats,
                                  if_light_sources const &light_sources) const {
    std::vector<e8util::color3> rad(rays.size());
    for (unsigned i = 0; i < rays.size(); i++) {
        e8util::ray const &ray = rays[i];
        if (first_hits.hits[i].intersect.valid()) {
            // compute radiance.
            e8util::color3 p_inf =
                sample_indirect_illum(rng, -ray.v(), first_hits.hits[i].intersect, path_space, mats,
                                      light_sources, /*depth=*/0);
            rad[i] = p_inf;
        }
    }
    return rad;
}

e8util::color3 e8::unidirect_lt1_path_tracer::sample_indirect_illum(
    e8util::rng &rng, e8util::vec3 const &o, e8::intersect_info const &vert,
    if_path_space const &path_space, if_material_container const &mats,
    if_light_sources const &light_sources, unsigned depth, unsigned multi_light_samps,
    unsigned multi_indirect_samps) const {
    static const int mutate_depth = 2;
    float p_survive = 0.5f;
    if (depth >= mutate_depth) {
        if (rng.draw() >= p_survive)
            return 0.0f;
    } else
        p_survive = 1;

    if (depth >= 1)
        multi_indirect_samps = 1;

    // direct.
    e8util::color3 direct =
        transport_direct_illum(rng, o, vert, path_space, mats, light_sources, multi_light_samps);

    // indirect.
    float proj_solid_dens;
    e8util::vec3 multi_indirect;
    for (unsigned k = 0; k < multi_indirect_samps; k++) {
        e8util::color3 i = sample_brdf(&rng, &proj_solid_dens, vert, o, mats);
        if (proj_solid_dens == 0.0f) {
            break;
        }
        e8::intersect_info indirect_vert = path_space.intersect(e8util::ray(vert.vertex, i));
        if (!indirect_vert.valid() || indirect_vert.normal.inner(-i) <= 0.0f) {
            break;
        }

        e8util::color3 indirect =
            sample_indirect_illum(rng, -i, indirect_vert, path_space, mats, light_sources,
                                  depth + 1, multi_light_samps, multi_indirect_samps);
        float cos_w = vert.normal.inner(i);
        multi_indirect += indirect * brdf(vert, o, i, mats) * cos_w / proj_solid_dens;
    }

    return (direct + multi_indirect / multi_indirect_samps) / p_survive;
}

std::vector<e8util::color3>
e8::unidirect_lt1_path_tracer::sample(e8util::rng &rng, std::vector<e8util::ray> const &rays,
                                      first_hits const &first_hits, if_path_space const &path_space,
                                      if_material_container const &mats,
                                      if_light_sources const &light_sources) const {
    std::vector<e8util::color3> rad(rays.size());
    for (unsigned i = 0; i < rays.size(); i++) {
        e8util::ray const &ray = rays[i];
        if (first_hits.hits[i].intersect.valid()) {
            // compute radiance.
            e8util::color3 p2_inf =
                sample_indirect_illum(rng, -ray.v(), first_hits.hits[i].intersect, path_space, mats,
                                      light_sources, /*depth=*/0,
                                      /*multi_light_samps=*/1, /*multi_indirect_samps=*/1);
            if (first_hits.hits[i].light) {
                rad[i] = p2_inf + first_hits.hits[i].light->radiance(
                                      -ray.v(), first_hits.hits[i].intersect.normal);
            } else {
                rad[i] = p2_inf;
            }
        }
    }
    return rad;
}

e8util::color3 e8::bidirect_lt2_path_tracer::join_with_light_paths(
    e8util::rng &rng, e8util::vec3 const &o, e8::intersect_info const &poi,
    if_path_space const &path_space, if_material_container const &mats,
    if_light_sources const &light_sources, unsigned cam_path_len) const {
    e8util::color3 p1_direct =
        transport_direct_illum(rng, o, poi, path_space, mats, light_sources, 1);

    // sample light.
    float light_prob_mass;
    if_light const *light = light_sources.sample_light(&rng, &light_prob_mass);
    e8::if_light::emission_sample emission = light->sample_emssion(&rng);
    e8util::ray light_path(emission.surface.p, emission.w);
    e8::intersect_info const &light_info = path_space.intersect(light_path);
    if (!light_info.valid())
        return 0.0f;

    // construct light path.
    e8util::color3 light_illum =
        light->projected_radiance(emission.w, emission.surface.n) /
        (light_prob_mass * emission.surface.area_dens * emission.solid_angle_dens);

    e8::intersect_info terminate = light_info;
    e8util::vec3 tray = -emission.w;

    // evaluate the area integral.
    e8util::vec3 join_path = poi.vertex - terminate.vertex;
    float distance = join_path.norm();
    join_path = join_path / distance;
    e8util::ray join_ray(terminate.vertex, join_path);
    float cos_w2 = terminate.normal.inner(tray);
    float cos_wo = terminate.normal.inner(join_path);
    float cos_wi = poi.normal.inner(-join_path);
    float t;
    e8util::color3 p2_direct;
    if (cos_wo > 0.0f && cos_wi > 0.0f && cos_w2 > 0.0f &&
        !path_space.has_intersect(join_ray, 1e-4f, distance - 1e-3f, t)) {
        e8util::color3 f2 = light_illum * brdf(terminate, join_path, tray, mats) * cos_w2;
        p2_direct = f2 * cos_wo / (distance * distance) * brdf(poi, o, -join_path, mats) * cos_wi;
        if (cam_path_len == 0)
            return p1_direct + 0.5f * p2_direct;
        else
            return 0.5f * (p1_direct + p2_direct);
    }
    return p1_direct;
}

e8util::color3 e8::bidirect_lt2_path_tracer::sample_indirect_illum(
    e8util::rng &rng, e8util::vec3 const &o, e8::intersect_info const &vert,
    if_path_space const &path_space, if_material_container const &mats,
    if_light_sources const &light_sources, unsigned depth) const {
    static const unsigned mutate_depth = 1;
    float p_survive = 0.5f;
    if (depth >= mutate_depth) {
        if (rng.draw() >= p_survive)
            return 0.0f;
    } else
        p_survive = 1;

    e8util::color3 bidirect =
        join_with_light_paths(rng, o, vert, path_space, mats, light_sources, depth);

    // indirect.
    float mat_pdf;
    e8util::vec3 i = sample_brdf(&rng, &mat_pdf, vert, o, mats);
    e8::intersect_info indirect_info = path_space.intersect(e8util::ray(vert.vertex, i));
    e8util::color3 r;
    if (indirect_info.valid()) {
        e8util::color3 indirect = sample_indirect_illum(rng, -i, indirect_info, path_space, mats,
                                                        light_sources, depth + 1);
        float cos_w = vert.normal.inner(i);
        if (cos_w < 0.0f)
            return 0.0f;
        r = indirect * brdf(vert, o, i, mats) * cos_w / mat_pdf;
    }
    return (bidirect + r) / p_survive;
}

std::vector<e8util::color3>
e8::bidirect_lt2_path_tracer::sample(e8util::rng &rng, std::vector<e8util::ray> const &rays,
                                     first_hits const &first_hits, if_path_space const &path_space,
                                     if_material_container const &mats,
                                     if_light_sources const &light_sources) const {
    std::vector<e8util::color3> rad(rays.size());
    for (unsigned i = 0; i < rays.size(); i++) {
        e8util::ray const &ray = rays[i];
        if (first_hits.hits[i].intersect.valid()) {
            // compute radiance.
            e8util::color3 p2_inf = sample_indirect_illum(
                rng, -ray.v(), first_hits.hits[i].intersect, path_space, mats, light_sources, 0);
            if (first_hits.hits[i].light)
                rad[i] = p2_inf + first_hits.hits[i].light->projected_radiance(
                                      -ray.v(), first_hits.hits[i].intersect.normal);
            else
                rad[i] = p2_inf;
        }
    }
    return rad;
}

e8::if_light const *
e8::bidirect_mis_path_tracer::sample_illum_source(e8util::rng *rng,
                                                  if_light::emission_sample *emission_samp,
                                                  if_light_sources const &light_sources) const {
    // Sample light.
    float light_prob_mass;
    if_light const *light = light_sources.sample_light(rng, &light_prob_mass);

    // Sample emission.
    *emission_samp = light->sample_emssion(rng);
    emission_samp->surface.area_dens *= light_prob_mass;

    return light;
}

std::vector<e8util::color3>
e8::bidirect_mis_path_tracer::sample(e8util::rng &rng, std::vector<e8util::ray> const &rays,
                                     first_hits const &first_hits, if_path_space const &path_space,
                                     if_material_container const &mats,
                                     if_light_sources const &light_sources) const {
    std::vector<e8util::color3> rad(rays.size());

    std::unique_ptr<sampled_pathlet[]> cam_path =
        std::unique_ptr<sampled_pathlet[]>(new sampled_pathlet[m_max_path_len]);

    std::unique_ptr<sampled_pathlet[]> light_path =
        std::unique_ptr<sampled_pathlet[]>(new sampled_pathlet[m_max_path_len]);

    for (unsigned i = 0; i < rays.size(); i++) {
        // Initiates the first pathlets for both camera and light, then random walk over the path
        // space.
        e8util::ray cam_path0 = rays[i];
        unsigned cam_path_len = sample_path(&rng, cam_path.get(), cam_path0, first_hits.hits[i],
                                            path_space, mats, m_max_path_len);

        if_light::emission_sample emission_sample;
        if_light const *light = sample_illum_source(&rng, &emission_sample, light_sources);
        e8util::ray light_path0 = e8util::ray(emission_sample.surface.p, emission_sample.w);
        unsigned light_path_len =
            sample_path(&rng, light_path.get(), light_path0, emission_sample.solid_angle_dens,
                        path_space, mats, m_max_path_len);

        // Compute radiance by combining different strategies.
        rad[i] = transport_all_connectible_subpaths(cam_path.get(), cam_path_len, light_path.get(),
                                                    light_path_len, emission_sample, *light,
                                                    path_space, mats);
    }

    return rad;
}
