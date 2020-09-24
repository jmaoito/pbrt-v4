// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef PBRT_BASE_BXDF_H
#define PBRT_BASE_BXDF_H

#include <pbrt/pbrt.h>

#include <pbrt/util/pstd.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/taggedptr.h>
#include <pbrt/util/vecmath.h>

#include <string>

namespace pbrt {

class MeasuredBRDF;

// BxDFReflTransFlags Definition
enum class BxDFReflTransFlags {
    Unset = 0,
    Reflection = 1 << 0,
    Transmission = 1 << 1,
    All = Reflection | Transmission
};

PBRT_CPU_GPU
inline BxDFReflTransFlags operator|(BxDFReflTransFlags a, BxDFReflTransFlags b) {
    return BxDFReflTransFlags((int)a | (int)b);
}

PBRT_CPU_GPU
inline int operator&(BxDFReflTransFlags a, BxDFReflTransFlags b) {
    return ((int)a & (int)b);
}

PBRT_CPU_GPU
inline BxDFReflTransFlags &operator|=(BxDFReflTransFlags &a, BxDFReflTransFlags b) {
    (int &)a |= int(b);
    return a;
}

std::string ToString(BxDFReflTransFlags flags);

// BxDFFlags Definition
enum BxDFFlags {
    Unset = 0,
    Reflection = 1 << 0,
    Transmission = 1 << 1,
    Diffuse = 1 << 2,
    Glossy = 1 << 3,
    Specular = 1 << 4,
    // Composite _BxDFFlags_ definitions
    DiffuseReflection = Diffuse | Reflection,
    DiffuseTransmission = Diffuse | Transmission,
    GlossyReflection = Glossy | Reflection,
    GlossyTransmission = Glossy | Transmission,
    SpecularReflection = Specular | Reflection,
    SpecularTransmission = Specular | Transmission,
    All = Diffuse | Glossy | Specular | Reflection | Transmission

};

PBRT_CPU_GPU
inline BxDFFlags operator|(BxDFFlags a, BxDFFlags b) {
    return BxDFFlags((int)a | (int)b);
}

PBRT_CPU_GPU
inline int operator&(BxDFFlags a, BxDFFlags b) {
    return ((int)a & (int)b);
}

PBRT_CPU_GPU
inline int operator&(BxDFFlags a, BxDFReflTransFlags b) {
    return ((int)a & (int)b);
}

PBRT_CPU_GPU
inline BxDFFlags &operator|=(BxDFFlags &a, BxDFFlags b) {
    (int &)a |= int(b);
    return a;
}

PBRT_CPU_GPU
inline bool IsReflective(BxDFFlags flags) {
    return (flags & BxDFFlags::Reflection) != 0;
}
PBRT_CPU_GPU
inline bool IsTransmissive(BxDFFlags flags) {
    return (flags & BxDFFlags::Transmission) != 0;
}
PBRT_CPU_GPU
inline bool IsDiffuse(BxDFFlags flags) {
    return (flags & BxDFFlags::Diffuse) != 0;
}
PBRT_CPU_GPU
inline bool IsGlossy(BxDFFlags flags) {
    return (flags & BxDFFlags::Glossy) != 0;
}
PBRT_CPU_GPU
inline bool IsSpecular(BxDFFlags flags) {
    return (flags & BxDFFlags::Specular) != 0;
}

std::string ToString(BxDFFlags flags);

// TransportMode Definition
enum class TransportMode { Radiance, Importance };

PBRT_CPU_GPU
inline TransportMode operator~(TransportMode mode) {
    return (mode == TransportMode::Radiance) ? TransportMode::Importance
                                             : TransportMode::Radiance;
}

std::string ToString(TransportMode mode);

// BSDFSample Definition
struct BSDFSample {
    // BSDFSample Public Methods
    BSDFSample() = default;
    PBRT_CPU_GPU
    BSDFSample(const SampledSpectrum &f, const Vector3f &wi, Float pdf, BxDFFlags flags)
        : f(f), wi(wi), pdf(pdf), flags(flags) {}

    PBRT_CPU_GPU
    operator bool() const { return pdf > 0; }

    PBRT_CPU_GPU
    bool IsReflection() const { return pbrt::IsReflective(flags); }
    PBRT_CPU_GPU
    bool IsTransmission() const { return pbrt::IsTransmissive(flags); }
    PBRT_CPU_GPU
    bool IsDiffuse() const { return pbrt::IsDiffuse(flags); }
    PBRT_CPU_GPU
    bool IsGlossy() const { return pbrt::IsGlossy(flags); }
    PBRT_CPU_GPU
    bool IsSpecular() const { return pbrt::IsSpecular(flags); }

    std::string ToString() const;
    SampledSpectrum f;
    Vector3f wi;
    Float pdf = 0;
    BxDFFlags flags;
};

class IdealDiffuseBxDF;
class DiffuseBxDF;
class DielectricInterfaceBxDF;
class ThinDielectricBxDF;
class HairBxDF;
class MeasuredBxDF;
class ConductorBxDF;
class BSSRDFAdapter;
class CoatedDiffuseBxDF;
class CoatedConductorBxDF;

// BxDFHandle Definition
class BxDFHandle : public TaggedPointer<IdealDiffuseBxDF, DiffuseBxDF, CoatedDiffuseBxDF,
                                        CoatedConductorBxDF, DielectricInterfaceBxDF,
                                        ThinDielectricBxDF, HairBxDF, MeasuredBxDF,
                                        ConductorBxDF, BSSRDFAdapter> {
  public:
    // BxDF Interface
    PBRT_CPU_GPU inline SampledSpectrum f(Vector3f wo, Vector3f wi,
                                          TransportMode mode) const;

    PBRT_CPU_GPU inline BxDFFlags Flags() const;

    using TaggedPointer::TaggedPointer;

    std::string ToString() const;

    PBRT_CPU_GPU inline BSDFSample Sample_f(
        Vector3f wo, Float uc, const Point2f &u, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const;

    PBRT_CPU_GPU inline bool SampledPDFIsProportional() const;

    PBRT_CPU_GPU inline Float PDF(
        Vector3f wo, Vector3f wi, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const;

    PBRT_CPU_GPU
    SampledSpectrum rho(Vector3f wo, pstd::span<const Float> uc,
                        pstd::span<const Point2f> u2) const;
    SampledSpectrum rho(pstd::span<const Float> uc1, pstd::span<const Point2f> u1,
                        pstd::span<const Float> uc2, pstd::span<const Point2f> u2) const;

    PBRT_CPU_GPU inline void Regularize();
};

}  // namespace pbrt

#endif  // PBRT_BASE_BXDF_H
