#include <mitsuba/core/properties.h>
#include <mitsuba/core/warp.h>
#include <mitsuba/render/phase.h>

NAMESPACE_BEGIN(mitsuba)

/**!

.. _phase-hg:

Henyey-Greenstein phase function (:monosp:`hg`)
-----------------------------------------------

.. pluginparameters::

 * - g
   - |float|
   - This parameter must be somewhere in the range -1 to 1
     (but not equal to -1 or 1). It denotes the *mean cosine* of scattering
     interactions. A value greater than zero indicates that medium interactions
     predominantly scatter incident light into a similar direction (i.e. the
     medium is *forward-scattering*), whereas values smaller than zero cause
     the medium to be scatter more light in the opposite direction.
   - |exposed|

This plugin implements the phase function model proposed by
Henyey and Greenstein |nbsp| :cite:`Henyey1941Diffuse`. It is
parameterizable from backward- (g<0) through
isotropic- (g=0) to forward (g>0) scattering.

.. tabs::
    .. code-tab:: xml
        :name: phase-hg

        <phase type="hg">
            <float name="g" value="0.1"/>
        </phase>

    .. code-tab:: python

        'type': 'hg',
        'g': 0.1

*/
template <typename Float, typename Spectrum>
class HGPhaseFunction final : public PhaseFunction<Float, Spectrum> {
public:
    MI_IMPORT_BASE(PhaseFunction, m_flags, m_components)
    MI_IMPORT_TYPES(PhaseFunctionContext)

    HGPhaseFunction(const Properties &props) : Base(props) {
        m_g = props.get<ScalarFloat>("g", 0.8f);
        if (m_g >= 1 || m_g <= -1)
            Log(Error, "The asymmetry parameter must lie in the interval (-1, 1)!");

        m_flags = +PhaseFunctionFlags::Anisotropic;
        dr::set_attr(this, "flags", m_flags);
        m_components.push_back(m_flags); // TODO: check
    }

    void traverse(TraversalCallback *callback) override {
        callback->put_parameter("g", m_g, +ParamFlags::NonDifferentiable);
    }

    MI_INLINE Float eval_hg(Float cos_theta) const {
        Float temp = 1.0f + m_g * m_g + 2.0f * m_g * cos_theta;
        return dr::InvFourPi<ScalarFloat> * (1 - m_g * m_g) / (temp * dr::sqrt(temp));
    }

    std::pair<Vector3f, Float> sample(const PhaseFunctionContext & /* ctx */,
                                      const MediumInteraction3f &mi,
                                      Float /* sample1 */,
                                      const Point2f &sample2,
                                      Mask active) const override {
        MI_MASKED_FUNCTION(ProfilerPhase::PhaseFunctionSample, active);

        Float cos_theta;
        if (dr::abs(m_g) < dr::Epsilon<ScalarFloat>) {
            cos_theta = 1 - 2 * sample2.x();
        } else {
            Float sqr_term = (1 - m_g * m_g) / (1 - m_g + 2 * m_g * sample2.x());
            cos_theta = (1 + m_g * m_g - sqr_term * sqr_term) / (2 * m_g);
        }

        Float sin_theta = dr::safe_sqrt(1.0f - cos_theta * cos_theta);
        auto [sin_phi, cos_phi] = dr::sincos(2 * dr::Pi<ScalarFloat> * sample2.y());
        auto wo = Vector3f(sin_theta * cos_phi, sin_theta * sin_phi, -cos_theta);
        wo = mi.to_world(wo);
        Float pdf = eval_hg(-cos_theta);
        return { wo, pdf };
    }

    Float eval(const PhaseFunctionContext & /* ctx */, const MediumInteraction3f &mi,
               const Vector3f &wo, Mask active) const override {
        MI_MASKED_FUNCTION(ProfilerPhase::PhaseFunctionEvaluate, active);
        return eval_hg(dr::dot(wo, mi.wi));
    }

    std::string to_string() const override {
        std::ostringstream oss;
        oss << "HGPhaseFunction[" << std::endl
            << "  g = " << string::indent(m_g) << std::endl
            << "]";
        return oss.str();
    }

    MI_DECLARE_CLASS()
private:
    ScalarFloat m_g;

};

MI_IMPLEMENT_CLASS_VARIANT(HGPhaseFunction, PhaseFunction)
MI_EXPORT_PLUGIN(HGPhaseFunction, "Henyey-Greenstein phase function")
NAMESPACE_END(mitsuba)
