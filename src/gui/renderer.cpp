
#include "renderer.h"

#include "base/launcher.h"
#include "clip/clip.h"

#include "common/rendering.h"
#include "common/rendering_frame.h"

#include "drag_handler.h"
#include "tasks.h"
#include "gui.h"

#include "ui/ui.h"
#include "ui/utils.h"
#include "ui/render.h"

#include "resources/fonts/eb_garamond.h"
#include "resources/fonts/dejavu_sans.h"

#define DEBUG_RENDER 0

const int PAD_X = 24;
const int PAD_Y = PAD_X;

const int NOTIFICATIONS_PAD_X = 10;
const int NOTIFICATIONS_PAD_Y = 10;

const float FPS_SMOOTHING = 0.95f;

void gui::renderer::init_fonts() {
	fonts::font = utils::create_font_from_data(DejaVuSans_ttf.data(), DejaVuSans_ttf.size(), 11);

	fonts::header_font = utils::create_font_from_data(
		EBGaramond_VariableFont_wght_ttf.data(), EBGaramond_VariableFont_wght_ttf.size(), 30
	);
	fonts::smaller_header_font = fonts::header_font;
	fonts::smaller_header_font.setSize(18.f);
}

void gui::renderer::set_cursor(os::NativeCursor cursor) {
	if (current_cursor != cursor) {
		current_cursor = cursor;
		window->setCursor(cursor);
	}

	set_cursor_this_frame = true;
}

void gui::renderer::components::render(
	ui::Container& container,
	Render& render,
	bool current,
	float delta_time,
	bool& is_progress_shown,
	float& bar_percent
) {
	// todo: ui concept
	// screen start|      [faded]last_video current_video [faded]next_video next_video2 next_video3 (+5) |
	// screen end animate sliding in as it moves along the queue
	ui::add_text(
		std::format("video {} name text", render.get_render_id()),
		container,
		base::to_utf8(render.get_video_name()),
		gfx::rgba(255, 255, 255, (current ? 255 : 100)),
		fonts::smaller_header_font,
		os::TextAlign::Center
	);

	if (!current)
		return;

	auto render_status = render.get_status();
	int bar_width = 300;

	std::string preview_path = render.get_preview_path().string();
	if (!preview_path.empty() && render_status.current_frame > 0) {
		auto element = ui::add_image(
			"preview image",
			container,
			preview_path,
			gfx::Size(container.get_usable_rect().w, container.get_usable_rect().h / 2),
			std::to_string(render_status.current_frame)
		);
		if (element) {
			bar_width = (*element)->rect.w;
		}
	}

	if (render_status.init) {
		float render_progress = (float)render_status.current_frame / (float)render_status.total_frames;
		bar_percent = u::lerp(bar_percent, render_progress, 5.f * delta_time, 0.005f);

		ui::add_bar(
			"progress bar",
			container,
			bar_percent,
			gfx::rgba(51, 51, 51, 255),
			gfx::rgba(255, 255, 255, 255),
			bar_width,
			std::format("{:.1f}%", render_progress * 100),
			gfx::rgba(255, 255, 255, 255),
			&fonts::font
		);

		container.push_element_gap(6);
		ui::add_text(
			"progress text",
			container,
			std::format("frame {}/{}", render_status.current_frame, render_status.total_frames),
			gfx::rgba(255, 255, 255, 155),
			fonts::font,
			os::TextAlign::Center
		);
		container.pop_element_gap();

		ui::add_text(
			"progress text 2",
			container,
			std::format("{:.2f} frames per second", render_status.fps),
			gfx::rgba(255, 255, 255, 155),
			fonts::font,
			os::TextAlign::Center
		);

		is_progress_shown = true;
	}
	else {
		ui::add_text(
			"initialising render text",
			container,
			"Initialising render...",
			gfx::rgba(255, 255, 255, 255),
			fonts::font,
			os::TextAlign::Center
		);
	}
}

