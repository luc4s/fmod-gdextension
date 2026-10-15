extends Node

# Smoke test for web exports, run in a browser by CI (tools/web/smoke_test.mjs).
# The event timeline only advances if FMOD's mixer is running, which proves FMOD works in the browser.
# The runner prints FMOD_WEB_SMOKE/FMOD_WEB_OK/FMOD_WEB_FAIL lines, the browser driver reads them from the console.
#
# Phases: wait for the timeline to move, play for PLAY_SEC, then play for STRESS_SEC with every frame blocked for
# STRESS_FRAME_MS. The browser driver counts the audio the worklet could not play during the whole run, so the
# stress phase fails the test when the output cannot absorb slow frames.

const TIMEOUT_SEC: float = 15.0
const PLAY_SEC: float = 3.0
const STRESS_SEC: float = 3.0
const STRESS_FRAME_MS: int = 60
# Without the queued output, FMOD mixes one block at a time on the main thread and smaller blocks replay stale
# audio (crackling). Must match FmodDspSettings::DEFAULT_DSP_BUFFER_SIZE_WEB.
const MIN_WEB_DSP_BUFFER_SIZE: int = 2048

# Banks are unloaded when their FmodBank is freed, keep references for the whole test.
var banks: Array[FmodBank] = []
var event: FmodEvent = null
var elapsed: float = 0.0
var done: bool = false
var phase: String = "wait"
var phase_start: float = 0.0
var phase_position: int = 0
var play_advanced: int = 0

func _ready():
	print("FMOD_WEB_SMOKE: started")
	var dsp: FmodDspSettings = FmodServer.get_system_dsp_buffer_settings()
	var queued_output: bool = ProjectSettings.get_setting("Fmod/Web/queued_output", true)
	print("FMOD_WEB_SMOKE: dsp buffer %d samples x %d, queued output %s" % [dsp.dsp_buffer_size, dsp.dsp_buffer_count, queued_output])
	if OS.has_feature("web") and not queued_output and dsp.dsp_buffer_size < MIN_WEB_DSP_BUFFER_SIZE:
		_finish(false, "dsp buffer size %d is below the web default %d" % [dsp.dsp_buffer_size, MIN_WEB_DSP_BUFFER_SIZE])
		return
	for bank_name in ["Master.strings", "Master", "Music"]:
		var bank: FmodBank = FmodServer.load_bank("res://assets/Banks/%s.bank" % bank_name, FmodServer.FMOD_STUDIO_LOAD_BANK_NORMAL)
		if bank == null or not bank.is_valid():
			_finish(false, "cannot load bank %s" % bank_name)
			return
		banks.append(bank)
		print("FMOD_WEB_SMOKE: loaded %s (%d events)" % [bank.get_path(), bank.get_event_description_count()])
	event = FmodServer.create_event_instance("event:/Music/Level 02")
	if event == null:
		_finish(false, "cannot create event")
		return
	event.start()

func _process(delta):
	if done or event == null:
		return
	elapsed += delta
	var position: int = event.get_timeline_position()
	match phase:
		"wait":
			if position > 0:
				print("FMOD_WEB_SMOKE: timeline at %d ms after %.1f s" % [position, elapsed])
				_start_phase("play", position)
			elif elapsed > TIMEOUT_SEC:
				_finish(false, "timeline did not advance after %.1f s" % elapsed)
		"play":
			var played: float = elapsed - phase_start
			if played >= PLAY_SEC:
				play_advanced = position - phase_position
				if not _advanced_enough(play_advanced, played):
					_finish(false, "timeline stalled, advanced only %d ms in %.1f s" % [play_advanced, played])
					return
				print("FMOD_WEB_SMOKE: advanced %d ms in %.1f s, stressing with %d ms frames" % [play_advanced, played, STRESS_FRAME_MS])
				_start_phase("stress", position)
		"stress":
			# Simulates a slow game loop: every frame blocks the main thread, which also runs FMOD's mixer.
			OS.delay_msec(STRESS_FRAME_MS)
			var stressed: float = elapsed - phase_start
			if stressed >= STRESS_SEC:
				var advanced: int = position - phase_position
				if _advanced_enough(advanced, stressed):
					_finish(true, "timeline at %d ms, advanced %d ms in %.1f s, then %d ms in %.1f s of %d ms frames" % [position, play_advanced, PLAY_SEC, advanced, stressed, STRESS_FRAME_MS])
				else:
					_finish(false, "timeline stalled under stress, advanced only %d ms in %.1f s" % [advanced, stressed])

func _start_phase(name: String, position: int):
	phase = name
	phase_start = elapsed
	phase_position = position

# The timeline is in milliseconds, allow for a slow browser but not for a stalled mixer.
func _advanced_enough(advanced: int, seconds: float) -> bool:
	return advanced >= int(seconds * 1000.0 * 0.5)

func _finish(success: bool, message: String):
	done = true
	print(("FMOD_WEB_OK: " if success else "FMOD_WEB_FAIL: ") + message)
