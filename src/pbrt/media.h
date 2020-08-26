// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef PBRT_MEDIA_H
#define PBRT_MEDIA_H

#include <pbrt/pbrt.h>

#include <pbrt/base/medium.h>
#include <pbrt/interaction.h>
#include <pbrt/paramdict.h>
#include <pbrt/textures.h>
#include <pbrt/util/colorspace.h>
#include <pbrt/util/error.h>
#include <pbrt/util/memory.h>
#include <pbrt/util/pstd.h>
#include <pbrt/util/scattering.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/transform.h>

#include <memory>
#include <vector>

namespace pbrt {

// Media Function Declarations
bool GetMediumScatteringProperties(const std::string &name, SpectrumHandle *sigma_a,
                                   SpectrumHandle *sigma_s, Allocator alloc);

// HGPhaseFunction Definition
class HGPhaseFunction {
  public:
    // HGPhaseFunction Public Methods
    HGPhaseFunction() = default;
    PBRT_CPU_GPU
    HGPhaseFunction(Float g) : g(g) {}

    PBRT_CPU_GPU
    Float p(const Vector3f &wo, const Vector3f &wi) const {
        return HenyeyGreenstein(Dot(wo, wi), g);
    }

    PBRT_CPU_GPU
    PhaseFunctionSample Sample_p(const Vector3f &wo, const Point2f &u) const {
        Float pdf;
        Vector3f wi = SampleHenyeyGreenstein(wo, g, u, &pdf);
        return PhaseFunctionSample{pdf, wi, pdf};
    }

    PBRT_CPU_GPU
    Float PDF(const Vector3f &wo, const Vector3f &wi) const { return p(wo, wi); }

    std::string ToString() const;

  private:
    // HGPhaseFunction Private Members
    Float g;
};

// MediumSample Definition
struct MediumSample {
    // MediumSample Public Methods
    MediumSample() = default;
    PBRT_CPU_GPU
    explicit MediumSample(const SampledSpectrum &Tmaj) : Tmaj(Tmaj) {}
    PBRT_CPU_GPU
    MediumSample(const MediumInteraction &intr, const SampledSpectrum &Tmaj)
        : intr(intr), Tmaj(Tmaj) {}

    std::string ToString() const;

    pstd::optional<MediumInteraction> intr;
    SampledSpectrum Tmaj = SampledSpectrum(1.f);
};

// HomogeneousMedium Definition
class HomogeneousMedium {
  public:
    // HomogeneousMedium Public Methods
    HomogeneousMedium(SpectrumHandle sigma_a, SpectrumHandle sigma_s, Float sigScale,
                      SpectrumHandle Le, Float g, Allocator alloc)
        : sigma_a_spec(sigma_a, alloc),
          sigma_s_spec(sigma_s, alloc),
          sigScale(sigScale),
          Le_spec(Le, alloc),
          phase(g) {}

    static HomogeneousMedium *Create(const ParameterDictionary &parameters,
                                     const FileLoc *loc, Allocator alloc);

    template <typename F>
    PBRT_CPU_GPU void SampleTmaj(const Ray &ray, Float tMax, RNG &rng,
                                 const SampledWavelengths &lambda, F callback) const {
        // Compute normalized ray in medium, _rayp_
        tMax *= Length(ray.d);
        Ray rayp(ray.o, Normalize(ray.d));

        // Compute _SampledSpectrum_ scattering properties for medium
        SampledSpectrum sigma_a = sigScale * sigma_a_spec.Sample(lambda);
        SampledSpectrum sigma_s = sigScale * sigma_s_spec.Sample(lambda);
        SampledSpectrum sigma_t = sigma_a + sigma_s;
        SampledSpectrum sigma_maj = sigma_t;

        // Sample exponential funciton to find _t_ for scattering event
        if (sigma_maj[0] == 0)
            return;
        Float u = rng.Uniform<Float>();
        Float t = SampleExponential(u, sigma_maj[0]);

        if (t >= tMax) {
            // Return transmittance to medium exit point
            callback(MediumSample(FastExp(-tMax * sigma_maj)));

        } else {
            // Report scattering event in homogeneous medium
            SampledSpectrum Tmaj = FastExp(-t * sigma_maj);
            SampledSpectrum Le = Le_spec.Sample(lambda);
            MediumInteraction intr(rayp(t), -rayp.d, ray.time, sigma_a, sigma_s,
                                   sigma_maj, Le, this, &phase);
            callback(MediumSample(intr, Tmaj));
        }
    }