void gui::renderer::components::main_screen(ui::Container& container, float delta_time) {
	static float bar_percent = 0.f;

	if (rendering.get_queue().empty() && !current_render_copy) {
		bar_percent = 0.f;

		gfx::Point title_pos = container.get_usable_rect().center();
		title_pos.y = int(PAD_Y + fonts::header_font.getSize());

		ui::add_text_fixed(
			"blur title text",
			container,
			title_pos,
			"blur",
			gfx::rgba(255, 255, 255, 255),
			fonts::header_font,
			os::TextAlign::Center
		);

		if (initialisation_res && !initialisation_res->success) {
			ui::add_text(
				"failed to initialise text",
				main_container,
				"Failed to initialise",
				gfx::rgba(255, 255, 255),
				fonts::font,
				os::TextAlign::Center
			);

			ui::add_text(
				"failed to initialise reason",
				main_container,
				initialisation_res->error_message,
				gfx::rgba(255, 255, 255, 155),
				fonts::font,
				os::TextAlign::Center
			);

			return;
		}

		ui::add_button("open file button", container, "Open files", fonts::font, [] {
			base::paths paths;
			utils::show_file_selector("Blur input", "", {}, os::FileDialog::Type::OpenFiles, paths);

			std::vector<std::wstring> wpaths;
			for (const auto path : paths) {
				wpaths.push_back(base::from_utf8(path));
			}

			tasks::add_files(wpaths);
		});

		ui::add_text(
			"drop file text",
			container,
			"or drop them anywhere",
			gfx::rgba(255, 255, 255, 255),
			fonts::font,
			os::TextAlign::Center
		);
	}
	else {
		bool is_progress_shown = false;

		rendering.lock();
		{
			auto current_render = rendering.get_current_render();

			// displays final state of the current render once where it would have been skipped otherwise
			auto render_current_edge_case = [&] {
				if (!current_render_copy)
					return;

				if (current_render) {
					if ((*current_render)->get_render_id() == (*current_render_copy).get_render_id()) {
						u::log("render final frame: wasnt deleted so just render normally");
						return;
					}
				}

				u::log("render final frame: it was deleted bro, rendering separately");

				components::render(container, *current_render_copy, true, delta_time, is_progress_shown, bar_percent);

				current_render_copy.reset();
			};

			render_current_edge_case();

			for (const auto [i, render] : u::enumerate(rendering.get_queue())) {
				bool current = current_render && render.get() == current_render.value();

				components::render(container, *render, current, delta_time, is_progress_shown, bar_percent);
			}
		}
		rendering.unlock();

		if (!is_progress_shown) {
			bar_percent = 0.f; // Reset when no progress bar is shown
		}
	}
}

void gui::renderer::components::configs::set_interpolated_fps() {
	if (interpolate_scale)
		settings.interpolated_fps = std::format("{}x", interpolated_fps_mult);
	else
		settings.interpolated_fps = std::to_string(interpolated_fps);
}

