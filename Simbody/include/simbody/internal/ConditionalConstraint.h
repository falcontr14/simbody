#ifndef SimTK_SIMBODY_CONDITIONAL_CONSTRAINT_H_
#define SimTK_SIMBODY_CONDITIONAL_CONSTRAINT_H_

/* -------------------------------------------------------------------------- *
 *                               Simbody(tm)                                  *
 * -------------------------------------------------------------------------- *
 * This is part of the SimTK biosimulation toolkit originating from           *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org/home/simbody.  *
 *                                                                            *
 * Portions copyright (c) 2014 Stanford University and the Authors.           *
 * Authors: Michael Sherman                                                   *
 * Contributors:                                                              *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0.         *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

#include "SimTKmath.h"
#include "simbody/internal/common.h"
#include "simbody/internal/Constraint.h"
#include "simbody/internal/Constraint_BuiltIns.h"
#include "simbody/internal/MobilizedBody.h"

namespace SimTK {

/** TODO: Simbody model element representing a conditionally-enforced 
constraint.
**/
class SimTK_SIMBODY_EXPORT ConditionalConstraint {
public:

/** Given the specified minimum coefficient of restitution (COR), capture speed,
and the speed at which the minimum COR is attained, calculate an effective COR 
for a given impact speed. All speeds must be nonnegative. The COR will be zero
at or below capture speed, will be minCOR at or above minCORSpeed, and will 
rise linearly with decreasing impact speed between minCORSpeed and the capture
velocity. **/
static Real calcEffectiveCOR(Real minCOR, Real captureSpeed, Real minCORSpeed, 
                             Real impactSpeed)
{
    assert(0 <= minCOR && minCOR <= 1);
    assert(0 <= captureSpeed && captureSpeed <= minCORSpeed);
    assert(impactSpeed >= 0);

    if (impactSpeed <= captureSpeed) return 0;
    if (impactSpeed >= minCORSpeed)  return minCOR;
    // captureSpeed < impactSpeed < minCORSpeed
    const Real slope = (1-minCOR) / minCORSpeed;
    return 1 - slope*impactSpeed;
}

/** Given the coefficients of friction and slip-to-rolling transition 
speed, calculate the effective COF mu for a given slip velocity. Speeds
must be nonnegative. This utility calculates mu with an abrupt rise at
the transitionSpeed from the dynamic coefficient mu_d to the static 
coefficient mu_s. There is also a linear contribution mu_v*slipSpeed. **/
static Real calcEffectiveCOF(Real mu_s, Real mu_d, Real mu_v,
                             Real transitionSpeed, Real slipSpeed)
{
    assert(mu_s>=0 && mu_d>=0 && mu_v>=0);
    assert(mu_s>=mu_d);
    assert(transitionSpeed>=0 && slipSpeed>=0);
    const Real viscous = mu_v*slipSpeed; // typically zero
    return viscous + (slipSpeed <= transitionSpeed ? mu_s : mu_d);
}

};

class UnilateralContact; // normal + friction
class UnilateralSpeedConstraint;
class BoundedSpeedConstraint;

class ConstraintLimitedFriction;
class StateLimitedFriction;

