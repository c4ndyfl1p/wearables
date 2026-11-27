# drum_n_didge.rb — ID 3 circular-motion didgeridoo plus ID 4
# hand-triggered hangpan. Each sensor owns its own sound mapping.
#
# Requires 4_hand_bridge_calibration.py, which sends
# /didgeridoo_motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2> for ID 3
# /handpan_motion_xz <sensor_id> <x_accel_m_s2> <z_accel_m_s2> for ID 4.

# Didgeridoo mapping copied from synth_and_didgeridoo.rb.
DIDGE_ID = 3
MIN_AXIS_ACCEL = 0.8
MIN_ANGLE_STEP = 0.025
MAX_ANGLE_STEP = 1.2
DISCOVERY_ANGLE = 0.35
MIN_CIRCULAR_SPEED = 0.6
MAX_CIRCULAR_SPEED = 8.0
SAMPLE_PERIOD = 0.02
DIDGE_CUTOFFS = (ring 50, 62, 75, 85)

# ID 4 handpan reversal mapping.
DOWN_ACCEL_THRESHOLD = 1.5
UP_ACCEL_THRESHOLD = 1.5
ID4_DOWN_ACCEL_THRESHOLD = 2.0
ID4_UP_ACCEL_THRESHOLD = 2.0
ID4_REFRACTORY_SECONDS = 0.35
HANGPAN_LINE = (ring :d2, :a2, :d3, :a2, :fs2, :a2, :d3, :a2)

set :dnd_didge_previous_angle, nil
set :dnd_didge_direction, 0
set :dnd_didge_turn_travel, 0.0
set :dnd_didge_circular_level, 0.0
set :dnd_handpan4_state, :ready
set :dnd_handpan4_last_trigger, 0.0

# ID 3: same low, throaty didgeridoo voice, circular-motion detection,
# smoothing, octave effect, and vowel filter as synth_and_didgeridoo.rb.
with_fx :reverb, mix: 0.68, room: 0.92 do
  with_fx :octaver, mix: 0.65, sub: 1, sup: 0 do
    with_fx :rlpf, cutoff: 55, res: 0.88 do |throat_filter|
      throat = synth :dsaw, note: :d1, sustain: 100000, release: 1,
        amp: 0, amp_slide: 0.08, detune: 0.15, pan: -0.06
      body = synth :dsaw, note: :d2, sustain: 100000, release: 1,
        amp: 0, amp_slide: 0.08, detune: 0.15, pan: 0.06
      breath = synth :pnoise, sustain: 100000, release: 1,
        amp: 0, amp_slide: 0.09, cutoff: 48, cutoff_slide: 0.18

      live_loop :drum_n_didge_didge_motion do
        use_real_time
        id, ax, az = sync "/osc*/didgeridoo_motion_xz"
        next unless id == DIDGE_ID

        magnitude = Math.sqrt(ax * ax + az * az)
        previous_angle = get[:dnd_didge_previous_angle]
        direction = get[:dnd_didge_direction] || 0
        travel = get[:dnd_didge_turn_travel] || 0.0
        level = get[:dnd_didge_circular_level] || 0.0
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
          set :dnd_didge_previous_angle, angle
        else
          set :dnd_didge_previous_angle, nil
          direction = 0
          travel = 0.0
        end

        # Preserve the standalone didgeridoo's slow fade-in and fade-out.
        smoothing = target > level ? 0.18 : 0.14
        level += smoothing * (target - level)
        set :dnd_didge_direction, direction
        set :dnd_didge_turn_travel, travel
        set :dnd_didge_circular_level, level

        control throat, amp: level * 1.05
        control body, amp: level * 0.48
        control breath, amp: level * 0.14
      end

      live_loop :drum_n_didge_didge_vowels do
        level = get[:dnd_didge_circular_level] || 0.0
        target_cutoff = [DIDGE_CUTOFFS.tick + level * 12, 115].min
        control throat_filter, cutoff: target_cutoff, cutoff_slide: 0.25
        sleep 0.5
      end
    end
  end
end

# ID 4: retain its current downstroke/reversal handpan behavior and sound.
with_fx :reverb, mix: 0.72, room: 0.85 do
  live_loop :drum_n_didge_handpan4 do
    use_real_time
    id, _ax, z_accel = sync "/osc*/handpan_motion_xz"
    next unless id == 4
    state = get[:dnd_handpan4_state] || :ready
    last_trigger = get[:dnd_handpan4_last_trigger] || 0.0
    now = Time.now.to_f
    note = nil

    if state == :ready
      state = :downstroke_seen if z_accel <= -ID4_DOWN_ACCEL_THRESHOLD
    elsif state == :downstroke_seen
      if z_accel >= ID4_UP_ACCEL_THRESHOLD
        note = HANGPAN_LINE.tick(:manual_hangpan_id4)
        state = :refractory
        last_trigger = now
      end
    elsif state == :refractory && now - last_trigger >= ID4_REFRACTORY_SECONDS
      state = :ready
    end

    set :dnd_handpan4_state, state
    set :dnd_handpan4_last_trigger, last_trigger

    if note
      synth :pretty_bell,
        note: note,
        amp: 0.42,
        attack: 0.003,
        release: 1.8,
        cutoff: 110,
        pan: rrand(-0.08, 0.08)

      synth :fm,
        note: note,
        divisor: 1.4,
        depth: 1.8,
        amp: 0.16,
        attack: 0.002,
        release: 1.3,
        pan: rrand(-0.08, 0.08)

      synth :sine,
        note: note,
        amp: 0.08,
        attack: 0.005,
        release: 2.0

      puts "Sensor 4 Z reversal: playing #{note}"
    end
  end
end   