void gui::renderer::components::configs::options(ui::Container& container, BlurSettings& settings) {
	static const gfx::Color section_color = gfx::rgba(155, 155, 155, 255);

	bool first_section = true;
	auto section_component = [&](std::string label, bool* setting = nullptr) {
		if (!first_section) {
			ui::add_separator(std::format("section {} separator", label), container, ui::SeparatorStyle::FADE_RIGHT);
		}
		else
			first_section = false;

		if (setting) {
			ui::add_checkbox(std::format("section {} checkbox", label), container, label, *setting, fonts::font);
		}
		else {
			// ui::add_text(
			// 	std::format("section {}", label), container, label, gfx::rgba(255, 255, 255, 255), fonts::font
			// );
		}
	};

	/*
	    Blur
	*/
	section_component("blur", &settings.blur);

	if (settings.blur) {
		ui::add_slider("blur amount", container, 0.f, 2.f, &settings.blur_amount, "blur amount: {:.2f}", fonts::font);
		ui::add_slider("output fps", container, 1, 120, &settings.blur_output_fps, "output fps: {} fps", fonts::font);
		ui::add_dropdown(
			"blur weighting",
			container,
			"blur weighting",
			{ "gaussian", "gaussian_sym", "pyramid", "pyramid_sym", "custom_weight", "custom_function", "equal" },
			settings.blur_weighting,
			fonts::font
		);
	}

	/*
	    Interpolation
	*/
	section_component("interpolation", &settings.interpolate);

	if (settings.interpolate) {
		ui::add_checkbox(
			"interpolate scale checkbox",
			container,
			"interpolate by scaling fps",
			interpolate_scale,
			fonts::font,
			[&](bool new_value) {
				set_interpolated_fps();
			}
		);

		if (interpolate_scale) {
			ui::add_slider(
				"interpolated fps mult",
				container,
				1.f,
				10.f,
				&interpolated_fps_mult,
				"interpolated fps: {:.1f}x",
				fonts::font,
				[&](std::variant<int*, float*> value) {
					set_interpolated_fps();
				},
				0.1f
			);
		}
		else {
			ui::add_slider(
				"interpolated fps",
				container,
				1,
				2400,
				&interpolated_fps,
				"interpolated fps: {} fps",
				fonts::font,
				[&](std::variant<int*, float*> value) {
					set_interpolated_fps();
				}
			);
		}
	}

	/*
	    Rendering
	*/
	section_component("rendering");

	if (settings.ffmpeg_override.empty()) {
		ui::add_slider("quality", container, 0, 51, &settings.quality, "quality: {}", fonts::font, {}, 0.f);
	}
	else {
		ui::add_text(
			"ffmpeg override quality warning",
			container,
			"quality overridden by custom ffmpeg filters",
			gfx::rgba(252, 186, 3, 150),
			fonts::font
		);
	}

	ui::add_checkbox("deduplicate checkbox", container, "deduplicate", settings.deduplicate, fonts::font);

	ui::add_checkbox("preview checkbox", container, "preview", settings.preview, fonts::font);

	ui::add_checkbox(
		"detailed filenames checkbox", container, "detailed filenames", settings.detailed_filenames, fonts::font
	);

	ui::add_checkbox("copy dates checkbox", container, "copy dates", settings.copy_dates, fonts::font);

	/*
	    GPU Acceleration
	*/
	section_component("gpu acceleration");

	ui::add_checkbox("gpu decoding checkbox", container, "gpu decoding", settings.gpu_decoding, fonts::font);

	ui::add_checkbox(
		"gpu interpolation checkbox", container, "gpu interpolation", settings.gpu_interpolation, fonts::font
	);

	if (settings.ffmpeg_override.empty()) {
		ui::add_checkbox("gpu encoding checkbox", container, "gpu encoding", settings.gpu_encoding, fonts::font);

		if (settings.gpu_encoding) {
			ui::add_dropdown(
				"gpu encoding type dropdown",
				container,
				"gpu encoding - gpu type",
				{ "nvidia", "amd", "intel" },
				settings.gpu_type,
				fonts::font
			);
		}
	}
	else {
		ui::add_text(
			"ffmpeg override gpu encoding warning",
			container,
			"gpu encoding overridden by custom ffmpeg filters",
			gfx::rgba(252, 186, 3, 150),
			fonts::font
		);
	}

	/*
	    Timescale
	*/
	section_component("timescale", &settings.timescale);

	if (settings.timescale) {
		ui::add_slider(
			"input timescale",
			container,
			0.f,
			2.f,
			&settings.input_timescale,
			"input timescale: {:.2f}",
			fonts::font,
			{},
			0.01f
		);

		ui::add_slider(
			"output timescale",
			container,
			0.f,
			2.f,
			&settings.output_timescale,
			"output timescale: {:.2f}",
			fonts::font,
			{},
			0.01f
		);

		ui::add_checkbox(
			"adjust timescaled audio pitch checkbox",
			container,
			"adjust timescaled audio pitch",
			settings.output_timescale_audio_pitch,
			fonts::font
		);
	}

	/*
	    Filters
	*/
	section_component("filters", &settings.filters);

	if (settings.filters) {
		ui::add_slider(
			"brightness", container, 0.f, 2.f, &settings.brightness, "brightness: {:.2f}", fonts::font, {}, 0.01f
		);
		ui::add_slider(
			"saturation", container, 0.f, 2.f, &settings.saturation, "saturation: {:.2f}", fonts::font, {}, 0.01f
		);
		ui::add_slider("contrast", container, 0.f, 2.f, &settings.contrast, "contrast: {:.2f}", fonts::font, {}, 0.01f);
	}

	section_component("advanced", &settings.advanced);

	if (settings.advanced) {
		/*
		    Advanced Rendering
		*/
		section_component("advanced rendering");

		ui::add_text_input(
			"video container text input", container, settings.video_container, "video container", fonts::font
		);

		ui::add_slider(
			"deduplicate range",
			container,
			-1,
			10,
			&settings.deduplicate_range,
			"deduplicate range: {}",
			fonts::font,
			{},
			0.f,
			"-1 = infinite"
		);

		std::istringstream iss(settings.deduplicate_threshold);
		float f = NAN;
		iss >> std::noskipws >> f; // try to read as float
		bool is_float = iss.eof() && !iss.fail();

		if (!is_float)
			container.push_element_gap(2);

		ui::add_text_input(
			"deduplicate threshold input",
			container,
			settings.deduplicate_threshold,
			"deduplicate threshold",
			fonts::font
		);

		if (!is_float) {
			container.pop_element_gap();

			ui::add_text(
				"deduplicate threshold not a float warning",
				container,
				"deduplicate threshold must be a decimal number",
				gfx::rgba(255, 0, 0, 255),
				fonts::font
			);
		}

		bool bad_audio = settings.timescale && settings.ffmpeg_override.find("-c:a copy") != std::string::npos;
		if (bad_audio)
			container.push_element_gap(2);

		ui::add_text_input(
			"custom ffmpeg filters text input",
			container,
			settings.ffmpeg_override,
			"custom ffmpeg filters",
			fonts::font
		);

		if (bad_audio) {
			container.pop_element_gap();

			ui::add_text(
				"timescale audio copy warning",
				container,
				"cannot use -c:a copy while using timescale",
				gfx::rgba(255, 0, 0, 255),
				fonts::font
			);
		}

		ui::add_checkbox("debug checkbox", container, "debug", settings.debug, fonts::font);

		/*
		    Advanced Interpolation
		*/
		section_component("advanced interpolation");

		ui::add_dropdown(
			"interpolation preset dropdown",
			container,
			"interpolation preset",
			config_blur::INTERPOLATION_PRESETS,
			settings.interpolation_preset,
			fonts::font
		);

		ui::add_dropdown(
			"interpolation algorithm dropdown",
			container,
			"interpolation algorithm",
			config_blur::INTERPOLATION_ALGORITHMS,
			settings.interpolation_algorithm,
			fonts::font
		);

		ui::add_dropdown(
			"interpolation block size dropdown",
			container,
			"interpolation block size",
			config_blur::INTERPOLATION_BLOCK_SIZES,
			settings.interpolation_blocksize,
			fonts::font
		);

		ui::add_slider(
			"interpolation mask area slider",
			container,
			0,
			500,
			&settings.interpolation_mask_area,
			"interpolation mask area: {}",
			fonts::font
		);

		/*
		    Advanced Blur
		*/
		section_component("advanced blur");

		ui::add_slider(
			"blur weighting gaussian std dev slider",
			container,
			0.f,
			10.f,
			&settings.blur_weighting_gaussian_std_dev,
			"blur weighting gaussian std dev: {:.2f}",
			fonts::font
		);
		ui::add_checkbox(
			"blur weighting triangle reverse checkbox",
			container,
			"blur weighting triangle reverse",
			settings.blur_weighting_triangle_reverse,
			fonts::font
		);
		ui::add_text_input(
			"blur weighting bound input", container, settings.blur_weighting_bound, "blur weighting bound", fonts::font
		);
	}
}

