extends "res://addons/gut/test.gd"

class TestGlobal:
	extends "res://addons/gut/test.gd"
	
	var sprite: Sprite2D = Sprite2D.new()
	
	var banks := Array()
	
	func before_all():
		# load banks
		# warning-ignore:return_value_discarded
		banks.append(
			FmodServer.load_bank("res://assets/Banks/Master.strings.bank", FmodServer.FMOD_STUDIO_LOAD_BANK_NORMAL)
		)
		# warning-ignore:return_value_discarded
		banks.append(
			FmodServer.load_bank("res://assets/Banks/Master.bank", FmodServer.FMOD_STUDIO_LOAD_BANK_NORMAL)
		)
		# warning-ignore:return_value_discarded
		banks.append(
			FmodServer.load_bank("res://assets/Banks/Music.bank", FmodServer.FMOD_STUDIO_LOAD_BANK_NORMAL)
		)
		# warning-ignore:return_value_discarded
		banks.append(
			FmodServer.load_bank("res://assets/Banks/Vehicles.bank", FmodServer.FMOD_STUDIO_LOAD_BANK_NORMAL)
		)
		FmodServer.set_listener_number(1)
		get_tree().get_root().add_child(sprite)
		FmodServer.add_listener(0, sprite)
	
	func after_all():
		FmodServer.remove_listener(0, sprite)
	
	func test_assert_should_have_performance_data():
		var perf_data: FmodPerformanceData = FmodServer.get_performance_data()
		assert_not_null(perf_data, "Performance data should not be null.")
		
	# The demo leaves the DSP settings unset, so FMOD runs with the extension defaults: 512 samples, except on the
	# web with FMOD's own output where it is 2048 (see docs/advanced/2-web.md).
	func _default_dsp_buffer_size() -> int:
		if OS.has_feature("web") and not ProjectSettings.get_setting("Fmod/Web/queued_output", true):
			return 2048
		return 512

	func test_assert_should_have_dsp_buffer_length():
		var buffer_length = FmodServer.get_system_dsp_buffer_length()
		assert_eq(buffer_length, _default_dsp_buffer_size())

	func test_assert_should_have_dsp_num_buffers():
		var num_buffers = FmodServer.get_system_dsp_num_buffers()
		assert_eq(num_buffers, 4)

	func test_assert_should_have_dsp_buffer_size():
		var buffer_size: FmodDspSettings = FmodServer.get_system_dsp_buffer_settings()
		assert_eq(buffer_size.dsp_buffer_size, _default_dsp_buffer_size())
		assert_eq(buffer_size.dsp_buffer_count, 4)

	# FMOD only accepts a DSP buffer size before initialization, so the settings resource is checked on its own
	# and the applied value through the length/settings getters above.
	func test_assert_dsp_settings_resource_round_trip():
		var settings := FmodDspSettings.new()
		settings.dsp_buffer_size = 2048
		settings.dsp_buffer_count = 2
		assert_eq(settings.dsp_buffer_size, 2048)
		assert_eq(settings.dsp_buffer_count, 2)
		assert_eq(settings.get_dsp_buffer_size(), 2048)
		assert_eq(settings.get_dsp_buffer_count(), 2)

	func test_assert_system_dsp_settings_match_length_and_count():
		var settings: FmodDspSettings = FmodServer.get_system_dsp_buffer_settings()
		assert_eq(settings.dsp_buffer_size, FmodServer.get_system_dsp_buffer_length())
		assert_eq(settings.dsp_buffer_count, FmodServer.get_system_dsp_num_buffers())
