# all_gloves.rb — all four gloves in one Sonic Pi file.
#   ID 1 and 2: synth gloves (chords and melodies on flicks)
#   ID 3:       didgeridoo (circular motion)
#   ID 4:       handpan (down-up stroke)
#
# Needs four_glove_bridge.py running.
# Press Stop before Run, so old sounds don't keep playing underneath.
#
# Performance notes (to avoid "thread got too far behind"):
#   - The fast motion loops keep their state in the loop itself (init: / return
#     value) instead of set/get, which is much cheaper at 50 messages per second.
#   - auto_cue: false stops each loop from sending a cue on every pass.
#   - Synths are only updated when the level actually changes.


# =====================================================================
# GLOVES 1 AND 2: SYNTH
# =====================================================================

# The chord changes every 8 seconds: D, G, B minor, A
chords = (ring chord(:D4, :major), chord(:G4, :major),
          chord(:B3, :minor), chord(:A3, :major))

live_loop :progression, auto_cue: false do
  set :chord, chords.tick
  sleep 8
end

with_fx :reverb, mix: 0.7 do
  live_loop :synth_flicks, auto_cue: false do
    use_real_time
    use_synth :hollow
    id, strength = sync "/osc*/flick"
    next unless id == 1 || id == 2        # gloves 3 and 4 have their own sounds below

    notes = get[:chord] || chord(:D4, :major)

    # Remember when each hand last flicked
    set :t1, vt if id == 1
    set :t2, vt if id == 2
    other = (id == 1) ? (get[:t2] || -10) : (get[:t1] || -10)

    if vt - other < 0.3
      # Both hands flick together: the whole chord plus a bell
      play notes, attack: 0.1, release: 5, amp: 1.2
      synth :pretty_bell, note: notes[0] + 12, release: 3, amp: 0.5
    elsif id == 1
      # Each hand walks up through the chord: melodies appear
      play notes.tick(:hand1), attack: 0.2, release: 3, amp: strength * 2
    else
      play notes.tick(:hand2) + 12, attack: 0.2, release: 3, amp: strength * 2
    end
  end
end


# =====================================================================
# GLOVE 3: DIDGERIDOO
# =====================================================================

DIDGE_ID = 3
MIN_AXIS_ACCEL = 0.8
MIN_ANGLE_STEP = 0.025
MAX_ANGLE_STEP = 1.2
DISCOVERY_ANGLE = 0.35
MIN_CIRCULAR_SPEED = 0.6
MAX_CIRCULAR_SPEED = 8.0
SAMPLE_PERIOD = 0.02          # must match AZ_SEND_INTERVAL in the bridge
DIDGE_CUTOFFS = (ring 50, 62, 75, 85)

set :didge_level, 0.0

with_fx :reverb, mix: 0.68, room: 0.92 do
  with_fx :octaver, mix: 0.65, sub: 1, sup: 0 do
    with_fx :rlpf, cutoff: 55, res: 0.88 do |throat_filter|
      throat = synth :dsaw, note: :d1, sustain: 100000, release: 1,
        amp: 0, amp_slide: 0.08, detune: 0.15, pan: -0.06
      body = synth :dsaw, note: :d2, sustain: 100000, release: 1,
        amp: 0, amp_slide: 0.08, detune: 0.15, pan: 0.06
      breath = synth :pnoise, sustain: 100000, release: 1,
        amp: 0, amp_slide: 0.09, cutoff: 48, cutoff_slide: 0.18

      # The loop's state travels from one pass to the next: whatever the block
      # returns at the end becomes "st" on the next pass.
      live_loop :didge_motion, auto_cue: false,
        init: { angle: nil, direction: 0, travel: 0.0, level: 0.0, sent: -1.0, shared_at: 0.0 } do |st|
        use_real_time
        id, ax, az = sync "/osc*/didgeridoo_motion_xz"

        angle = st[:angle]
        direction = st[:direction]
        travel = st[:travel]
        level = st[:level]
        sent = st[:sent]
        shared_at = st[:shared_at]

        if id == DIDGE_ID
          magnitude = Math.sqrt(ax * ax + az * az)
          target = 0.0

          if magnitude >= MIN_AXIS_ACCEL
            new_angle = Math.atan2(az, ax)
            if angle
              delta = new_angle - angle
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
            angle = new_angle
          else
            angle = nil
            direction = 0
            travel = 0.0
          end

          smoothing = target > level ? 0.18 : 0.14
          level += smoothing * (target - level)

          # Only update the synths when the level has really changed
          if (level - sent).abs > 0.01
            control throat, amp: level * 1.05
            control body, amp: level * 0.48
            control breath, amp: level * 0.14
            sent = level
          end

          # Share the level with the vowel loop about 10 times per second
          now = Time.now.to_f
          if now - shared_at > 0.1
            set :didge_level, level
            shared_at = now
          end
        end

        { angle: angle, direction: direction, travel: travel, level: level, sent: sent, shared_at: shared_at }
      end

      live_loop :didge_vowels, auto_cue: false do
        level = get[:didge_level] || 0.0
        target_cutoff = [DIDGE_CUTOFFS.tick + level * 12, 115].min
        control throat_filter, cutoff: target_cutoff, cutoff_slide: 0.25
        sleep 0.5
      end
    end
  end
end


# =====================================================================
# GLOVE 4: HANDPAN
# =====================================================================

ID4_DOWN_ACCEL_THRESHOLD = 2.0
ID4_UP_ACCEL_THRESHOLD = 2.0
ID4_REFRACTORY_SECONDS = 0.35
HANGPAN_LINE = (ring :d2, :a2, :d3, :a2, :fs2, :a2, :d3, :a2)

with_fx :reverb, mix: 0.72, room: 0.85 do
  live_loop :handpan4, auto_cue: false, init: { state: :ready, last: 0.0 } do |st|
    use_real_time
    id, _ax, z_accel = sync "/osc*/handpan_motion_xz"
    state = st[:state]
    last_trigger = st[:last]

    if id == 4
      now = Time.now.to_f
      note = nil

      if state == :ready
        state = :downstroke_seen if z_accel <= -ID4_DOWN_ACCEL_THRESHOLD
      elsif state == :downstroke_seen
        if z_accel >= ID4_UP_ACCEL_THRESHOLD
          note = HANGPAN_LINE.tick(:hangpan4)
          state = :refractory
          last_trigger = now
        end
      elsif state == :refractory && now - last_trigger >= ID4_REFRACTORY_SECONDS
        state = :ready
      end

      if note
        synth :pretty_bell, note: note, amp: 0.42, attack: 0.003, release: 1.8,
          cutoff: 110, pan: rrand(-0.08, 0.08)
        synth :fm, note: note, divisor: 1.4, depth: 1.8, amp: 0.16,
          attack: 0.002, release: 1.3, pan: rrand(-0.08, 0.08)
        synth :sine, note: note, amp: 0.08, attack: 0.005, release: 2.0
      end
    end

    { state: state, last: last_trigger }
  end
end