    bool IsEmissive() const { return Le_spec.MaxValue() > 0; }

    std::string ToString() const;

  private:
    // HomogeneousMedium Private Data
    DenselySampledSpectrum sigma_a_spec, sigma_s_spec, Le_spec;
    Float sigScale;
    HGPhaseFunction phase;
};

// GeneralMedium Definition
template <typename DensityProvider>
class GeneralMedium {
  public:
    // GeneralMedium Public Methods
    GeneralMedium(const DensityProvider *provider, SpectrumHandle sigma_a,
                  SpectrumHandle sigma_s, Float sigScale, Float g,
                  const Transform &renderFromMedium, Allocator alloc)
        : provider(provider),
          mediumBounds(provider->Bounds()),
          sigma_a_spec(sigma_a, alloc),
          sigma_s_spec(sigma_s, alloc),
          sigScale(sigScale),
          phase(g),
          mediumFromRender(Inverse(renderFromMedium)),
          renderFromMedium(renderFromMedium),
          maxDensityGrid(alloc) {
        maxDensityGrid = provider->GetMaxDensityGrid(alloc, &maxDGridRes);
    }

    std::string ToString() const { return "GeneralMedium TODO"; }
    bool IsEmissive() const { return provider->IsEmissive(); }

    template <typename F>
    PBRT_CPU_GPU void SampleTmaj(const Ray &rRender, Float raytMax, RNG &rng,
                                 const SampledWavelengths &lambda, F callback) const {
        // Transform ray to grid density's space and compute bounds overlap
        raytMax *= Length(rRender.d);
        Ray ray = mediumFromRender(Ray(rRender.o, Normalize(rRender.d)), &raytMax);
        Float tMin, tMax;
        if (!mediumBounds.IntersectP(ray.o, ray.d, raytMax, &tMin, &tMax))
            return;
        DCHECK_LE(tMax, raytMax);

        // Sample spectra for grid medium scattering
        SampledSpectrum sigma_a = sigScale * sigma_a_spec.Sample(lambda);
        SampledSpectrum sigma_s = sigScale * sigma_s_spec.Sample(lambda);
        SampledSpectrum sigma_t = sigma_a + sigma_s;

        // Set up 3D DDA for ray through grid
        Vector3f diag = mediumBounds.Diagonal();
        Ray rayGrid(Point3f((ray.o.x - mediumBounds.pMin.x) / diag.x,
                            (ray.o.y - mediumBounds.pMin.y) / diag.y,
                            (ray.o.z - mediumBounds.pMin.z) / diag.z),
                    Vector3f(ray.d.x / diag.x, ray.d.y / diag.y, ray.d.z / diag.z));
        Point3f gridIntersect = rayGrid(tMin);
        float nextCrossingT[3], deltaT[3];
        int step[3], voxelLimit[3], voxel[3];
        Vector3f voxelWidth(1.f / maxDGridRes.x, 1.f / maxDGridRes.y,
                            1.f / maxDGridRes.z);
        for (int axis = 0; axis < 3; ++axis) {
            // Initialize ray stepping parameters for axis
            // Handle negative zero ray direction
            if (rayGrid.d[axis] == -0.f)
                rayGrid.d[axis] = 0.f;

            // Compute current voxel for axis
            voxel[axis] =
                Clamp(gridIntersect[axis] * maxDGridRes[axis], 0, maxDGridRes[axis] - 1);

            if (rayGrid.d[axis] >= 0) {
                // Handle ray with positive direction for voxel stepping
                Float nextVoxelPos = Float(voxel[axis] + 1) / maxDGridRes[axis];
                nextCrossingT[axis] =
                    tMin + (nextVoxelPos - gridIntersect[axis]) / rayGrid.d[axis];
                deltaT[axis] = voxelWidth[axis] / rayGrid.d[axis];
                step[axis] = 1;
                voxelLimit[axis] = maxDGridRes[axis];

            } else {
                // Handle ray with negative direction for voxel stepping
                Float nextVoxelPos = Float(voxel[axis]) / maxDGridRes[axis];
                nextCrossingT[axis] =
                    tMin + (nextVoxelPos - gridIntersect[axis]) / rayGrid.d[axis];
                deltaT[axis] = -voxelWidth[axis] / rayGrid.d[axis];
                step[axis] = -1;
                voxelLimit[axis] = -1;
            }
        }

        // Walk ray through maximum density grid and sample scattering
        Float t0 = tMin, u = rng.Uniform<Float>();
        while (true) {
            // Find _stepAxis_ for stepping to next voxel and exit point _t1_
            int bits = ((nextCrossingT[0] < nextCrossingT[1]) << 2) +
                       ((nextCrossingT[0] < nextCrossingT[2]) << 1) +
                       ((nextCrossingT[1] < nextCrossingT[2]));
            const int cmpToAxis[8] = {2, 1, 2, 1, 2, 2, 0, 0};
            int stepAxis = cmpToAxis[bits];
            Float t1 = nextCrossingT[stepAxis];

            // Sample volume scattering in current voxel
            // Get _maxDensity_ for current voxel and compute _sigma\_maj_
            int offset = voxel[0] + maxDGridRes.x * (voxel[1] + maxDGridRes.y * voxel[2]);
            Float maxDensity = maxDensityGrid[offset];
            SampledSpectrum sigma_maj(sigma_t * maxDensity);

            if (sigma_maj[0] > 0) {
                while (true) {
                    // Sample medium in current voxel
                    // Compute _uEnd_ for exiting voxel and continue if no valid event
                    Float uEnd = InvertExponentialSample(t1 - t0, sigma_maj[0]);
                    if (u >= uEnd) {
                        u = std::min((u - uEnd) / (1 - uEnd), OneMinusEpsilon);
                        goto advance;
                    }

                    // Sample _t_ for scattering event and check validity
                    Float t = t0 + SampleExponential(u, sigma_maj[0]);
                    if (t >= tMax) {
                        callback(MediumSample(SampledSpectrum(1.f)));
                        return;
                    }

                    // Report scattering event in grid to callback function
                    Point3f p = ray(t);
                    SampledSpectrum Tmaj = FastExp(-sigma_maj * (t - t0));
                    // Compute _density_ and _Le_ at sampled point in grid
                    SampledSpectrum density = provider->Density(p, lambda);
                    SampledSpectrum Le = provider->Le(p, lambda);

                    MediumInteraction intr(renderFromMedium(p), -Normalize(rRender.d),
                                           rRender.time, sigma_a * density,
                                           sigma_s * density, sigma_maj, Le, this,
                                           &phase);
                    if (!callback(MediumSample(intr, Tmaj)))
                        return;

                    // Update _u_ and _t0_ after grid medium event
                    u = rng.Uniform<Float>();
                    t0 = t;
                }
            }

        advance:
            // Advance to next voxel in maximum density grid
            if (nextCrossingT[stepAxis] > tMax)
                return;
            voxel[stepAxis] += step[stepAxis];
            if (voxel[stepAxis] == voxelLimit[stepAxis])
                return;
            nextCrossingT[stepAxis] += deltaT[stepAxis];
            t0 = t1;
        }
    }

