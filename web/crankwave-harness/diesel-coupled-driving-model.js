// Authored heavy-vehicle listening rig. Ratios/road resistance are carried with
// the engine; they are not a measured Ural powertrain or a production vehicle solver.
const TAU = 2*Math.PI;
const STEP = 1/1000;
const clamp = (x,a,b) => Math.max(a,Math.min(b,x));
const smooth = (a,b,x) => {const t=clamp((x-a)/(b-a),0,1);return t*t*(3-2*t);};
const approach = (x,y,delta) => x+clamp(y-x,-delta,delta);

export class DieselCoupledDrivingModel {
  constructor(runtime, bank) {
    this.runtime = runtime;
    this.bank = bank;
    this.profile = runtime.drivePackage.manifest;
    this.minimumRpm = runtime.minimumRpm;
    this.maximumRpm = runtime.maximumRpm;
    this.reset();
  }

  reset() {
    this.selector='D'; this.gear=1; this.speed=0; this.distance=0;
    this.rpm=this.profile.vehicle.idle_rpm;
    this.throttle=0; this.brake=0; this.pedal=0; this.clutch=0;
    this.fuel=.12; this.shiftElapsed=-1; this.gearSeconds=0;
    this.backdrive=0; this.coupling=0; this.accumulator=0;
    return this.snapshot();
  }

  setControls({selector,throttle01,brake01}) {
    if (!['N','D'].includes(selector) || ![throttle01,brake01].every(x=>Number.isFinite(x)&&x>=0&&x<=1))
      throw new RangeError('Invalid diesel driving controls');
    if (selector !== this.selector) {
      this.clutch=0; this.shiftElapsed=-1; this.coupling=0;
      this.gear=1;
      while (this.gear<this.profile.vehicle.shaft_rpm_per_mps.length &&
        this.shaftRpm() > this.profile.vehicle.shift_rpm) ++this.gear;
      this.gearSeconds=0;
    }
    this.selector=selector; this.throttle=throttle01; this.brake=brake01;
    return this.snapshot();
  }

  shaftRpm() { return this.speed*this.profile.vehicle.shaft_rpm_per_mps[this.gear-1]; }

  gasTorque(rpm, fuel) {
    const rows=this.runtime.rows;
    let ri=0; while(ri<rows.length-2&&rpm>rows[ri+1].rpm)++ri;
    const sample=row=>{
      const cells=row.cells;
      let i=0; while(i<cells.length-2&&fuel>cells[i+1].fueling_01)++i;
      const a=cells[i],b=cells[i+1],w=(fuel-a.fueling_01)/(b.fueling_01-a.fueling_01);
      return a.indicated_work_j+(b.indicated_work_j-a.indicated_work_j)*w;
    };
    const a=rows[ri],b=rows[ri+1],w=(rpm-a.rpm)/(b.rpm-a.rpm);
    return (sample(a)*(1-w)+sample(b)*w)*this.runtime.manifest.cylinders.length/(2*TAU);
  }

  operatingPoint() {
    return {rpm:this.rpm,throttle01:this.pedal,load01:this.fuel,
      backdriveTorqueNm:this.backdrive,coupling01:this.coupling};
  }

  snapshot() {
    return {selector:this.selector,gear:this.selector==='N'?'N':String(this.gear),
      speedKph:this.speed*3.6,distanceM:this.distance,rpm:this.rpm,throttle01:this.throttle,
      brake01:this.brake,clutch01:this.clutch,load01:this.fuel,shifting:this.shiftElapsed>=0,
      backdriveTorqueNm:this.backdrive,coupling01:this.coupling,limiterCutActive:false,limiterCuts:0};
  }

  advance(seconds) {
    if (!Number.isFinite(seconds)||seconds<0||seconds>1) throw new RangeError('Invalid driving timestep');
    this.accumulator+=seconds;
    while(this.accumulator+1e-12>=STEP) {
      this.step(); this.accumulator=Math.max(0,this.accumulator-STEP);
    }
    return this.operatingPoint();
  }