// NOLINTBEGIN(readability-function-cognitive-complexity) todo: refactor
void gui::renderer::components::configs::preview(ui::Container& container, BlurSettings& settings) {
	static BlurSettings previewed_settings;
	static bool first = true;

	static auto debounce_time = std::chrono::milliseconds(50);
	auto now = std::chrono::steady_clock::now();
	static auto last_render_time = now;

	static size_t preview_id = 0;
	static std::filesystem::path preview_path;
	static bool loading = false;
	static bool error = false;
	static std::mutex preview_mutex;

	auto sample_video_path = blur.settings_path / "sample_video.mp4";
	bool sample_video_exists = std::filesystem::exists(sample_video_path);

	auto render_preview = [&] {
		if (!sample_video_exists) {
			preview_path.clear();
			return;
		}

		if (first) {
			first = false;
		}
		else {
			if (settings == previewed_settings && !first && !just_added_sample_video)
				return;

			if (now - last_render_time < debounce_time)
				return;
		}

		u::log("generating config preview");

		previewed_settings = settings;
		just_added_sample_video = false;
		last_render_time = now;

		{
			std::lock_guard<std::mutex> lock(preview_mutex);
			loading = true;
		}

		std::thread([sample_video_path, settings] {
			FrameRender* render = nullptr;

			{
				std::lock_guard<std::mutex> lock(render_mutex);

				// stop ongoing renders early, a new render's coming bro
				for (auto& render : renders) {
					render->stop();
				}

				render = renders.emplace_back(std::make_unique<FrameRender>()).get();
			}

			auto res = render->render(sample_video_path, settings);

			if (render == renders.back().get())
			{ // todo: this should be correct right? any cases where this doesn't work?
				loading = false;
				error = !res.success;

				if (!error) {
					std::lock_guard<std::mutex> lock(preview_mutex);
					preview_id++;

					Blur::remove_temp_path(preview_path.parent_path());

					preview_path = res.output_path;

					u::log("config preview finished rendering");
				}
				else {
					if (res.error_message != "Input path does not exist") {
						add_notification(
							"Failed to generate config preview. Click to copy error message",
							ui::NotificationType::NOTIF_ERROR,
							[res] {
								clip::set_text(res.error_message);
								add_notification(
									"Copied error message to clipboard",
									ui::NotificationType::INFO,
									{},
									std::chrono::duration<float>(2.f)
								);
							}
						);
					}
				}
			}

			render->set_can_delete();
		}).detach();
	};

	render_preview();

	// remove finished renders
	{
		std::lock_guard<std::mutex> lock(render_mutex);
		std::erase_if(renders, [](const auto& render) {
			return render->can_delete();
		});
	}

	if (!preview_path.empty() && std::filesystem::exists(preview_path) && !error) {
		auto element = ui::add_image(
			"config preview image",
			container,
			preview_path,
			container.get_usable_rect().size(),
			std::to_string(preview_id),
			gfx::rgba(255, 255, 255, loading ? 100 : 255)
		);
	}
	else {
		if (sample_video_exists) {
			if (loading) {
				ui::add_text(
					"loading config preview text",
					container,
					"Loading config preview...",
					gfx::rgba(255, 255, 255, 100),
					fonts::font,
					os::TextAlign::Center
				);
			}
			else {
				ui::add_text(
					"failed to generate preview text",
					container,
					"Failed to generate preview.",
					gfx::rgba(255, 255, 255, 100),
					fonts::font,
					os::TextAlign::Center
				);
			}
		}
		else {
			ui::add_text(
				"sample video does not exist text",
				container,
				"No preview video found.",
				gfx::rgba(255, 255, 255, 100),
				fonts::font,
				os::TextAlign::Center
			);

			ui::add_text(
				"sample video does not exist text 2",
				container,
				"Drop a video here to add one.",
				gfx::rgba(255, 255, 255, 100),
				fonts::font,
				os::TextAlign::Center
			);

			ui::add_button("open preview file button", container, "Open file", fonts::font, [] {
				base::paths paths;
				utils::show_file_selector("Blur input", "", {}, os::FileDialog::Type::OpenFile, paths);

				if (paths.size() != 1)
					return; // ??

				tasks::add_sample_video(base::from_utf8(paths[0]));
			});
		}
	}

	ui::add_separator("config preview separator", container, ui::SeparatorStyle::FADE_BOTH);

	auto validation_res = config_blur::validate(settings, false);
	if (!validation_res.success) {
		ui::add_text(
			"config validation error/s",
			container,
			validation_res.error,
			gfx::rgba(255, 50, 50, 255),
			fonts::font,
			os::TextAlign::Center,
			ui::TextStyle::OUTLINE
		);

		ui::add_button("fix config button", container, "Reset invalid config options to defaults", fonts::font, [&] {
			config_blur::validate(settings, true);
		});
	}

	ui::add_button("open config folder", container, "Open config folder", fonts::font, [] {
		base::launcher::open_folder(blur.settings_path.string());
	});
}