    static GeneralMedium<DensityProvider> *Create(const DensityProvider *provider,
                                                  const ParameterDictionary &parameters,
                                                  const Transform &renderFromMedium,
                                                  const FileLoc *loc, Allocator alloc) {
        SpectrumHandle sig_a = nullptr, sig_s = nullptr;
        std::string preset = parameters.GetOneString("preset", "");
        if (!preset.empty()) {
            if (!GetMediumScatteringProperties(preset, &sig_a, &sig_s, alloc))
                Warning(loc, "Material preset \"%s\" not found.", preset);
        }

        if (sig_a == nullptr) {
            sig_a = parameters.GetOneSpectrum("sigma_a", nullptr, SpectrumType::General,
                                              alloc);
            if (sig_a == nullptr)
                sig_a = alloc.new_object<RGBSpectrum>(*RGBColorSpace::sRGB,
                                                      RGB(.0011f, .0024f, .014f));
        }
        if (sig_s == nullptr) {
            sig_s = parameters.GetOneSpectrum("sigma_s", nullptr, SpectrumType::General,
                                              alloc);
            if (sig_s == nullptr)
                sig_s = alloc.new_object<RGBSpectrum>(*RGBColorSpace::sRGB,
                                                      RGB(2.55f, 3.21f, 3.77f));
        }

        Float sigScale = parameters.GetOneFloat("scale", 1.f);

        Float g = parameters.GetOneFloat("g", 0.0f);

        return alloc.new_object<GeneralMedium<DensityProvider>>(
            provider, sig_a, sig_s, sigScale, g, renderFromMedium, alloc);
    }