//==============================================================================
//                       UNILATERAL CONTACT CONSTRAINT
//==============================================================================
/** (Experimental -- API will change -- use at your own risk) 
A unilateral contact constraint uses a single holonomic (position) 
constraint equation to prevent motion in one direction while leaving it 
unrestricted in the other direction. Examples are surface-surface contact, joint
stops, and inextensible ropes. These constraints are subject to violent impacts
that are treated with a coefficient of restitution that may be state dependent.

Some unilateral contacts may be associated with one or more friction elements
that are dependent on the normal force generated by the contact. Whenever the
unilateral contact is inactive (meaning its associated multiplier is zero), its
associated friction elements are also inactive.

There are two possible sign conventions, depending on the underlying Constraint
element definition. The default (sign=1) is to consider the constraint
position error (perr) to be a signed distance function, meaning that perr>=0
is valid and perr<0 is a violation. Similarly verr>=0 is separation while
verr<0 is approach or penetration velocity, and aerr>=0 is separation 
acceleration. The corresponding force should also be >= 0, but since constraint
multipliers have the opposite sign from applied forces that means the inequality
restricting the multiplier is lambda<=0; lambda>0 would produce an attractive 
force. That would suck, so is not allowed. In the opposite sign convention 
(sign=-1), the inequalities that must be satisfied are perr,verr,aerr<=0 and 
lambda>=0. So the constraints to be enforced are:
<pre>
    sign*perr >= 0 (always)
    sign*verr >= 0 (if perr==0)
    if perr==verr==0 then:
        sign*aerr >= 0 && -sign*lambda >= 0 && aerr*lambda==0
</pre>
In practice we enforce constraints up to a tolerance, so the zeroes above are
not enforced exactly.
**/
class SimTK_SIMBODY_EXPORT UnilateralContact {
public:
    /** The base class constructor allows specification of the sign convention
    to be used with this constraint. The sign convention cannot be changed
    later. See the class documentation for more information. **/
    explicit UnilateralContact(int sign=1) : m_sign((Real)sign) 
    {   assert(sign==1 || sign==-1); }

    virtual ~UnilateralContact() {}

    /** Report the sign convention (1 or -1) supplied at construction. **/
    Real getSignConvention() const {return m_sign;}

    /** Disable the normal and friction constraints if they were enabled. 
    Return true if we actually had to disable something. **/
    virtual bool disable(State& state) const = 0;

    /** Enable the normal and friction constraints if they were disabled. 
    Return true if we actually had to enable something. **/
    virtual bool enable(State& state) const = 0;

    /** Return true if this contact is enabled. **/
    virtual bool isEnabled(const State& state) const = 0;

    /** This returns a point in the ground frame at which you might want to
    say the constraint is "located", for purposes of display only. **/
    virtual Vec3 whereToDisplay(const State& state) const = 0;

    /** Returns the effective coefficient of restitution (COR) for this contact,
    given an impact speed (a nonnegative scalar). For a given pair of contacting 
    materials this is typically a function of just the impact speed, but it
    could also depend on the time and configuration in \a state, which should 
    be realized through Stage::Position. The given default impact speed
    thresholds (also nonnegative) are used to calculate the COR unless this 
    Contact overrides those. **/
    virtual Real calcEffectiveCOR(const State& state,
                                  Real defaultCaptureSpeed,
                                  Real defaultMinCORSpeed,
                                  Real impactSpeed) const = 0;

    /** Return the position error for the contact constraint (usually a signed 
    distance function). You have to apply the sign convention to interpret this 
    properly. **/
    virtual Real getPerr(const State& state) const = 0;
    /** Return the time derivative of the contact constraint position error. You 
    have to apply the sign convention to interpret this properly. **/
    virtual Real getVerr(const State& state) const = 0;
    /** Return the time derivative of the contact constraint velocity error. You 
    have to apply the sign convention to interpret this properly. **/
    virtual Real getAerr(const State& state) const = 0;

    /** Given the position constraint tolerance currently in use, is this 
    contact close enough to contacting that we should treat it as though
    it is in contact? Normally we just see if sign*perr <= tol, but individual
    contacts can override this if they want to do some scaling. **/
    virtual bool isProximal(const State& state, Real ptol) const 
    {   return m_sign*getPerr(state) <= ptol; }

    /** Return the multiplier index Simbody assigned for the unilateral 
    contact constraint (for contact, this is the normal constraint). If the
    constraint is not enabled, there is no multiplier and the returned index
    will be invalid. **/
    virtual MultiplierIndex 
    getContactMultiplierIndex(const State& state) const = 0;

    /** Returns \c true if there is a friction constraint associated with this
    contact constraint. If so, calcEffectiveCOF() must be overridden. **/
    virtual bool hasFriction(const State& state) const {return false;}

