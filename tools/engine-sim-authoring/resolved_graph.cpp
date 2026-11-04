// Offline adapter for the evaluated engine-sim graph. No MR parsing or simulation
// is implemented here. prepare_graph.py adds read-only friend declarations to a
// private header staging directory; the source checkout is never modified.
#include "compiler.h"
#include "engine_sim.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Json {
    std::string text;
    Json(double v) {
        if (!std::isfinite(v)) throw std::runtime_error("Non-finite resolved graph value");
        std::ostringstream s; s << std::setprecision(17) << v; text = s.str();
    }
    Json(int v) : Json(double(v)) {}
    Json(bool v) : text(v ? "true" : "false") {}
    Json(const char *s) : Json(std::string(s)) {}
    Json(const std::string &s) {
        std::ostringstream out; out << '"';
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') out << '\\' << c;
            else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
            else out << c;
        }
        out << '"'; text = out.str();
    }
    static Json raw(std::string value) { Json j(0); j.text = std::move(value); return j; }
};
Json array(const std::vector<Json> &values) {
    std::string s = "[";
    for (const auto &v : values) { if (s.size() > 1) s += ','; s += v.text; }
    return Json::raw(s + ']');
}
Json object(std::initializer_list<std::pair<std::string, Json>> fields) {
    std::string s = "{";
    for (const auto &kv : fields) {
        if (s.size() > 1) s += ',';
        s += Json(kv.first).text + ':' + kv.second.text;
    }
    return Json::raw(s + '}');
}
std::string id(const char *prefix, int index) {
    std::ostringstream out; out << prefix << '-' << std::setw(3) << std::setfill('0') << index;
    return out.str();
}
void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

struct CrankwaveResolvedGraph {
    std::map<const Function *, std::string> functionIds;
    std::vector<Json> functions;

    std::string function(const Function *f) {
        require(f != nullptr, "Missing function");
        auto found = functionIds.find(f);
        if (found != functionIds.end()) return found->second;
        const std::string name = id("function", int(functionIds.size()));
        functionIds.emplace(f, name);
        require(f->m_size > 0 && f->m_inputScale > 0 && f->m_filterRadius > 0,
                "Unsupported empty/nonpositive-scale function");
        std::vector<Json> samples;
        for (int i = 0; i < f->m_size; ++i) samples.push_back(array({f->m_x[i], f->m_y[i]}));
        functions.push_back(object({{"id", name}, {"input_scale", f->m_inputScale},
            {"output_scale", f->m_outputScale}, {"filter_radius", f->m_filterRadius},
            {"evaluation", "triangle"}, {"samples", array(samples)}}));
        return name;
    }

    Json cam(Camshaft *c, int expectedLobes) {
        require(c && c->m_lobes == expectedLobes, "Cam lobe count does not match its bank");
        std::vector<Json> lobes;
        for (int i = 0; i < c->m_lobes; ++i) lobes.emplace_back(c->getLobeCenterline(i));
        return object({{"profile", function(c->getLobeProfile())},
            {"advance_rad", c->getAdvance()}, {"base_radius_m", c->getBaseRadius()},
            {"lobe_centers_cam_rad", array(lobes)}});
    }