void gui::renderer::components::configs::option_information(ui::Container& container, BlurSettings& settings) {
	const static std::unordered_map<std::string, std::vector<std::string>> option_explanations = {
		// Blur settings
		// { "section blur checkbox",
		//   {
		// 	  "Enable motion blur",
		//   }, },
		{
			"blur amount",
			{
				"Amount of motion blur",
				"(0 = no blur, 1 = fully blend all frames, >1 = blend extra frames (ghosting))",
			},
		},
		// { "output fps",
		//   {
		// 	  "FPS of the output video",
		//   }, },
		{
			"blur weighting gaussian std dev slider",
			{
				"Standard deviation for Gaussian blur weighting",
			},
		},
		{
			"blur weighting triangle reverse checkbox",
			{
				"Reverses the direction of triangle weighting",
			},
		},
		{
			"blur weighting bound input",
			{
				"Weighting bounds to spread weights more",
			},
		},

		// Interpolation settings
		// { "section interpolation checkbox",
		//   {
		// 	  "Enable interpolation to a higher FPS before blurring",
		//   }, },
		{
			"interpolate scale checkbox",
			{
				"Use a multiplier for FPS interpolation rather than a set FPS",
			},
		},
		{
			"interpolated fps mult",
			{
				"Multiplier for FPS interpolation",
				"The input video will be interpolated to this FPS (before blurring)",
			},
		},
		{
			"interpolated fps",
			{
				"FPS to interpolate input video to (before blurring)",
			},
		},
		{
			"interpolation preset dropdown",
			{
				"Check the blur GitHub for more information",
			},
		},
		{
			"interpolation algorithm dropdown",
			{
				"Check the blur GitHub for more information",
			},
		},
		{
			"interpolation block size dropdown",
			{
				"Block size for interpolation",
				"(higher = less accurate, faster; lower = more accurate, slower)",
			},
		},
		{
			"interpolation mask area slider",
			{
				"Mask amount for interpolation",
				"(higher reduces blur on static objects but can affect smoothness)",
			},
		},

		// Rendering settings
		{
			"quality",
			{
				"Quality setting for output video",
				"(0 = lossless quality, 51 = really bad)",
			},
		},
		{
			"deduplicate checkbox",
			{
				"Removes duplicate frames and replaces them with interpolated frames",
				"(fixes 'unsmooth' looking output)",
			},
		},
		{
			"deduplicate range",
			{
				"Amount of frames beyond the current frame to look for unique frames when deduplicating",
				"Make it higher if your footage is at a lower FPS than it should be, e.g. choppy 120fps gameplay "
				"recorded at 240fps",
				"Lower it if your blurred footage starts blurring static elements such as menu screens",
			},
		},
		{
			"deduplicate threshold input",
			{
				"Threshold of movement that triggers deduplication",
				"Turn on debug in advanced and render a video to embed text showing the movement in each frame",
			},
		},
		{
			"preview checkbox",
			{
				"Shows preview while rendering",
			},
		},
		{
			"detailed filenames checkbox",
			{
				"Adds blur settings to generated filenames",
			},
		},

		// Timescale settings
		{
			"section timescale checkbox",
			{
				"Enable video timescale manipulation",
			},
		},
		{
			"input timescale",
			{
				"Timescale of the input video file",
			},
		},
		{
			"output timescale",
			{
				"Timescale of the output video file",
			},
		},
		{
			"adjust timescaled audio pitch checkbox",
			{
				"Pitch shift audio when speeding up or slowing down video",
			},
		},

		// Filters
		// { "section filters checkbox", { "Enable video filters", }, },
		// { "brightness", { "Adjusts brightness of the output video", }, },
		// { "saturation", { "Adjusts saturation of the output video", }, },
		// { "contrast", { "Adjusts contrast of the output video", }, },

		// Advanced rendering
		// { "gpu interpolation checkbox", { "Uses GPU for interpolation", }, },
		// { "gpu encoding checkbox", { "Uses GPU for rendering", }, },
		// { "gpu encoding type dropdown", { "Select GPU type", }, },
		{
			"video container text input",
			{
				"Output video container format",
			},
		},
		{
			"custom ffmpeg filters text input",
			{
				"Custom FFmpeg filters for rendering",
				"(overrides GPU & quality options)",
			},
		},
		// { "debug checkbox", { "Shows debug window and prints commands used by blur", } }
		{
			"copy dates checkbox",
			{
				"Copies over the modified date from the input",
			},
		},
	};

	ui::AnimatedElement* hovered = ui::get_hovered_element();

	if (!hovered || !hovered->element)
		return;

	if (!option_explanations.contains(hovered->element->id))
		return;

	ui::add_text(
		"hovered option info",
		container,
		option_explanations.at(hovered->element->id),
		gfx::rgba(255, 255, 255, 255),
		fonts::font,
		os::TextAlign::Center,
		ui::TextStyle::OUTLINE
	);
}