  private:
    // GeneralMedium Private Members
    const DensityProvider *provider;
    Bounds3f mediumBounds;
    DenselySampledSpectrum sigma_a_spec, sigma_s_spec;
    Float sigScale;
    HGPhaseFunction phase;
    Transform mediumFromRender, renderFromMedium;
    pstd::vector<Float> maxDensityGrid;
    Point3i maxDGridRes;
};

// UniformGridMediumProvider Definition
class UniformGridMediumProvider {
  public:
    // UniformGridMediumProvider Public Methods
    UniformGridMediumProvider(const Bounds3f &bounds,
                              pstd::optional<SampledGrid<Float>> densityGrid,
                              pstd::optional<SampledGrid<RGB>> rgbDensityGrid,
                              const RGBColorSpace *colorSpace, SpectrumHandle Le,
                              SampledGrid<Float> LeScaleGrid, Allocator alloc);

    static UniformGridMediumProvider *Create(const ParameterDictionary &parameters,
                                             const FileLoc *loc, Allocator alloc);

    std::string ToString() const;

    PBRT_CPU_GPU
    const Bounds3f &Bounds() const { return bounds; }

    bool IsEmissive() const { return Le_spec.MaxValue() > 0; }

    PBRT_CPU_GPU
    SampledSpectrum Le(const Point3f &p, const SampledWavelengths &lambda) const {
        Point3f pp = Point3f(bounds.Offset(p));
        return Le_spec.Sample(lambda) * LeScaleGrid.Lookup(pp);
    }

    PBRT_CPU_GPU
    SampledSpectrum Density(const Point3f &p, const SampledWavelengths &lambda) const {
        Point3f pp = Point3f(bounds.Offset(p));
        if (densityGrid)
            return SampledSpectrum(densityGrid->Lookup(pp));
        else {
            RGB rgb = rgbDensityGrid->Lookup(pp);
            return RGBSpectrum(*colorSpace, rgb).Sample(lambda);
        }
    }

    pstd::vector<Float> GetMaxDensityGrid(Allocator alloc, Point3i *res) const {
        pstd::vector<Float> maxGrid(alloc);
        // Set _maxDGridRes_ and allocate _maxGrid_
        *res = Point3i(4, 4, 4);
        maxGrid.resize(res->x * res->y * res->z);

        // Define _getMaxDensity_ lambda
        auto getMaxDensity = [&](const Bounds3f &bounds) -> Float {
            if (densityGrid)
                return densityGrid->MaximumValue(bounds);
            else {
                // Compute maximum density of RGB density over _bounds_
                int nx = rgbDensityGrid->xSize();
                int ny = rgbDensityGrid->ySize();
                int nz = rgbDensityGrid->zSize();
                Point3f ps[2] = {
                    Point3f(bounds.pMin.x * nx - .5f, bounds.pMin.y * ny - .5f,
                            bounds.pMin.z * nz - .5f),
                    Point3f(bounds.pMax.x * nx - .5f, bounds.pMax.y * ny - .5f,
                            bounds.pMax.z * nz - .5f)};
                Point3i pi[2] = {Max(Point3i(Floor(ps[0])), Point3i(0, 0, 0)),
                                 Min(Point3i(Floor(ps[1])) + Vector3i(1, 1, 1),
                                     Point3i(nx - 1, ny - 1, nz - 1))};

                std::vector<SampledWavelengths> lambdas;
                for (Float u : Stratified1D(16))
                    lambdas.push_back(SampledWavelengths::SampleXYZ(u));

                Float maxDensity = 0;
                for (int z = pi[0].z; z <= pi[1].z; ++z)
                    for (int y = pi[0].y; y <= pi[1].y; ++y)
                        for (int x = pi[0].x; x <= pi[1].x; ++x) {
                            RGB rgb = rgbDensityGrid->Lookup(Point3i(x, y, z));

                            Float maxComponent = std::max({rgb.r, rgb.g, rgb.b});
                            if (maxComponent == 0)
                                continue;

                            RGBSpectrum spec(*colorSpace, rgb);
                            for (const SampledWavelengths &lambda : lambdas) {
                                SampledSpectrum s = spec.Sample(lambda);
                                maxDensity = std::max(maxDensity, s.MaxComponentValue());
                            }
                        }

                return maxDensity * 1.025f;
            }
        };

        // Compute maximum density for each _maxGrid_ cell
        int offset = 0;
        for (int z = 0; z < res->z; ++z)
            for (int y = 0; y < res->y; ++y)
                for (int x = 0; x < res->x; ++x) {
                    Bounds3f bounds(
                        Point3f(Float(x) / res->x, Float(y) / res->y, Float(z) / res->z),
                        Point3f(Float(x + 1) / res->x, Float(y + 1) / res->y,
                                Float(z + 1) / res->z));
                    maxGrid[offset++] = getMaxDensity(bounds);
                }

        return maxGrid;
    }