    /** Returns the effective coefficient of friction mu for this contact,
    given a relative slip speed (a nonnegative scalar). For a given pair of 
    contacting materials this is typically a function of just the slip speed, 
    but it could also depend on the time and configuration in \a state, which 
    should be realized through Stage::Position. The given default slip-to-roll
    transition speed threshold (also nonnegative) is used to calculate mu 
    unless this Contact overrides it with its own transition speed. **/
    virtual Real calcEffectiveCOF(const State& state,
                                  Real defaultTransitionSpeed,
                                  Real slipSpeed) const
    {   return NaN; }

    virtual Vec2 getSlipVelocity(const State& state) const 
    {   return Vec2(NaN); }

    /** If hasFriction(), this method returns the multipliers used for the
    x- and y-direction friction constraints. If no friction, or if this
    constraint is disabled, the returned values are invalid. **/
    virtual void
    getFrictionMultiplierIndices(const State&       state, 
                                 MultiplierIndex&   ix_x, 
                                 MultiplierIndex&   ix_y) const
    {   ix_x.invalidate(); ix_y.invalidate(); }

    /** TODO: kludge needed because we're misusing existing constraints. 
    This must be called while Stage::Position is valid. **/
    virtual Vec3 getPositionInfo(const State& state) const 
    {   return Vec3(NaN); }
    /** TODO: kludge to set instance parameters on internal constraints;
    this should be the same Vec3 you got from getPositionInfo(). **/
    virtual void setInstanceParameter(State& state, const Vec3& pos) const {}


    void setMyIndex(UnilateralContactIndex cx) {m_myIx = cx;}
    UnilateralContactIndex getMyIndex() const {return m_myIx;}
private:
    Real                    m_sign; // 1 or -1
    UnilateralContactIndex  m_myIx;
};

//==============================================================================
//                       UNILATERAL SPEED CONSTRAINT
//==============================================================================
/** TODO: not implemented yet. 

A unilateral speed constraint uses a single nonholonomic (velocity)
constraint equation to prevent relative slip in one direction but not in the 
other. Examples are ratchets and mechanical diodes.
**/
class SimTK_SIMBODY_EXPORT UnilateralSpeedConstraint {
public:
    UnilateralSpeedConstraint() {}
    virtual ~UnilateralSpeedConstraint() {}

private:
};

//==============================================================================
//                         BOUNDED SPEED CONSTRAINT
//==============================================================================
/** TODO: not implemented yet.

A bounded speed constraint uses a single nonholonomic (velocity) constraint
equation to prevent relative slip provided it can do so while keeping the
generated force within a range given by a lower and upper bound. Outside that
range the connection will slip and the force value will be one of the
bounds, depending on the slip direction. An example is a torque-limited 
speed control motor. Recall that multipliers lambda have the opposite sign
convention from applied forces. We enforce:
<pre>
    lower <= -lambda <= upper and verr=0
    or verr > 0 and -lambda=lower
    or verr < 0 and -lambda=upper
</pre>
The bounds (lower,upper) can be state dependent, for example, they may be
dependent on the current slip velocity. When lower=-upper, this is just 
a restriction on the magnitude |lambda|, like a friction constraint where the
normal force is known.

This constraint is workless when it is able to prevent slip with the force
in range; it is maximally dissipative otherwise because the constraint force
opposes the slip velocity.
**/
class SimTK_SIMBODY_EXPORT BoundedSpeedConstraint {
public:
    BoundedSpeedConstraint() {}
    virtual ~BoundedSpeedConstraint() {}

    /** Return the currently effective lower and upper bounds on the
    associated multiplier as a Vec2(lower,upper). The bounds may depend on
    time, position, and velocity taken from the given \a state.
    **/
    virtual Vec2 calcEffectiveBounds(const State& state) const = 0;
private:
};

//==============================================================================
//                          STATE LIMITED FRICTION
//==============================================================================
/** TODO: not implemented yet **/
class SimTK_SIMBODY_EXPORT StateLimitedFriction {
public:
    StateLimitedFriction() {}
    virtual ~StateLimitedFriction() {}

    /** Disable the friction constraints if they were enabled. Return true if 
    we actually had to disable something. **/
    virtual bool disable(State& state) const = 0;