// NOLINTEND(readability-function-cognitive-complexity)

void gui::renderer::components::configs::screen(
	ui::Container& config_container,
	ui::Container& preview_container,
	ui::Container& option_information_container,
	float delta_time
) {
	auto parse_interp = [&] {
		try {
			auto split = u::split_string(settings.interpolated_fps, "x");
			if (split.size() > 1) {
				interpolated_fps_mult = std::stof(split[0]);
				interpolate_scale = true;
			}
			else {
				interpolated_fps = std::stof(settings.interpolated_fps);
				interpolate_scale = false;
			}

			u::log(
				"loaded interp, scale: {} (fps: {}, mult: {})",
				interpolate_scale,
				interpolated_fps,
				interpolated_fps_mult
			);
		}
		catch (std::exception& e) {
			u::log("failed to parse interpolated fps, setting defaults cos user error");
			set_interpolated_fps();
		}
	};

	auto on_load = [&] {
		current_global_settings = settings;
		parse_interp();
	};

	auto save_config = [&] {
		config_blur::create(config_blur::get_global_config_path(), settings);
		current_global_settings = settings;

		u::log("saved global settings");
	};

	if (!loaded_config) {
		settings = config_blur::parse_global_config();
		on_load();
		loaded_config = true;
	}

	bool config_changed = settings != current_global_settings;

	if (config_changed) {
		ui::set_next_same_line(nav_container);
		ui::add_button("save button", nav_container, "Save", fonts::font, [&] {
			save_config();
		});

		ui::set_next_same_line(nav_container);
		ui::add_button("reset changes button", nav_container, "Reset changes", fonts::font, [&] {
			settings = current_global_settings;
			on_load();
		});
	}

	if (settings != BlurSettings{}) {
		ui::set_next_same_line(nav_container);
		ui::add_button("restore defaults button", nav_container, "Restore defaults", fonts::font, [&] {
			settings = BlurSettings{};
			parse_interp();
		});
	}

	options(config_container, settings);

	preview(preview_container, settings);

	option_information(option_information_container, settings);
}

// NOLINTBEGIN(readability-function-size,readability-function-cognitive-complexity)