  private:
    // UniformGridMediumProvider Private Members
    Bounds3f bounds;
    pstd::optional<SampledGrid<Float>> densityGrid;
    pstd::optional<SampledGrid<RGB>> rgbDensityGrid;
    const RGBColorSpace *colorSpace;
    DenselySampledSpectrum Le_spec;
    SampledGrid<Float> LeScaleGrid;
};

// CloudMediumProvider Definition
class CloudMediumProvider {
  public:
    // CloudMediumProvider Public Methods
    static CloudMediumProvider *Create(const ParameterDictionary &parameters,
                                       const FileLoc *loc, Allocator alloc);

    std::string ToString() const { return "TODO cloud provider"; }

    CloudMediumProvider(const Bounds3f &bounds, Float density, Float wispiness,
                        Float extent)
        : bounds(bounds), density(density), wispiness(wispiness), extent(extent) {}

    PBRT_CPU_GPU
    const Bounds3f &Bounds() const { return bounds; }

    bool IsEmissive() const { return false; }

    PBRT_CPU_GPU
    SampledSpectrum Le(const Point3f &p, const SampledWavelengths &lambda) const {
        return SampledSpectrum(0.f);
    }

    pstd::vector<Float> GetMaxDensityGrid(Allocator alloc, Point3i *res) const {
        *res = Point3i(1, 1, 1);
        return pstd::vector<Float>(1, 1.f, alloc);
    }

    PBRT_CPU_GPU
    SampledSpectrum Density(const Point3f &p, const SampledWavelengths &lambda) const {
        Point3f pp = 5 * extent * p;

        // Use noise to perturb the lookup point.
        if (wispiness > 0) {
            Float vscale = .05f * wispiness, vlac = 10.f;
            for (int i = 0; i < 2; ++i) {
                Float delta = .01f;
                // TODO: update Noise() to return the gradient?
                Float base = Noise(vlac * pp);
                Point3f pd(Noise(vlac * pp + Vector3f(delta, 0, 0)),
                           Noise(vlac * pp + Vector3f(0, delta, 0)),
                           Noise(vlac * pp + Vector3f(0, 0, delta)));
                pp += vscale * (pd - Vector3f(base, base, base)) / delta;
                vscale *= 0.5f;
                vlac *= 2.01f;
            }
        }

        Float d = 0;
        Float scale = .5, lac = 1.f;
        for (int i = 0; i < 5; ++i) {
            d += scale * Noise(lac * pp);
            scale *= 0.5f;
            lac *= 2.01f;
        }

        d = Clamp((1 - p.y) * 4.5f * density * d, 0, 1);
        d += 2 * std::max<Float>(0, .5f - p.y);
        return SampledSpectrum(Clamp(d, 0, 1));
    }

  private:
    // CloudMediumProvider Private Members
    Bounds3f bounds;
    Float density, wispiness, extent;
};

inline Float PhaseFunctionHandle::p(const Vector3f &wo, const Vector3f &wi) const {
    auto p = [&](auto ptr) { return ptr->p(wo, wi); };
    return Dispatch(p);
}

inline PhaseFunctionSample PhaseFunctionHandle::Sample_p(const Vector3f &wo,
                                                         const Point2f &u) const {
    auto sample = [&](auto ptr) { return ptr->Sample_p(wo, u); };
    return Dispatch(sample);
}

inline Float PhaseFunctionHandle::PDF(const Vector3f &wo, const Vector3f &wi) const {
    auto pdf = [&](auto ptr) { return ptr->PDF(wo, wi); };
    return Dispatch(pdf);
}

template <typename F>
void MediumHandle::SampleTmaj(const Ray &ray, Float tMax, RNG &rng,
                              const SampledWavelengths &lambda, F func) const {
    auto sampletn = [&](auto ptr) { ptr->SampleTmaj(ray, tMax, rng, lambda, func); };
    Dispatch(sampletn);
}

}  // namespace pbrt

#endif  // PBRT_MEDIA_H