    /** Enable the friction constraints if they were disabled. Return true if 
    we actually had to enable something. **/
    virtual bool enable(State& state) const = 0;

    /** Return the current value of the state-dependent normal force 
    magnitude that limits this friction element. **/
    virtual Real getNormalForceMagnitude(const State& state) const = 0;

    virtual Real calcEffectiveCOF(const State& state,
                                  Real defaultTransitionSpeed,
                                  Real slipSpeed) const = 0;

    virtual Real getSlipSpeed(const State& state) const = 0; 

    /** TODO: kludge needed because we're misusing existing constraints. 
    This must be called while Stage::Position is valid. **/
    virtual Vec3 getPositionInfo(const State& state) const 
    {   return Vec3(NaN); }
    /** TODO: kludge to set instance parameters on internal constraints;
    this should be the same Vec3 you got from getPositionInfo(). **/
    virtual void setInstanceParameter(State& state, const Vec3& pos) const {}

    void setMyIndex(StateLimitedFrictionIndex fx) {m_myIx = fx;}
    StateLimitedFrictionIndex getMyIndex() const {return m_myIx;}
private:
    StateLimitedFrictionIndex   m_myIx;
};


//==============================================================================
//                              HARD STOP UPPER
//==============================================================================
/** (Experimental -- API will change -- use at your own risk) 
Set a hard limit on the maximum value of a generalized coordinate q. A
generalized force opposes further excursion of the coordinate, and a generalized
impulse is produced when the stop is hit with a non-zero velocity (an impact).
A coefficient of restitution (COR) e, with 0<=e<=1 is specified that 
determines the rebound impulse that occurs as a result of impact. The COR is 
typically velocity-dependent. The given value is the COR at high impact 
velocities; it will be higher for low impact velocities but zero at very small
impact velocities. 

The sign convention for this unilateral constraint is negative, meaning that
perr,verr,aerr<=0, lambda>=0 are the good directions. **/
class SimTK_SIMBODY_EXPORT HardStopUpper : public UnilateralContact {
public:
    HardStopUpper(MobilizedBody& mobod, MobilizerQIndex whichQ,
             Real defaultUpperLimit, Real minCOR);

    bool disable(State& state) const OVERRIDE_11 
    {   if (m_upper.isDisabled(state)) return false;
        else {m_upper.disable(state); return true;} }
    bool enable(State& state) const OVERRIDE_11 
    {   if (!m_upper.isDisabled(state)) return false;
        else {m_upper.enable(state); return true;} }
    bool isEnabled(const State& state) const OVERRIDE_11 
    {   return !m_upper.isDisabled(state); }

    // Returns the contact point in the Ground frame.
    Vec3 whereToDisplay(const State& state) const OVERRIDE_11;

    // Currently have to fake the perr because the constraint might be
    // disabled in which case it won't calculate perr. Also, we want 
    // negative to mean violated so may need to adjust the sign.
    Real getPerr(const State& state) const OVERRIDE_11;
    Real getVerr(const State& state) const OVERRIDE_11;
    Real getAerr(const State& state) const OVERRIDE_11;

    Real calcEffectiveCOR(const State& state,
                          Real defaultCaptureSpeed,
                          Real defaultMinCORSpeed,
                          Real impactSpeed) const OVERRIDE_11 
    {
       return ConditionalConstraint::calcEffectiveCOR
               (m_minCOR, defaultCaptureSpeed, defaultMinCORSpeed,
                impactSpeed);
    }

    MultiplierIndex getContactMultiplierIndex(const State& s) const OVERRIDE_11;

private:
    MobilizedBody                   m_mobod;
    Real                            m_defaultUpperLimit;
    Real                            m_minCOR;
    Constraint::ConstantCoordinate  m_upper;
};