bool gui::renderer::redraw_window(os::Window* window, bool force_render) {
	ui::on_frame_start();

	set_cursor_this_frame = false;

	auto now = std::chrono::steady_clock::now();
	static auto last_frame_time = now;

	// todo: first render in a batch might be fucked, look at progress bar skipping fully to complete instantly on
	// 25 speed - investigate
	static bool first = true;

#if DEBUG_RENDER
	float fps = -1.f;
#endif
	float delta_time = NAN;

	if (first) {
		delta_time = DEFAULT_DELTA_TIME;
		first = false;
	}
	else {
		float time_since_last_frame =
			std::chrono::duration<float>(std::chrono::steady_clock::now() - last_frame_time).count();

#if DEBUG_RENDER
		fps = 1.f / time_since_last_frame;

// float current_fps = 1.f / time_since_last_frame;
// if (fps == -1.f)
// 	fps = current_fps;
// fps = (fps * FPS_SMOOTHING) + (current_fps * (1.0f - FPS_SMOOTHING));
#endif

		delta_time = std::min(time_since_last_frame, MIN_DELTA_TIME);
	}

	last_frame_time = now;

	os::Surface* surface = window->surface();
	const gfx::Rect rect = surface->bounds();

	static float bg_overlay_shade = 0.f;
	float last_fill_shade = bg_overlay_shade;
	bg_overlay_shade = u::lerp(bg_overlay_shade, drag_handler::dragging ? 30.f : 0.f, 25.f * delta_time);
	force_render |= bg_overlay_shade != last_fill_shade;

	gfx::Rect nav_container_rect = rect;
	nav_container_rect.h = 70;
	nav_container_rect.y = rect.y2() - nav_container_rect.h;

	ui::reset_container(nav_container, nav_container_rect, fonts::font.getSize(), {});

	int nav_cutoff = rect.y2() - nav_container_rect.y;
	int bottom_pad = std::max(PAD_Y, nav_cutoff);

	const static int main_pad_x = std::min(100, window->width() / 10); // bit of magic never hurt anyone
	ui::reset_container(main_container, rect, 13, ui::Padding{ PAD_Y, main_pad_x, bottom_pad, main_pad_x });

	const int config_page_container_gap = PAD_X / 2;

	gfx::Rect config_container_rect = rect;
	config_container_rect.w = 200 + PAD_X * 2;

	ui::reset_container(config_container, config_container_rect, 9, ui::Padding{ PAD_Y, PAD_X, bottom_pad, PAD_X });

	gfx::Rect config_preview_container_rect = rect;
	config_preview_container_rect.x = config_container_rect.x2() + config_page_container_gap;
	config_preview_container_rect.w -= config_container_rect.w + config_page_container_gap;

	ui::reset_container(
		config_preview_container,
		config_preview_container_rect,
		fonts::font.getSize(),
		ui::Padding{ PAD_Y, PAD_X, bottom_pad, PAD_X }
	);

	ui::reset_container(
		option_information_container, config_preview_container_rect, 9, ui::Padding{ PAD_Y, PAD_X, bottom_pad, PAD_X }
	);

	gfx::Rect notification_container_rect = rect;
	notification_container_rect.w = 230;
	notification_container_rect.x = rect.x2() - notification_container_rect.w - NOTIFICATIONS_PAD_X;
	notification_container_rect.h = 300;
	notification_container_rect.y = NOTIFICATIONS_PAD_Y;

	ui::reset_container(notification_container, notification_container_rect, 6, {});

	switch (screen) {
		case Screens::MAIN: {
			components::configs::loaded_config = false;

			components::main_screen(main_container, delta_time);

			if (initialisation_res && initialisation_res->success) {
				auto current_render = rendering.get_current_render();
				if (current_render) {
					ui::add_button("stop render button", nav_container, "Stop current render", fonts::font, [] {
						auto current_render = rendering.get_current_render();
						if (current_render)
							(*current_render)->stop();
					});
				}

				ui::set_next_same_line(nav_container);
				ui::add_button("config button", nav_container, "Config", fonts::font, [] {
					screen = Screens::CONFIG;
				});
			}

			ui::center_elements_in_container(main_container);

			break;
		}
		case Screens::CONFIG: {
			ui::set_next_same_line(nav_container);
			ui::add_button("back button", nav_container, "Back", fonts::font, [] {
				screen = Screens::MAIN;
			});

			components::configs::screen(
				config_container, config_preview_container, option_information_container, delta_time
			);

			ui::center_elements_in_container(config_preview_container);
			ui::center_elements_in_container(option_information_container, true, false);

			break;
		}
	}

	render_notifications();

	ui::center_elements_in_container(nav_container);

	bool want_to_render = false;
	want_to_render |= ui::update_container_frame(notification_container, delta_time);
	want_to_render |= ui::update_container_frame(nav_container, delta_time);

	want_to_render |= ui::update_container_frame(main_container, delta_time);
	want_to_render |= ui::update_container_frame(config_container, delta_time);
	want_to_render |= ui::update_container_frame(config_preview_container, delta_time);
	want_to_render |= ui::update_container_frame(option_information_container, delta_time);
	ui::on_update_frame_end();

	if (!want_to_render && !force_render)
		// note: DONT RENDER ANYTHING ABOVE HERE!!! todo: render queue?
		return false;

	// background
	render::rect_filled(surface, rect, gfx::rgba(0, 0, 0, 255));

#if DEBUG_RENDER
	{
		// debug
		static const int debug_box_size = 30;
		static float x = rect.x2() - debug_box_size;
		static float y = 100.f;
		static bool right = false;
		static bool down = true;
		x += 1.f * (right ? 1 : -1);
		y += 1.f * (down ? 1 : -1);

		render::rect_filled(surface, gfx::Rect(x, y, debug_box_size, debug_box_size), gfx::rgba(255, 0, 0, 50));

		if (right) {
			if (x + debug_box_size > rect.x2())
				right = false;
		}
		else {
			if (x < 0)
				right = true;
		}

		if (down) {
			if (y + debug_box_size > rect.y2())
				down = false;
		}
		else {
			if (y < 0)
				down = true;
		}
	}
#endif

	ui::render_container(surface, main_container);
	ui::render_container(surface, config_container);
	ui::render_container(surface, config_preview_container);
	ui::render_container(surface, option_information_container);
	ui::render_container(surface, nav_container);
	ui::render_container(surface, notification_container);

	// file drop overlay
	if ((int)bg_overlay_shade > 0)
		render::rect_filled(surface, rect, gfx::rgba(255, 255, 255, (gfx::ColorComponent)bg_overlay_shade));

#if DEBUG_RENDER
	if (fps != -1.f) {
		gfx::Point fps_pos(rect.x2() - PAD_X, rect.y + PAD_Y);
		render::text(
			surface,
			fps_pos,
			gfx::rgba(0, 255, 0, 255),
			std::format("{:.0f} fps", fps),
			fonts::font,
			os::TextAlign::Right
		);
	}
#endif

	// todo: whats this do
	if (!window->isVisible())
		window->setVisible(true);

	window->invalidateRegion(gfx::Region(rect));

	return want_to_render;
}