  step() {
    const v=this.profile.vehicle,b=this.profile.backdrive,dt=STEP;
    this.pedal=approach(this.pedal,this.brake>.1?0:this.throttle,dt*(this.throttle>this.pedal?1.25:5));
    this.gearSeconds+=dt;
    let shaft=this.shaftRpm();
    if (this.selector==='D'&&this.shiftElapsed<0&&this.gearSeconds>=v.minimum_gear_seconds) {
      let next=this.gear;
      if (shaft>v.shift_rpm&&this.gear<v.shaft_rpm_per_mps.length) ++next;
      else if (shaft<v.idle_rpm+100&&this.gear>1&&
        this.speed*v.shaft_rpm_per_mps[this.gear-2]<v.shift_rpm-150) --next;
      if (next!==this.gear) {this.targetGear=next;this.shiftElapsed=0;this.gearSeconds=0;}
    }
    let shiftClutch=1;
    if(this.shiftElapsed>=0) {
      this.shiftElapsed+=dt;
      if(this.shiftElapsed>=v.shift_seconds*.45) this.gear=this.targetGear;
      shiftClutch=smooth(v.shift_seconds*.65,v.shift_seconds,this.shiftElapsed);
      if(this.shiftElapsed>=v.shift_seconds) this.shiftElapsed=-1;
    }
    shaft=this.shaftRpm();
    const scale=v.shaft_rpm_per_mps[this.gear-1], k=scale*TAU/60;
    const reference=v.shaft_rpm_per_mps[v.inertia_reference_gear-1]*TAU/60;
    const iv=b.vehicleReflectedInertiaKgM2*(reference/k)**2;
    const mass=b.vehicleReflectedInertiaKgM2*reference**2;
    const road=b.reflectedRoadDragNm*reference/k;
    const launch=v.idle_rpm+this.pedal*350;
    const targetClutch=this.selector==='N'?0:shiftClutch*Math.max(
      smooth(v.idle_rpm*.55,launch,shaft),this.pedal*.8);
    this.clutch=approach(this.clutch,targetClutch,dt*(targetClutch<this.clutch?8:2));
    const neutralTarget=v.idle_rpm+this.pedal*(this.maximumRpm-v.idle_rpm)*.96;
    const target=this.selector==='N'?neutralTarget:Math.max(launch,shaft);
    this.rpm=approach(this.rpm,clamp(target,this.minimumRpm,this.maximumRpm),dt*(this.selector==='N'?900:2400));
    const idleAssist=1-smooth(v.idle_rpm+50,v.idle_rpm+250,this.rpm);
    const noLoad=.12+.1*clamp((this.rpm-v.idle_rpm)/(this.maximumRpm-v.idle_rpm),0,1);
    const demand=this.pedal*shiftClutch*(1-smooth(v.shift_rpm,this.maximumRpm,this.rpm));
    const baseline=noLoad*Math.max(idleAssist,smooth(0,.05,demand));
    this.fuel=clamp(baseline+(1-baseline)*demand**1.5*(this.selector==='N'?.15:this.clutch),0,1);
    const drag=-this.bank.mean(this.rpm)+b.frictionConstantNm+b.frictionNmPerRpm*this.rpm;
    // Blend propulsion into the exact native motored drag used by the accepted coast.
    const torque=this.fuel===0?-drag:this.gasTorque(this.rpm,this.fuel)-b.frictionConstantNm-b.frictionNmPerRpm*this.rpm;
    let acceleration;
    const locked=this.selector==='D'&&this.clutch>.995&&shaft>=v.idle_rpm&&this.shiftElapsed<0;
    if (locked) {
      const angular=(torque-road-this.brake*4*mass/k)/(b.engineInertiaKgM2+iv);
      acceleration=angular/k;
      this.backdrive=Math.max(0,-torque+b.engineInertiaKgM2*angular)*(1-smooth(0,.06,this.fuel));
    } else {
      acceleration=(this.selector==='D'?torque*this.clutch*k:0)/mass-road*k/mass-this.brake*4;
      this.backdrive=0;
    }
    this.speed=Math.max(0,this.speed+acceleration*dt);
    // A locked clutch ties crank speed directly to the wheels in either torque direction.
    if(locked) this.rpm=clamp(this.shaftRpm(),v.idle_rpm,this.maximumRpm);
    this.coupling=approach(this.coupling,this.backdrive>0?1:0,dt/b.takeupSeconds);
    this.distance+=this.speed*dt;
  }
}