//==============================================================================
//                              HARD STOP LOWER
//==============================================================================
/** (Experimental -- API will change -- use at your own risk) 
Set a hard limit on the minimum value of a generalized coordinate q. A
generalized force opposes further excursion of the coordinate, and a generalized
impulse is produced when the stop is hit with a non-zero velocity (an impact).
A coefficient of restitution (COR) e, with 0<=e<=1 is specified that 
determines the rebound impulse that occurs as a result of impact. The COR is 
typically velocity-dependent. The given value is the COR at high impact 
velocities; it will be higher for low impact velocities but zero at very small
impact velocities. 

The sign convention for this unilateral constraint is positive, meaning that
perr,verr,aerr>=0, lambda<=0 are the good directions. **/
class SimTK_SIMBODY_EXPORT HardStopLower : public UnilateralContact {
public:
    HardStopLower(MobilizedBody& mobod, MobilizerQIndex whichQ,
                  Real defaultLowerLimit, Real minCOR);

    bool disable(State& state) const OVERRIDE_11 
    {   if (m_lower.isDisabled(state)) return false;
        else {m_lower.disable(state); return true;} }
    bool enable(State& state) const OVERRIDE_11 
    {   if (!m_lower.isDisabled(state)) return false;
        else {m_lower.enable(state); return true;} }
    bool isEnabled(const State& state) const OVERRIDE_11 
    {   return !m_lower.isDisabled(state); }

    // Returns the contact point in the Ground frame.
    Vec3 whereToDisplay(const State& state) const OVERRIDE_11;

    // Currently have to fake the perr because the constraint might be
    // disabled in which case it won't calculate perr. Also, we want 
    // negative to mean violated so may need to adjust the sign.
    Real getPerr(const State& state) const OVERRIDE_11;
    Real getVerr(const State& state) const OVERRIDE_11;
    Real getAerr(const State& state) const OVERRIDE_11;

    Real calcEffectiveCOR(const State& state,
                          Real defaultCaptureSpeed,
                          Real defaultMinCORSpeed,
                          Real impactSpeed) const OVERRIDE_11 
    {
       return ConditionalConstraint::calcEffectiveCOR
               (m_minCOR, defaultCaptureSpeed, defaultMinCORSpeed,
                impactSpeed);
    }

    MultiplierIndex getContactMultiplierIndex(const State& s) const OVERRIDE_11;
private:
    MobilizedBody                   m_mobod;
    Real                            m_defaultLowerLimit;
    Real                            m_minCOR;
    Constraint::ConstantCoordinate  m_lower;
};

//==============================================================================
//                    POINT PLANE FRICTIONLESS CONTACT
//==============================================================================
/** (Experimental -- API will change -- use at your own risk) 
Define a point on one body that cannot penetrate a plane attached to another
body. The resulting contact is parameterized by a coefficient of restitution
for impacts in the plane normal direction. **/
class SimTK_SIMBODY_EXPORT PointPlaneFrictionlessContact 
:   public UnilateralContact {
public:
    PointPlaneFrictionlessContact(
        MobilizedBody& planeBodyB, const UnitVec3& normal_B, Real height,
        MobilizedBody& followerBodyF, const Vec3& point_F, Real minCOR);

    bool disable(State& state) const OVERRIDE_11 {
        if (m_ptInPlane.isDisabled(state)) return false;
        m_ptInPlane.disable(state);
        return true;
    }

    bool enable(State& state) const OVERRIDE_11 {
        if (!m_ptInPlane.isDisabled(state)) return false;
        m_ptInPlane.enable(state);
        return true;
    }

    bool isEnabled(const State& state) const OVERRIDE_11 {
        return !m_ptInPlane.isDisabled(state);
    }

    // Returns the contact point in the Ground frame.
    Vec3 whereToDisplay(const State& state) const OVERRIDE_11;

    // Currently have to fake the perr because the constraint might be
    // disabled in which case it won't calculate perr.
    Real getPerr(const State& state) const OVERRIDE_11;

    // We won't need to look at these except for proximal constraints which
    // will already have been enabled, so no need to fake.
    Real getVerr(const State& state) const OVERRIDE_11
    {   return m_ptInPlane.getVelocityError(state); }
    Real getAerr(const State& state) const OVERRIDE_11
    {   return m_ptInPlane.getAccelerationError(state); }


    Real calcEffectiveCOR(const State& state,
                          Real defaultCaptureSpeed,
                          Real defaultMinCORSpeed,
                          Real impactSpeed) const OVERRIDE_11 
    {
       return ConditionalConstraint::calcEffectiveCOR
               (m_minCOR, defaultCaptureSpeed, defaultMinCORSpeed,
                impactSpeed);
    }

    MultiplierIndex getContactMultiplierIndex(const State& s) const OVERRIDE_11;

private:
    MobilizedBody               m_planeBody;    // body P
    const Rotation              m_frame;        // z is normal; expressed in P
    const Real                  m_height;

    MobilizedBody               m_follower;     // body F
    const Vec3                  m_point;        // measured & expressed in F

    Real                        m_minCOR;

    Constraint::PointInPlane    m_ptInPlane;
};