// NOLINTEND(readability-function-size,readability-function-cognitive-complexity)

void gui::renderer::add_notification(
	const std::string& id,
	const std::string& text,
	ui::NotificationType type,
	const std::optional<std::function<void()>>& on_click,
	std::chrono::duration<float> duration
) {
	std::lock_guard<std::mutex> lock(notification_mutex);

	Notification new_notification{
		.id = id,
		.end_time = std::chrono::steady_clock::now() +
		            std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration),
		.text = text,
		.type = type,
		.on_click_fn = on_click,
	};

	for (auto& notification : notifications) {
		if (notification.id == id) {
			notification = new_notification;
			return;
		}
	}

	notifications.emplace_back(new_notification);
}

void gui::renderer::add_notification(
	const std::string& text,
	ui::NotificationType type,
	const std::optional<std::function<void()>>& on_click,
	std::chrono::duration<float> duration
) {
	static uint32_t current_notification_id = 0;
	add_notification(std::to_string(current_notification_id++), text, type, on_click, duration);
}

void gui::renderer::on_render_finished(Render* render, const RenderResult& result) {
	if (result.stopped) {
		add_notification(
			std::format("Render '{}' stopped", base::to_utf8(render->get_video_name())), ui::NotificationType::INFO
		);
	}
	else if (result.success) {
		auto output_path = render->get_output_video_path();

		add_notification(
			std::format("Render '{}' completed", base::to_utf8(render->get_video_name())),
			ui::NotificationType::SUCCESS,
			[output_path] {
				base::launcher::open_folder(output_path.string());
			}
		);
	}
	else {
		add_notification(
			std::format("Render '{}' failed. Click to copy error message", base::to_utf8(render->get_video_name())),
			ui::NotificationType::NOTIF_ERROR,
			[result] {
				clip::set_text(result.error_message);
				add_notification(
					"Copied error message to clipboard",
					ui::NotificationType::INFO,
					{},
					std::chrono::duration<float>(2.f)
				);
			}
		);
	}
}

void gui::renderer::render_notifications() {
	std::lock_guard<std::mutex> lock(notification_mutex);

	auto now = std::chrono::steady_clock::now();

	for (auto it = notifications.begin(); it != notifications.end();) {
		ui::add_notification(
			std::format("notification {}", it->id),
			notification_container,
			it->text,
			it->type,
			fonts::font,
			it->on_click_fn
		);

		if (now > it->end_time)
			it = notifications.erase(it);
		else
			++it;
	}
}