    Json exportGraph(es_script::Compiler::Output &out) {
        auto *e = out.engine;
        require(e && out.vehicle && out.transmission, "MR must supply engine, vehicle and transmission");
        require(e->getCrankshaftCount() == 1, "Exporter subset supports exactly one crankshaft");
        auto *throttle = dynamic_cast<DirectThrottleLinkage *>(e->m_throttle);
        require(throttle != nullptr, "Exporter subset supports direct throttle only");
        auto *fuel = e->getFuel();
        require(!fuel->isCompressionIgnition() && fuel->getPremixedBurnFraction() == 0 && fuel->getLbvMultiplier() == 1,
                "Fork combustion extensions cannot be silently converted to spark ignition");
        auto *crank = e->getCrankshaft(0);
        require(crank->getPosX() == 0 && crank->getPosY() == 0, "Offset crank position is outside exporter subset");
        std::vector<Json> journals, banks, intakes, exhausts, cylinders, ignition;
        for (int i = 0; i < crank->getRodJournalCount(); ++i) journals.push_back(object({
            {"id", id("journal", i)}, {"angle_rad", crank->getRodJournalAngle(i)}}));
        std::map<const Intake *, std::string> intakeIds;
        std::map<const ExhaustSystem *, std::string> exhaustIds;
        for (int i = 0; i < e->getIntakeCount(); ++i) {
            auto *v = e->getIntake(i); const auto name = id("intake", i); intakeIds[v] = name;
            intakes.push_back(object({{"id", name}, {"volume_m3", v->m_system.volume()},
                {"area_m2", v->getPlenumCrossSectionArea()}, {"input_k", v->m_inputFlowK},
                {"idle_k", v->m_idleFlowK}, {"runner_k", v->getRunnerFlowRate()},
                {"molecular_afr", v->m_molecularAfr}, {"idle_plate", v->m_idleThrottlePlatePosition},
                {"runner_length_m", v->getRunnerLength()}, {"velocity_decay", v->getVelocityDecay()}}));
        }
        for (int i = 0; i < e->getExhaustSystemCount(); ++i) {
            auto *v = e->getExhaustSystem(i); const auto name = id("exhaust", i); exhaustIds[v] = name;
            auto *ir = v->getImpulseResponse(); require(ir != nullptr, "Missing exhaust IR");
            exhausts.push_back(object({{"id", name}, {"length_m", v->getLength()},
                {"area_m2", v->getCollectorCrossSectionArea()}, {"outlet_k", v->m_outletFlowRate},
                {"primary_k", v->getPrimaryFlowRate()}, {"primary_length_m", v->getPrimaryTubeLength()},
                {"velocity_decay", v->getVelocityDecay()}, {"audio_volume", v->getAudioVolume()},
                {"ir_path", ir->getFilename()}, {"ir_volume", ir->getVolume()}}));
        }
        for (int i = 0; i < e->getCylinderBankCount(); ++i) {
            auto *b = e->getCylinderBank(i); auto *h = e->getHead(i);
            require(dynamic_cast<StandardValvetrain *>(h->m_valvetrain), "Exporter subset supports standard fixed cams only");
            banks.push_back(object({{"id", id("bank", i)}, {"angle_rad", b->getAngle()},
                {"bore_m", b->getBore()}, {"deck_height_m", b->getDeckHeight()},
                {"cylinder_count", b->getCylinderCount()}, {"chamber_m3", h->getCombustionChamberVolume()},
                {"intake_runner_m3", h->getIntakeRunnerVolume()}, {"intake_runner_m2", h->getIntakeRunnerCrossSectionArea()},
                {"exhaust_runner_m3", h->getExhaustRunnerVolume()}, {"exhaust_runner_m2", h->getExhaustRunnerCrossSectionArea()},
                {"intake_flow", function(h->m_intakePortFlow)}, {"exhaust_flow", function(h->m_exhaustPortFlow)},
                {"intake_cam", cam(h->getIntakeCamshaft(), b->getCylinderCount())},
                {"exhaust_cam", cam(h->getExhaustCamshaft(), b->getCylinderCount())}}));
        }
        auto *ign = e->getIgnitionModule();
        for (int i = 0; i < e->getCylinderCount(); ++i) {
            auto *p = e->getPiston(i); auto *r = e->getConnectingRod(i);
            require(!r->getMasterRod() && r->getRodJournalCount() == 0 && r->getCrankshaft() == crank,
                    "Exporter subset supports direct connecting rods only");
            const auto bankIndex = p->getCylinderBank()->getIndex();
            auto *h = e->getHead(bankIndex); const auto local = p->getCylinderIndex();
            cylinders.push_back(object({{"id", id("cylinder", i)}, {"bank", id("bank", bankIndex)},
                {"bank_local_index", local}, {"journal", id("journal", r->getJournal())},
                {"intake", intakeIds.at(h->getIntake(local))}, {"exhaust", exhaustIds.at(h->getExhaustSystem(local))},
                {"sound_attenuation", h->getSoundAttenuation(local)}, {"header_length_m", h->getHeaderPrimaryLength(local)},
                {"piston", object({{"mass_kg", p->getMass()}, {"compression_height_m", p->getCompressionHeight()},
                    {"displacement_m3", p->getDisplacement()}, {"wrist_pin_m", p->getWristPinLocation()},
                    {"blowby_k", p->getBlowbyK()}})},
                {"rod", object({{"length_m", r->getLength()}, {"mass_kg", r->getMass()},
                    {"inertia_kg_m2", r->getMomentOfInertia()}, {"center_of_mass_midpoint_offset_m", r->getCenterOfMass()}})}}));
            require(ign->m_plugs[i].enabled, "Unwired cylinder ignition is outside exporter subset");
            ignition.push_back(object({{"cylinder", id("cylinder", i)}, {"angle_rad", ign->m_plugs[i].angle}}));
        }
        auto *v = out.vehicle; auto *t = out.transmission;
        std::vector<Json> gears;
        for (int i = 0; i < t->getGearCount(); ++i) gears.emplace_back(t->getGearRatio(i));
        const auto timing = function(ign->m_timingCurve);
        const auto turbulence = function(fuel->m_turbulenceToFlameSpeedRatio);
        return object({{"schema", "crankwave/engine-sim-resolved-graph/v1"},
            {"subset", "single-crank-direct-rods-fixed-cams-direct-throttle-spark-v1"},
            {"name", e->getName()}, {"redline_rad_s", e->getRedline()},
            {"starter_torque_nm", e->getStarterTorque()}, {"starter_speed_rad_s", e->getStarterSpeed()},
            {"simulation_hz", e->getSimulationFrequency()}, {"throttle_gamma", throttle->m_gamma},
            {"hf_gain", e->getInitialHighFrequencyGain()}, {"jitter", e->getInitialJitter()}, {"noise", e->getInitialNoise()},
            {"flow_calibration", object({{"carb_k_per_cfm", GasSystem::k_carb(1)}, {"port_k_per_cfm", GasSystem::k_28inH2O(1)}})},
            {"crank", object({{"throw_m", crank->getThrow()}, {"mass_kg", crank->getMass()},
                {"flywheel_mass_kg", crank->getFlywheelMass()}, {"inertia_kg_m2", crank->getMomentOfInertia()},
                {"friction_nm", crank->getFrictionTorque()}, {"tdc_rad", crank->getTdc()}})},
            {"journals", array(journals)}, {"banks", array(banks)}, {"intakes", array(intakes)},
            {"exhausts", array(exhausts)}, {"cylinders", array(cylinders)},
            {"ignition", object({{"timing", timing}, {"firing", array(ignition)},
                {"limiter_rad_s", ign->getRevLimit()}, {"cut_seconds", ign->getLimiterDuration()}})},
            {"fuel", object({{"name", fuel->m_name}, {"molecular_mass_kg_mol", fuel->getMolecularMass()},
                {"energy_j_kg", fuel->getEnergyDensity()}, {"density_kg_m3", fuel->getDensity()},
                {"molecular_afr", fuel->getMolecularAfr()}, {"turbulence", turbulence},
                {"efficiency", fuel->getMaxBurningEfficiency()}, {"variation", fuel->getBurningEfficiencyRandomness()},
                {"low_efficiency_attenuation", fuel->getLowEfficiencyAttenuation()},
                {"max_turbulence", fuel->getMaxTurbulenceEffect()}, {"max_dilution", fuel->getMaxDilutionEffect()}})},
            {"functions", array(functions)},
            {"rig", object({{"mass_kg", v->getMass()}, {"drag_coefficient", v->getDragCoefficient()},
                {"area_m2", v->getCrossSectionArea()}, {"diff_ratio", v->getDiffRatio()},
                {"tire_radius_m", v->getTireRadius()}, {"rolling_resistance_n", v->getRollingResistance()},
                {"clutch_torque_nm", t->getMaxClutchTorque()}, {"gears", array(gears)},
                {"dyno_min_rad_s", e->getDynoMinSpeed()}, {"dyno_max_rad_s", e->getDynoMaxSpeed()},
                {"dyno_step_rad_s", e->getDynoHoldStep()}})}});
    }
};

int main(int argc, char **argv) {
    if (argc != 3) { std::cerr << "Usage: resolved-graph main.mr graph.json\n"; return 2; }
    es_script::Compiler compiler;
    bool initialized = false;
    try {
        *es_script::Compiler::output() = {};
        compiler.initialize(); initialized = true;
        if (!compiler.compile(piranha::IrPath(std::filesystem::absolute(argv[1]).string())))
            throw std::runtime_error("MR compilation failed; see error_log.log");
        auto result = compiler.execute();
        const auto graph = CrankwaveResolvedGraph{}.exportGraph(result);
        compiler.destroy(); initialized = false;
        std::ofstream output(argv[2]); output << graph.text << '\n'; output.close();
        if (!output) throw std::runtime_error("Cannot write resolved graph");
        return 0;
    } catch (const std::exception &error) {
        if (initialized) compiler.destroy();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