//==============================================================================
//                          POINT PLANE CONTACT
//==============================================================================
/** (Experimental -- API will change -- use at your own risk) 
Define a point on one body that cannot penetrate a plane attached to another
body. The resulting contact is parameterized by a coefficient of restitution
for impacts in the plane normal direction, and by coefficients of friction
for frictional forces in the plane. **/
class SimTK_SIMBODY_EXPORT PointPlaneContact : public UnilateralContact {
public:
    PointPlaneContact(
        MobilizedBody& planeBodyB, const UnitVec3& normal_B, Real height,
        MobilizedBody& followerBodyF, const Vec3& point_F, 
        Real minCOR, Real mu_s, Real mu_d, Real mu_v);

    bool disable(State& state) const OVERRIDE_11 {
        if (m_ptInPlane.isDisabled(state)) return false;
        m_ptInPlane.disable(state);
        return true;
    }

    bool enable(State& state) const OVERRIDE_11 {
        if (!m_ptInPlane.isDisabled(state)) return false;
        m_ptInPlane.enable(state);
        return true;
    }

    bool isEnabled(const State& state) const OVERRIDE_11 {
        return !m_ptInPlane.isDisabled(state);
    }

    // Returns the contact point in the Ground frame.
    Vec3 whereToDisplay(const State& state) const OVERRIDE_11;

    // Currently have to fake the perr because the constraint might be
    // disabled in which case it won't calculate perr.
    Real getPerr(const State& state) const OVERRIDE_11;

    // We won't need to look at these except for proximal constraints which
    // will already have been enabled, so no need to fake.
    Real getVerr(const State& state) const OVERRIDE_11
    {   return m_ptInPlane.getVelocityErrors(state)[2]; }
    Real getAerr(const State& state) const OVERRIDE_11
    {   return m_ptInPlane.getAccelerationErrors(state)[2]; }


    Real calcEffectiveCOR(const State& state,
                          Real defaultCaptureSpeed,
                          Real defaultMinCORSpeed,
                          Real impactSpeed) const OVERRIDE_11 
    {
       return ConditionalConstraint::calcEffectiveCOR
               (m_minCOR, defaultCaptureSpeed, defaultMinCORSpeed,
                impactSpeed);
    }

    bool hasFriction(const State& state) const OVERRIDE_11
    {   return true; }

    Vec2 getSlipVelocity(const State& state) const  OVERRIDE_11 {
        const Vec3 v = m_ptInPlane.getVelocityErrors(state);
        return Vec2(v[0], v[1]);
    }

    Real calcEffectiveCOF(const State& state,
                          Real defaultTransitionSpeed,
                          Real slipSpeed) const OVERRIDE_11
    {
       return ConditionalConstraint::calcEffectiveCOF
               (m_mu_s, m_mu_d, m_mu_v, defaultTransitionSpeed, slipSpeed);
    }

    MultiplierIndex getContactMultiplierIndex(const State& s) const OVERRIDE_11;

    void getFrictionMultiplierIndices(const State&     s, 
                                      MultiplierIndex& ix_x, 
                                      MultiplierIndex& ix_y) const OVERRIDE_11;

private:
    MobilizedBody               m_planeBody;    // body P
    const Rotation              m_frame;        // z is normal; expressed in P
    const Real                  m_height;

