#include <mitsuba/core/properties.h>
#include <mitsuba/render/phase.h>
#include <mitsuba/render/volume.h>

NAMESPACE_BEGIN(mitsuba)

/**!

.. _phase-blendphase:

Blended phase function (:monosp:`blendphase`)
---------------------------------------------

.. pluginparameters::

 * - weight
   - |float| or |texture|
   - A floating point value or texture with values between zero and one.
     The extreme values zero and one activate the first and second nested phase
     function respectively, and inbetween values interpolate accordingly.
     (Default: 0.5)
   - |exposed|, |differentiable|

 * - (Nested plugin)
   - |phase|
   - Two nested phase function instances that should be mixed according to the
     specified blending weight
   - |exposed|, |differentiable|

This plugin implements a *blend* phase function, which represents linear
combinations of two phase function instances. Any phase function in Mitsuba 3
(be it isotropic, anisotropic, micro-flake ...) can be mixed with others in this
manner. This is of particular interest when mixing components in a participating
medium (*e.g.* accounting for the presence of aerosols in a Rayleigh-scattering
atmosphere).

.. tabs::
    .. code-tab:: xml
        :name: phase-blendphase

        <phase type="blendphase">
            <float name="weight" value="0.5"/>
            <phase type="isotropic" />
            <phase type="hg">
                <float name="g" value="0.2"/>
            </phase>
        </phase>

    .. code-tab:: python

        'type': 'hg',
        'g': 0.1

*/

template <typename Float, typename Spectrum>
class BlendPhaseFunction final : public PhaseFunction<Float, Spectrum> {
public:
    MI_IMPORT_BASE(PhaseFunction, m_flags, m_components)
    MI_IMPORT_TYPES(PhaseFunctionContext, Volume)

    BlendPhaseFunction(const Properties &props) : Base(props) {
        int phase_index = 0;

        for (auto &[name, obj] : props.objects(false)) {
            auto *phase = dynamic_cast<Base *>(obj.get());
            if (phase) {
                if (phase_index == 2)
                    Throw("BlendPhase: Cannot specify more than two child "
                          "phase functions");
                m_nested_phase[phase_index++] = phase;
                props.mark_queried(name);
            }
        }

        m_weight = props.volume<Volume>("weight");
        if (phase_index != 2)
            Throw("BlendPhase: Two child phase functions must be specified!");

        m_components.clear();
        for (size_t i = 0; i < 2; ++i)
            for (size_t j = 0; j < m_nested_phase[i]->component_count(); ++j)
                m_components.push_back(m_nested_phase[i]->flags(j));

        m_flags = m_nested_phase[0]->flags() | m_nested_phase[1]->flags();
        dr::set_attr(this, "flags", m_flags);
    }

    void traverse(TraversalCallback *callback) override {
        callback->put_object("weight",  m_weight.get(),          +ParamFlags::Differentiable);
        callback->put_object("phase_0", m_nested_phase[0].get(), +ParamFlags::Differentiable);
        callback->put_object("phase_1", m_nested_phase[1].get(), +ParamFlags::Differentiable);
    }

    std::pair<Vector3f, Float> sample(const PhaseFunctionContext &ctx,
                                      const MediumInteraction3f &mi,
                                      Float sample1, const Point2f &sample2,
                                      Mask active) const override {
        MI_MASKED_FUNCTION(ProfilerPhase::PhaseFunctionSample, active);

        Float weight = eval_weight(mi, active);
        if (unlikely(ctx.component != (uint32_t) -1)) {
            bool sample_first =
                ctx.component < m_nested_phase[0]->component_count();
            PhaseFunctionContext ctx2(ctx);
            if (!sample_first)
                ctx2.component -=
                    (uint32_t) m_nested_phase[0]->component_count();
            else
                weight = 1.f - weight;
            auto [wo, pdf] = m_nested_phase[sample_first ? 0 : 1]->sample(
                ctx2, mi, sample1, sample2, active);
            pdf *= weight;
            return { wo, pdf };
        }

        Vector3f wo;
        Float pdf;

        Mask m0 = active && sample1 > weight,
             m1 = active && sample1 <= weight;

        if (dr::any_or<true>(m0)) {
            auto [wo0, pdf0] = m_nested_phase[0]->sample(
                ctx, mi, (sample1 - weight) / (1 - weight), sample2, m0);
            dr::masked(wo, m0)  = wo0;
            dr::masked(pdf, m0) = pdf0;
        }

        if (dr::any_or<true>(m1)) {
            auto [wo1, pdf1] = m_nested_phase[1]->sample(
                ctx, mi, sample1 / weight, sample2, m1);
            dr::masked(wo, m1)  = wo1;
            dr::masked(pdf, m1) = pdf1;
        }

        return { wo, pdf };
    }

    MI_INLINE Float eval_weight(const MediumInteraction3f &mi,
                                 const Mask &active) const {
        return dr::clamp(m_weight->eval_1(mi, active), 0.f, 1.f);
    }

    Float eval(const PhaseFunctionContext &ctx, const MediumInteraction3f &mi,
               const Vector3f &wo, Mask active) const override {
        MI_MASKED_FUNCTION(ProfilerPhase::PhaseFunctionEvaluate, active);

        Float weight = eval_weight(mi, active);

        if (unlikely(ctx.component != (uint32_t) -1)) {
            bool sample_first =
                ctx.component < m_nested_phase[0]->component_count();
            PhaseFunctionContext ctx2(ctx);
            if (!sample_first)
                ctx2.component -=
                    (uint32_t) m_nested_phase[0]->component_count();
            else
                weight = 1.f - weight;
            return weight * m_nested_phase[sample_first ? 0 : 1]->eval(
                                ctx2, mi, wo, active);
        }

        return m_nested_phase[0]->eval(ctx, mi, wo, active) * (1 - weight) +
               m_nested_phase[1]->eval(ctx, mi, wo, active) * weight;
    }

    std::string to_string() const override {
        std::ostringstream oss;
        oss << "BlendPhase[" << std::endl
            << "  weight = " << string::indent(m_weight) << "," << std::endl
            << "  nested_phase[0] = " << string::indent(m_nested_phase[0])
            << "," << std::endl
            << "  nested_phase[1] = " << string::indent(m_nested_phase[1])
            << std::endl
            << "]";
        return oss.str();
    }

    MI_DECLARE_CLASS()
protected:
    ref<Volume> m_weight;
    ref<Base> m_nested_phase[2];
};

MI_IMPLEMENT_CLASS_VARIANT(BlendPhaseFunction, PhaseFunction)
MI_EXPORT_PLUGIN(BlendPhaseFunction, "Blended phase function")
NAMESPACE_END(mitsuba)
