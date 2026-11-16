#!/usr/bin/env python3
"""Reproduce the explicit UAZ-469 MR-to-JSON port (not a general MR interpreter)."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'data/engines/uaz469-cleanroom'
# This mapping is reviewed against one immutable source revision. Unknown MR
# edits must be mapped before running the exporter, even on a fresh checkout.
REVIEWED_MR_SHA256 = '102bb4fe7e14fa9a0c453ed859ae30ba40cc9e0d41e08dfba2e0cf5dc7a798ca'
REVIEWED_IR_SHA256 = '1bd5894b04539c7e95631afd5a1dacee94bc203a09c8b1db8a0bc6ba04d68323'
Q = lambda v, u: {'value': v, 'unit': u}

def write(name, data):
    (DEST/name).parent.mkdir(parents=True, exist_ok=True)
    (DEST/name).write_text(json.dumps(data, indent=2)+'\n')

def flow(v):
    return {'type':'flow_bench', 'rated_flow':dict(Q(v,'cfm'), standard='carburetor_1p5_inhg'), 'pressure_drop':Q(1.5,'inHg')}

def curve(name, inputs, outputs, iu, ou, idim, odim, radius, standard=None):
    return {'id':name, 'input_dimension':idim, 'output_dimension':odim,
        'evaluation':'triangle_weighted_samples', 'triangle_filter_radius':Q(radius,iu),
        'below_domain':'clamp', 'above_domain':'clamp',
        'samples':[{'input':Q(i,iu), 'output':dict(Q(o,ou), **({'standard':standard} if standard else {}))} for i,o in zip(inputs,outputs)]}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,required=True)
    parser.add_argument('--variant',choices=['cleanroom','reference-3m'],default='cleanroom')
    args=parser.parse_args()
    src=args.source_root.resolve()
    global DEST
    reference = args.variant == 'reference-3m'
    engine_id = 'uaz469-reference-3m' if reference else 'uaz469-cleanroom'
    mr_name = 'uaz469_reference_3m.mr' if reference else 'uaz469_cleanroom.mr'
    DEST = ROOT / 'data/engines' / engine_id
    source_audio = src/'assets/sound-library/uaz469'
    if reference: source_audio = source_audio/'reference_3m'
    mr=src/'assets/engines'/mr_name
    reviewed_mr = '1022ef727755337c2714f5b26290b86917c2d8edf2e8456aeb5d8ab9417b4447' if reference else REVIEWED_MR_SHA256
    reviewed_ir = 'f547b216ea1f11c6bc90ad60440c27cf67affd94955468455978b91554cb1494' if reference else REVIEWED_IR_SHA256
    source_hash=hashlib.sha256(mr.read_bytes()).hexdigest()
    if source_hash != reviewed_mr:
        raise SystemExit('Unreviewed MR revision: update the explicit parameter mapping first.')
    ir=source_audio/'uaz469_synthetic_exhaust.wav'
    if hashlib.sha256(ir.read_bytes()).hexdigest() != reviewed_ir:
        raise SystemExit('Unreviewed exhaust IR revision: review its matching MR gain first.')
    expected=DEST/'source'/mr_name
    DEST.mkdir(parents=True,exist_ok=True)
    (DEST/'source').mkdir(exist_ok=True)
    shutil.copyfile(mr,expected)
    shutil.copyfile(ir,DEST/'uaz469_synthetic_exhaust.wav')
    shutil.copyfile(src/'tools/generate_uaz469_ir.py',DEST/'source/generate_uaz469_ir.py')
    shutil.copyfile(source_audio/'reference_eq.json',DEST/'source/reference_eq.json')
    shutil.copyfile(src/'docs/uaz469-cleanroom.md',DEST/'source/uaz469-cleanroom.md')
    if reference:
        shutil.copyfile(source_audio/'README.md',DEST/'source/reference_3m.md')
        shutil.copyfile(source_audio/'manifest.json',DEST/'source/reference_3m_manifest.json')
    baseline=json.loads((ROOT/'data/engines/kohler-ch750-cleanroom/engine.json').read_text())
    accessory=json.loads((ROOT/'data/engines/kohler-ch750-cleanroom/accessory-configuration.json').read_text())
    accessory['engine_id']=engine_id
    accessory['configuration_id']=engine_id+'-warm-generic-accessories-v1'
    accessory['claim_boundary']['statement']='Crankwave requires its generic Chen-Flynn warm-running aggregate loss policy. These coefficients are executor policy, not measurements from the UAZ MR model. The source crank friction torque remains explicitly authored; aggregate-loss execution does not claim identical free-running dynamics to engine-sim.'
    write('accessory-configuration.json',accessory)
    phases=[0,180,540,360]
    e={'identity':{'id':engine_id,'display_name':'UAZ-469 UMZ-451M 2.45L (3:00 reference)' if reference else 'UAZ-469 UMZ-451M 2.45L (MR port)',
        'description':f'Explicit port of engine-sim {mr_name}. Preserves 92 x 92 mm geometry, 6.7:1 compression, 1-2-4-3 firing, 184/190 degree effective OHV cams, port flow, carburetor, exhaust, ignition and synthetic IR. Crankwave requires an explicit generic Chen-Flynn loss policy; therefore this is not a claim of byte-identical simulation or measured UAZ acoustics. See PORT.md and provenance.json.'},
        'cycle':'four_stroke','layout':'inline','limits':{'redline':Q(4200,'rpm')},
        'curves':[
            curve('intake-valve-flow',range(10),[x*1.2 for x in [0,19,37,54,69,81,90,96,100,102]],'mm','cfm','length','volume_flow_rate',1,'port_28_inh2o'),
            curve('exhaust-valve-flow',range(10),[x*1.2 for x in [0,16,30,43,54,63,70,75,78,80]],'mm','cfm','length','volume_flow_rate',1,'port_28_inh2o'),
            curve('ignition-timing',[0,600,1000,1500,2200,2500,3000,3500,4000,4500],[0,4,8,9,12,14,18,24,30,30],'rpm','deg','angular_speed','angle',500),
            curve('turbulence-to-flame-speed',range(0,50,5),[3]+[x*1.5 for x in range(5,50,5)],'1','m/s','dimensionless','speed',5)],
        'crankshafts':[{'id':'crank','throw_radius':Q(46,'mm'),'mass':Q(21,'kg'),'flywheel_mass':Q(14,'kg'),
            'moment_of_inertia':Q(.5*(14*.155**2+21*.092**2+4*.09**2),'kg*m2'),'friction_torque':Q(14,'N*m'),'tdc_reference_angle':Q(90,'deg')}],
        'output_crankshaft':'crank',
        'journals':[{'id':f'j{i+1}','type':'crankshaft','crankshaft':'crank','phase':Q(p,'deg')} for i,p in enumerate([0,180,180,0])],
        'connecting_rods':[{'id':'rod','length':Q(175,'mm'),'mass':Q(.88,'kg'),'moment_of_inertia':Q(.88*.175**2/12,'kg*m2'),'center_of_mass_from_crank_pin':Q(87.5,'mm')}],
        'pistons':[{'id':'piston','mass':Q(.72,'kg'),'compression_height':Q(51,'mm'),'displacement_volume':Q(0,'cm3'),
            'blowby':{'type':'flow_bench','rated_flow':dict(Q(.03,'cfm'),standard='port_28_inh2o'),'pressure_drop':Q(28,'inH2O')}}],
        'banks':[{'id':'bank','angle':Q(0,'deg'),'bore':Q(92,'mm'),'deck_height':Q(272,'mm'),'head':'head'}],
        'intakes':[{'id':'carburetor','plenum_volume':Q(1.2,'L'),'plenum_cross_section_area':Q(14,'cm2'),'runner_length':Q(200,'mm'),
            'main_restriction':flow(200),'idle_bypass_restriction':flow(0),'runner_restriction':flow(145),'idle_throttle_position_01':.99035,'runner_velocity_decay_01':.5,'main_mixture_lambda':1}],
        'exhausts':[{'id':'exhaust','collector_length':Q(2400,'mm'),'collector_cross_section_area':Q(math.pi*22**2,'mm2'),
            'primary_tube_length':Q(320,'mm'),'outlet_restriction':flow(190),'primary_restriction':flow(135),'velocity_decay_01':.85}],
        'ports':[{'id':kind+'-port','head':'head','kind':kind,'runner_volume':Q(volume,'cm3'),'runner_cross_section_area':Q(area,'cm2'),'flow_curve':kind+'-valve-flow'} for kind,volume,area in [('intake',110,6),('exhaust',85,5.2)]],
        'cam_lobes':[{'id':f'{kind}-lobe-{i+1}','cylinder':f'cylinder-{i+1}','port_kind':kind,'centerline':Q(center+p,'deg'),
            'type':'harmonic','duration_at_reference_lift':Q(duration,'deg'),'reference_lift':Q(.05,'in'),'maximum_lift':Q(8.5,'mm'),'gamma':1,'sample_count':256}
            for kind,center,duration in [('intake',470,184),('exhaust',252,190)] for i,p in enumerate(phases)],
        'camshafts':[{'id':kind+'-camshaft','advance':Q(0,'deg'),'base_radius':Q(17,'mm'),'lobes':[f'{kind}-lobe-{i}' for i in range(1,5)]} for kind in ['intake','exhaust']],
        'valvetrains':[{'id':'valvetrain','type':'standard','intake_camshaft':'intake-camshaft','exhaust_camshaft':'exhaust-camshaft'}],
        'heads':[{'id':'head','chamber_volume':Q(math.pi*9.2**3/4/(6.7-1),'cm3'),'valvetrain':'valvetrain','ports':['intake-port','exhaust-port']}],
        'fuels':[{'id':'low-compression-petrol','display_name':'Low-compression petrol','molecular_mass':Q(100,'g/mol'),'lower_heating_value':Q(44,'MJ/kg'),
            'stoichiometric_air_fuel_molar_ratio':12.5,'turbulence_to_flame_speed':'turbulence-to-flame-speed',
            'combustion':{'maximum_efficiency_01':.78,'cycle_variation_01':.08,'low_efficiency_attenuation_01':.72,'maximum_turbulence_effect':2,'maximum_dilution_effect':10}}],
        'default_fuel':'low-compression-petrol',
        'accessory_configurations':[{'id':'warm-generic-accessories','uri':'accessory-configuration.json','sha256':hashlib.sha256((DEST/'accessory-configuration.json').read_bytes()).hexdigest()}],
        'losses':baseline['engine']['losses'],
        'ignition':{'timing_curve':'ignition-timing','wires':[{'id':f'wire-{i}'} for i in range(1,5)],
            'firing_order':[{'wire':f'wire-{i}','crank_angle':Q(p,'deg')} for i,p in [(1,0),(2,180),(4,360),(3,540)]],
            'limiter':{'activation_speed':Q(4500,'rpm'),'cut_duration':Q(.1,'s')}},
        'throttle_controllers':[{'id':'direct-throttle','type':'direct','gamma':1.5}],'throttle_controller':'direct-throttle',
        'starter':{'type':'cranking','torque':Q(125,'N*m'),'target_speed':Q(220,'rpm')},
        'cylinders':[{'id':f'cylinder-{i+1}','bank':'bank','journal':f'j{i+1}','connecting_rod':'rod','piston':'piston','intake':'carburetor','exhaust':'exhaust',
            'ignition_wire':f'wire-{i+1}','intake_port':'intake-port','exhaust_port':'exhaust-port','exhaust_header_primary_length':Q(p,'mm')} for i,p in enumerate([440,310,260,390])],
        'source_routes':[{'id':'exhaust','type':'exhaust','exhaust':'exhaust'}]}
    p={'assets':[{'id':'uaz469-synthetic-exhaust','kind':'impulse_response','uri':'uaz469_synthetic_exhaust.wav',
          'sha256':hashlib.sha256((DEST/'uaz469_synthetic_exhaust.wav').read_bytes()).hexdigest()}],
       'cylinder_routes':[{'cylinder':f'cylinder-{i+1}','route':'exhaust','gain_linear':g} for i,g in enumerate([.98,1.02,.99,1.01])],
       'routes':[{'route':'exhaust','source_gain_linear':1,'impulse_response':'uaz469-synthetic-exhaust','impulse_response_gain_linear':(.05518654 if reference else .00956941),'wet_mix_01':1}],
       'conditioning':{'jitter_scale':.010,'jitter_modulation_cutoff_frequency':Q(10000,'Hz'),'derivative_mix_01':.00012,'air_noise_mix_01':.08,'air_noise_cutoff_frequency':Q(2000,'Hz')},
       'buses':[{'id':name,'routes':['exhaust'],'gain_linear':1,'publish':True} for name in ['master-engine-raw','master-engine-audition']],
       'audition':{'buses':['master-engine-audition'],'volume_linear':1,'fade_in':Q(.02,'s'),'fade_out':Q(.02,'s')},'publication_gain_linear':2**-26}
    rig={'id':'uaz469-high-range','vehicle':{'id':'uaz469','mass':Q(1600,'kg'),'drag_coefficient':.6,'frontal_area':Q(2.65,'m2'),'differential_ratio':5.38,'tire_radius':Q(390,'mm'),'rolling_resistance_force':Q(260,'N')},
        'transmission':{'id':'four-speed-manual','maximum_clutch_torque':Q(260,'N*m'),'gears':[{'id':f'gear-{i+1}','ratio':r} for i,r in enumerate([4.124,2.641,1.58,1])]},
        'dyno_defaults':{'minimum_engine_speed':Q(600,'rpm'),'maximum_engine_speed':Q(4200,'rpm'),'hold_step':Q(100,'rpm')}}
    write('engine.json',{'schema':'crankwave/engine','engine':e,'presentation':p,'rig':rig})
    scenario=json.loads((ROOT/'data/engines/bmw-m52tub28-cleanroom/scenarios/held-idle-region-700rpm.json').read_text())
    scenario.update(id='uaz469-held-765rpm',engine=engine_id,fuel=e['default_fuel'],public_seed='469')
    scenario['initial_state']['engine_speed']=Q(765,'rpm')
    scenario['mode']['target_engine_speed']=Q(765,'rpm')
    scenario['preparation']['preparation_duration']=Q(3,'s')
    scenario['preparation']['trailing_complete_cycle_count']=8
    scenario.update(total_duration=Q(6,'s'),audible_start=Q(3,'s'),audible_duration=Q(3,'s'))
    scenario['events'][0]['id']='uaz469-running'
    write('scenarios/held-765rpm.json',scenario)
    write('provenance.json',{'source_mr_sha256':source_hash,'source_path':'engine-sim/assets/engines/'+mr_name,
        'synthetic_ir_sha256':p['assets'][0]['sha256'],'engine_json_sha256':hashlib.sha256((DEST/'engine.json').read_bytes()).hexdigest(),
        'port_kind':'explicit-authored-parameter-mapping','source_recordings_used':False,
        'source_revision':'2026-09-08-reference-3m' if reference else '2026-09-08-pressure-led-restrained-rasp',
        'authored_idle_target_rpm':765,
        'reference_magnitude_eq_used':True,
        'reference_eq_sha256':hashlib.sha256((DEST/'source/reference_eq.json').read_bytes()).hexdigest(),
        'ir_generator_sha256':hashlib.sha256((DEST/'source/generate_uaz469_ir.py').read_bytes()).hexdigest(),
        'differences':['Crankwave mandatory Chen-Flynn aggregate loss prior is not the original simulator loss model.',
                       'Fuel density is GUI telemetry only in engine-sim and is omitted.',
                       'Rod center_of_mass=0 is a midpoint offset in MR: 87.5 mm measured from crank pin in JSON.',
                       'Crankwave publication gain and conditioning-cutoff defaults use its canonical presentation contract.',
                       'The held-idle validation scenario specifies 20 kHz physics; the native carrier baker uses its 10 kHz interactive preview profile with 192 kHz presentation. Godot playback uses a derived bank.']})
    print(DEST/'engine.json')

if __name__=='__main__': main()