    MobilizedBody               m_follower;     // body F
    const Vec3                  m_point;        // measured & expressed in F

    Real                        m_minCOR;
    Real                        m_mu_s, m_mu_d, m_mu_v;

    Constraint::PointInPlaneWithStiction    m_ptInPlane;
};

//==============================================================================
//                          SPHERE PLANE CONTACT
//==============================================================================
/** (Experimental -- API will change -- use at your own risk) 
Define a sphere on one body that cannot penetrate a plane attached to another
body. The resulting contact is parameterized by a coefficient of restitution
for impacts in the plane normal direction, and by coefficients of friction
for frictional forces in the plane. **/
class SimTK_SIMBODY_EXPORT SpherePlaneContact : public UnilateralContact {
public:
    SpherePlaneContact(
        MobilizedBody& planeBodyB, const UnitVec3& normal_B, Real height,
        MobilizedBody& followerBodyF, const Vec3& point_F, Real radius,
        Real minCOR, Real mu_s, Real mu_d, Real mu_v);

    bool disable(State& state) const OVERRIDE_11 {
        if (m_sphereOnPlane.isDisabled(state)) return false;
        m_sphereOnPlane.disable(state);
        return true;
    }

    bool enable(State& state) const OVERRIDE_11 {
        if (!m_sphereOnPlane.isDisabled(state)) return false;
        m_sphereOnPlane.enable(state);
        return true;
    }

    bool isEnabled(const State& state) const OVERRIDE_11 {
        return !m_sphereOnPlane.isDisabled(state);
    }

    // Returns the contact point in the Ground frame.
    Vec3 whereToDisplay(const State& state) const OVERRIDE_11;

    // Currently have to fake the perr because the constraint might be
    // disabled in which case it won't calculate perr.
    Real getPerr(const State& state) const OVERRIDE_11;

    // We won't need to look at these except for proximal constraints which
    // will already have been enabled, so no need to fake.
    Real getVerr(const State& state) const OVERRIDE_11
    {   return m_sphereOnPlane.getVelocityErrors(state)[2]; }
    Real getAerr(const State& state) const OVERRIDE_11
    {   return m_sphereOnPlane.getAccelerationErrors(state)[2]; }


    Real calcEffectiveCOR(const State& state,
                          Real defaultCaptureSpeed,
                          Real defaultMinCORSpeed,
                          Real impactSpeed) const OVERRIDE_11 
    {
       return ConditionalConstraint::calcEffectiveCOR
               (m_minCOR, defaultCaptureSpeed, defaultMinCORSpeed,
                impactSpeed);
    }

    bool hasFriction(const State& state) const OVERRIDE_11
    {   return true; }

    Vec2 getSlipVelocity(const State& state) const  OVERRIDE_11 {
        const Vec3 v = m_sphereOnPlane.getVelocityErrors(state);
        return Vec2(v[0], v[1]);
    }

    Real calcEffectiveCOF(const State& state,
                          Real defaultTransitionSpeed,
                          Real slipSpeed) const OVERRIDE_11
    {
       return ConditionalConstraint::calcEffectiveCOF
               (m_mu_s, m_mu_d, m_mu_v, defaultTransitionSpeed, slipSpeed);
    }

    MultiplierIndex getContactMultiplierIndex(const State& s) const OVERRIDE_11;

    void getFrictionMultiplierIndices(const State&     s, 
                                      MultiplierIndex& ix_x, 
                                      MultiplierIndex& ix_y) const OVERRIDE_11;

private:
    MobilizedBody               m_planeBody;    // body P
    const Rotation              m_frame;        // z is normal; expressed in P
    const Real                  m_height;

    MobilizedBody               m_follower;     // body F
    const Vec3                  m_point;        // measured & expressed in F
    const Real                  m_radius;

    Real                        m_minCOR;
    Real                        m_mu_s, m_mu_d, m_mu_v;

    Constraint::SphereOnPlaneContact    m_sphereOnPlane;
};


} // namespace SimTK

#endif // SimTK_SIMBODY_CONDITIONAL_CONSTRAINT_H_
