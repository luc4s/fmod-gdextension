extends Node

# Smoke test for web exports, run in a headless browser by CI (tools/web/smoke_test.mjs).
# The event timeline only advances if FMOD's mixer is running, which proves FMOD works in the browser.

const TIMEOUT_SEC: float = 15.0

var event: FmodEvent = null
var elapsed: float = 0.0
var done: bool = false

func _ready():
	print("FMOD_WEB_SMOKE: started")
	for bank_name in ["Master.strings", "Master", "Music"]:
		var bank: FmodBank = FmodServer.load_bank("res://assets/Banks/%s.bank" % bank_name, FmodServer.FMOD_STUDIO_LOAD_BANK_NORMAL)
		if bank == null or not bank.is_valid():
			_finish(false, "cannot load bank %s" % bank_name)
			return
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
	if position > 0:
		_finish(true, "timeline at %d ms after %.1f s" % [position, elapsed])
	elif elapsed > TIMEOUT_SEC:
		_finish(false, "timeline did not advance after %.1f s" % elapsed)

func _finish(success: bool, message: String):
	done = true
	print(("FMOD_WEB_OK: " if success else "FMOD_WEB_FAIL: ") + message)
