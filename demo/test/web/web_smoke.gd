extends Node

# Smoke test for web exports, run in a browser by CI (tools/web/smoke_test.mjs).
# The event timeline only advances if FMOD's mixer is running, which proves FMOD works in the browser.
# The runner prints FMOD_WEB_SMOKE/FMOD_WEB_OK/FMOD_WEB_FAIL lines, the browser driver reads them from the console.

const TIMEOUT_SEC: float = 15.0
# Keep playing after the timeline first moved, to check that the mixer keeps running.
const PLAY_SEC: float = 3.0
# On the web FMOD mixes on the main thread, smaller buffers replay stale audio (crackling).
# Must match FmodDspSettings::DEFAULT_DSP_BUFFER_SIZE_WEB.
const MIN_WEB_DSP_BUFFER_SIZE: int = 2048

# Banks are unloaded when their FmodBank is freed, keep references for the whole test.
var banks: Array[FmodBank] = []
var event: FmodEvent = null
var elapsed: float = 0.0
var done: bool = false
var first_position: int = 0
var first_position_at: float = -1.0

func _ready():
	print("FMOD_WEB_SMOKE: started")
	var dsp: FmodDspSettings = FmodServer.get_system_dsp_buffer_settings()
	print("FMOD_WEB_SMOKE: dsp buffer %d samples x %d" % [dsp.dsp_buffer_size, dsp.dsp_buffer_count])
	if OS.has_feature("web") and dsp.dsp_buffer_size < MIN_WEB_DSP_BUFFER_SIZE:
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
	if first_position_at < 0.0:
		if position > 0:
			first_position = position
			first_position_at = elapsed
			print("FMOD_WEB_SMOKE: timeline at %d ms after %.1f s" % [position, elapsed])
		elif elapsed > TIMEOUT_SEC:
			_finish(false, "timeline did not advance after %.1f s" % elapsed)
		return
	var played: float = elapsed - first_position_at
	if played < PLAY_SEC:
		return
	# The timeline is in milliseconds, allow for a slow browser but not for a stalled mixer.
	var advanced: int = position - first_position
	if advanced >= int(played * 1000.0 * 0.5):
		_finish(true, "timeline at %d ms, advanced %d ms in %.1f s" % [position, advanced, played])
	else:
		_finish(false, "timeline stalled, advanced only %d ms in %.1f s" % [advanced, played])

func _finish(success: bool, message: String):
	done = true
	print(("FMOD_WEB_OK: " if success else "FMOD_WEB_FAIL: ") + message)
