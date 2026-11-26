# synth_and_didgeridoo.rb — a low, throaty didgeridoo drone for sensor 3.
# Circular X/Z acceleration brings the sound in; faster circles make it louder
# and brighter. It fades smoothly when circular motion stops.
#
# Start 4_hand_bridge_calibration.py first. It sends
# /accel_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2> at about 50 Hz.

DIDGE_ID = 3
MIN_AXIS_ACCEL = 0.8       # m/s^2; ignore still-sensor noise
MIN_ANGLE_STEP = 0.025     # radians; ignore tiny vector jitter
MAX_ANGLE_STEP = 1.2       # reject discontinuities when the vector is tiny
DISCOVERY_ANGLE = 0.35     # require sustained rotation before fading in
MIN_CIRCULAR_SPEED = 0.6   # radians/s
MAX_CIRCULAR_SPEED = 8.0   # maps to full dynamic
SAMPLE_PERIOD = 0.02       # bridge sends at about 50 Hz

set :didge_previous_angle, nil
set :didge_direction, 0
set :didge_turn_travel, 0.0
set :didge_circular_level, 0.0

with_fx :reverb, mix: 0.68, room: 0.92 do
  with_fx :octaver, mix: 0.65, sub: 1, sup: 0 do
    # Low D drone with resonant pulse waves for the wooden, growly timbre.
    throat = synth :tb303, note: :d0, sustain: 100000, release: 1,
      amp: 0, amp_slide: 0.08, cutoff: 42, cutoff_slide: 0.16,
      res: 0.975, wave: 1, pulse_width: 0.38, pan: -0.08
    body = synth :tb303, note: :d1, sustain: 100000, release: 1,
      amp: 0, amp_slide: 0.08, cutoff: 52, cutoff_slide: 0.16,
      res: 0.93, wave: 1, pulse_width: 0.42, pan: 0.08
    breath = synth :pnoise, sustain: 100000, release: 1,
      amp: 0, amp_slide: 0.09, cutoff: 48, cutoff_slide: 0.18

    live_loop :didgeridoo_motion do
      use_real_time
      id, ax, az = sync "/osc*/accel_xz"

      if id == DIDGE_ID
        magnitude = Math.sqrt(ax * ax + az * az)
        previous_angle = get[:didge_previous_angle]
        direction = get[:didge_direction] || 0
        travel = get[:didge_turn_travel] || 0.0
        level = get[:didge_circular_level] || 0.0
        target = 0.0

        if magnitude >= MIN_AXIS_ACCEL
          angle = Math.atan2(az, ax)

          if previous_angle
            delta = angle - previous_angle
            delta -= 2 * Math::PI if delta > Math::PI
            delta += 2 * Math::PI if delta < -Math::PI

            if delta.abs <= MAX_ANGLE_STEP
              if delta.abs >= MIN_ANGLE_STEP
                step_direction = delta > 0 ? 1 : -1
                direction = step_direction if direction == 0
                travel += delta * direction
                if travel < 0
                  direction = -direction
                  travel = -travel
                end
              else
                direction = 0
                travel = 0.0
              end

              angular_speed = delta.abs / SAMPLE_PERIOD
              if travel >= DISCOVERY_ANGLE && angular_speed >= MIN_CIRCULAR_SPEED
                speed_level = [[angular_speed / MAX_CIRCULAR_SPEED, 0.0].max, 1.0].min
                axis_level = [[magnitude / 5.0, 0.0].max, 1.0].min
                target = speed_level * (0.35 + 0.65 * axis_level)
              end
            else
              direction = 0
              travel = 0.0
            end
          end

          set :didge_previous_angle, angle
        else
          set :didge_previous_angle, nil
          direction = 0
          travel = 0.0
        end

        # Smooth attack and release keep the drone continuous rather than per-hit.
        smoothing = target > level ? 0.18 : 0.14
        level += smoothing * (target - level)
        set :didge_direction, direction
        set :didge_turn_travel, travel
        set :didge_circular_level, level

        control throat, amp: level * 0.9, cutoff: 44 + level * 32
        control body, amp: level * 0.42, cutoff: 52 + level * 30
        control breath, amp: level * 0.15, cutoff: 52 + level * 22
      end
    end
  end
end
