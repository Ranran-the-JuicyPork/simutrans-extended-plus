/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <string.h>
#include <cmath>

#include "gui_chart.h"
#include "../gui_frame.h"
#include "../simwin.h"
#include "../../simcolor.h"
#include "../../simworld.h" // sprintf_time_secs
#include "../../utils/simstring.h"
#include "../../dataobj/environment.h"
#include "../../display/simgraph.h"
#include "../gui_theme.h"

static char tooltip[64];

/**
 * Set background color. -1 means no background
 */
void gui_chart_t::set_background(FLAGGED_PIXVAL color)
{
	background = color;
}


gui_chart_t::gui_chart_t() : gui_component_t()
{
	// no toolstips at the start
	tooltipcoord = scr_coord::invalid;

	seed = 0;
	show_x_axis = true;
	show_y_axis = true;
	ltr = 1;
	x_elements = 0;
	x_axis_span = 1;
	highlight_x = -1;
	min_size = scr_size(0,0);

	// transparent by default
	background = TRANSPARENT_FLAGS;
}

void gui_chart_t::set_min_size(scr_size s)
{
	min_size = s;
}

scr_size gui_chart_t::get_min_size() const
{
	return min_size;
}

scr_size gui_chart_t::get_max_size() const
{
	return scr_size::inf;
}


uint32 gui_chart_t::add_curve(PIXVAL color, const sint64 *values, int size, int offset, int elements, int type, bool show, bool show_value, int precision, convert_proc proc, chart_marker_t marker_type)
{
	curve_t new_curve;
	new_curve.color = color;
	new_curve.values = values;
	new_curve.size = size;
	new_curve.offset = offset;
	new_curve.elements = elements;
	new_curve.show = show;
	new_curve.show_value = show_value;
	new_curve.type = type;
	switch (type) {
		case MONEY:    new_curve.suffix = "$";    break;
		case PERCENT:  new_curve.suffix = "%";    break;
		case DISTANCE: new_curve.suffix = "km";   break;
		case FORCE:    new_curve.suffix = "kN";   break;
		case KMPH:     new_curve.suffix = "km/h"; break;
		case PAX_KM:   new_curve.suffix = translator::translate("pkm");  break;
		case SEAT_KM:  new_curve.suffix = translator::translate("skm");  break;
		case KG_KM:    new_curve.suffix = translator::translate("kgkm"); break;
		case TON_KM:
		case TON_KM_MAIL:
			           new_curve.suffix = translator::translate("tkm");  break;
		case KW:       new_curve.suffix = "kW";   break;
		case TONNEN:   new_curve.suffix = translator::translate("tonnen");   break;
		default:       new_curve.suffix = NULL;   break;
	}
	new_curve.precision = precision;
	new_curve.convert = proc;
	new_curve.marker_type = marker_type;
	curves.append(new_curve);
	return curves.get_count()-1;
}


void gui_chart_t::hide_curve(unsigned int id)
{
	if (id < curves.get_count()) {
		curves.at(id).show = false;
	}
}


void gui_chart_t::show_curve(unsigned int id)
{
	if (id < curves.get_count()) {
		curves.at(id).show = true;
	}
}


void gui_chart_t::draw(scr_coord offset)
{
	chart_layout_t layout;
	layout.chart_offset = scr_coord(100,0);
	layout.maximum_axis_len = 22;

	// Only charts with a changeable orientation will follow the env_t::left_to_right_graphs setting.
	layout.left_to_right_graph = (ltr==1) ? env_t::left_to_right_graphs : (ltr==2) ? false : true;

	offset += pos;
	if(  size.w<D_DEFAULT_WIDTH  ) {
		layout.chart_offset.x = size.w/6;
		layout.maximum_axis_len = max(7,layout.chart_offset.x/6);
	}

	offset += layout.chart_offset;
	layout.offset = offset;
	layout.chart_size = size-layout.chart_offset-scr_size(10,4+LINESPACE);

	char cmin[128] = "0", cmax[128] = "0";
	sint64 baseline = 0;
	double scale = 0;

	// calc baseline and scale
	calc_gui_chart_values(&baseline, &scale, cmin, cmax, 18);

	draw_axes(layout, baseline, cmin, cmax);
	draw_curves(layout, baseline, scale, cmin);
}


void gui_chart_t::draw_axes(chart_layout_t& layout, sint64 baseline, const char* cmin, const char* cmax) const
{
	const scr_coord offset = layout.offset;
	const scr_coord chart_offset = layout.chart_offset;
	const scr_size chart_size = layout.chart_size;
	const int maximum_axis_len = layout.maximum_axis_len;
	const bool left_to_right_graph = layout.left_to_right_graph;
	char digit[11];
	if(background != TRANSPARENT_FLAGS) {
		display_fillbox_wh_clip_rgb(offset.x, offset.y, chart_size.w, chart_size.h, background, false);
	}
	int tmpx, factor;
	if(  left_to_right_graph  ) {
		const int xoff = x_elements>1 ? chart_size.w % (x_elements - 1) : 0;
		tmpx = offset.x + chart_size.w - xoff;
		factor = -1;
	}
	else {
		tmpx = offset.x;
		factor = 1;
	}

	// draw zero line
	display_direct_line_rgb(offset.x+1, offset.y+(scr_coord_val)baseline, offset.x+chart_size.w-2, offset.y+(scr_coord_val)baseline, SYSCOL_CHART_LINES_ZERO);

	if (show_y_axis) {

		// draw zero number only, if it will not disturb any other printed values!
		if ((baseline > 18) && (baseline < chart_size.h -18)) {
			display_proportional_clip_rgb(offset.x - 4, offset.y+(scr_coord_val)baseline-3, "0", ALIGN_RIGHT, SYSCOL_TEXT_HIGHLIGHT, true );
		}

		// display min/max money values
		display_proportional_clip_rgb(offset.x - 4, offset.y-5, cmax, ALIGN_RIGHT, SYSCOL_TEXT_HIGHLIGHT, true );
		display_proportional_clip_rgb(offset.x - 4, offset.y+chart_size.h-5, cmin, ALIGN_RIGHT, SYSCOL_TEXT_HIGHLIGHT, true );
	}

	// draw chart frame
	display_ddd_box_clip_rgb(offset.x, offset.y, chart_size.w, chart_size.h, SYSCOL_SHADOW, SYSCOL_HIGHLIGHT);

	// draw chart lines
	scr_coord_val x_last = 0;  // remember last digit position to avoid overwriting by next label
	for(  int i = 0;  i < x_elements;  i++  ) {
		const int j = left_to_right_graph ? x_elements - 1 - i : i;
		const scr_coord_val x0 = x_elements>1 ? tmpx + factor * (chart_size.w / (x_elements - 1) ) * j : 0;
		const bool highlighted = highlight_x<0 ? false : left_to_right_graph ? highlight_x==x_elements-1-i : highlight_x == i;
		const PIXVAL line_color = highlighted ? SYSCOL_TEXT_STRONG : (i%2) ? SYSCOL_CHART_LINES_ODD : SYSCOL_CHART_LINES_EVEN;
		if(  show_x_axis  ) {
			// display x-axis
			int val = (abort_display_x && left_to_right_graph) ? (abort_display_x - j - 1) * x_axis_span : seed - (j*x_axis_span);
			sprintf(digit, "%i", abs(val));
			scr_coord_val x =  x0 - (seed != j ? (int)(2 * log( (double)abs(seed - j) )) : 0);
			if(  x > x_last  ) {
				x_last = x + display_proportional_clip_rgb( x, offset.y + chart_size.h + 6, digit, ALIGN_LEFT, line_color, true );
			}
		}
		// year's vertical lines
		display_vline_wh_clip_rgb( x0, offset.y + 1, chart_size.h - 2, line_color, false );
	}

	// display current value?
	int tooltip_n=-1;
	int ttcx = tooltipcoord.x-chart_offset.x;
	if(  ttcx>0  &&  ttcx<chart_size.w  ) {
		const uint8 temp_x = abort_display_x ? (left_to_right_graph ? abort_display_x : x_elements-abort_display_x) : x_elements;
		if(  left_to_right_graph  ) {
			tooltip_n = x_elements-1-(ttcx*temp_x+4)/(chart_size.w|1);
		}
		else {
			tooltip_n = (ttcx*temp_x+4)/(chart_size.w|1);
		}
	}

	layout.x_origin = tmpx;
	layout.x_direction = factor;
	layout.tooltip_index = tooltip_n;
}


void gui_chart_t::draw_curves(const chart_layout_t& layout, sint64 baseline, double scale, char* cmin) const
{
	const scr_coord offset = layout.offset;
	const scr_size chart_size = layout.chart_size;
	const int maximum_axis_len = layout.maximum_axis_len;
	const bool left_to_right_graph = layout.left_to_right_graph;
	const int tooltip_n = layout.tooltip_index;
	const int tmpx = layout.x_origin;
	const int factor = layout.x_direction;
	sint64 last_year = 0;
	sint64 tmp = 0;
	for(curve_t const& c : curves) {
		if (c.show) {
			double display_tmp;
			int start = abort_display_x ? (left_to_right_graph ? c.elements - abort_display_x : 0) : 0;
			int end   = abort_display_x ? (left_to_right_graph ? c.elements : abort_display_x) : c.elements;
			// for each curve iterate through all elements and display curve
			for (int i = start; i < end; i++) {

				tmp = get_curve_value(c, i);
				display_tmp = get_curve_display_value(c, tmp);

				// display marker(box) for financial value
				if (i < end) {
					scr_coord_val x = x_elements>1 ? tmpx + factor * (chart_size.w / (x_elements-1))*(i - start) - 2 : 0;
					scr_coord_val y = (scr_coord_val)(offset.y + baseline - (long)(tmp / scale) - 2);
					switch (c.marker_type)
					{
						case cross:
							display_direct_line_rgb(x, y, x + 4, y + 4, c.color);
							display_direct_line_rgb(x + 4, y, x, y + 4, c.color);
							break;
						case diamond:
							for (int j = 0; j < 5; j++) {
								display_vline_wh_clip_rgb(x + j, y + abs(2 - j), 5 - 2 * abs(2 - j), c.color, false);
							}
							break;
						case round_box:
							display_filled_roundbox_clip(x, y, 5, 5, c.color, true);
							break;
						case none:
							// display nothing
							break;
						case square:
						default:
							display_fillbox_wh_clip_rgb(x, y, 5, 5, c.color, true);
							break;
					}
				}


				// display tooltip?
				show_curve_tooltip(c, i, tooltip_n, tmp, display_tmp, baseline, scale);

				// draw line between two financial markers; this is only possible from the second value on
				if (i > start && i < end) {
					display_direct_line_rgb(tmpx + factor * (chart_size.w / (x_elements - 1))*(i-start-1),
						(scr_coord_val)( offset.y+baseline-(int)(last_year/scale) ),
						tmpx+factor*(chart_size.w / (x_elements - 1))*(i - start),
						(scr_coord_val)( offset.y+baseline-(int)(tmp/scale) ),
						c.color);
				}
				else if ( (i==0 && !abort_display_x)  ||  (abort_display_x && i==start) ) {
					// for the first element print the current value (optionally)
					// only print value if not too narrow to min/max/zero
					if(  c.show_value  ) {
						number_to_string_fit(cmin, display_tmp, c.precision, maximum_axis_len - (c.suffix != NULL) );
						if (c.suffix) {
							strcat(cmin, c.suffix);
						}

						if(  left_to_right_graph  ) {
							const sint16 width = proportional_string_width(cmin)+7;
							display_ddd_proportional( tmpx + 8, (scr_coord_val)(offset.y+baseline-(int)(tmp/scale)-4), width, 0, c.color, is_dark_color(c.color) ? color_idx_to_rgb(COL_WHITE) : color_idx_to_rgb(COL_BLACK), cmin, true);
						}
						else if(  (baseline-tmp/scale-8) > 0  &&  (baseline-tmp/scale+8) < chart_size.h  &&  abs((int)(tmp/scale)) > 9  ) {
							display_proportional_rgb(tmpx - 4, (scr_coord_val)(offset.y+baseline-(int)(tmp/scale)-4), cmin, ALIGN_RIGHT, c.color, true );
						}
					}
				}
				last_year=tmp;
			}
		}
		last_year=tmp=0;
	}
}



void gui_chart_t::calc_gui_chart_values(sint64 *baseline, double *scale, char *cmin, char *cmax, int maximum_axis_len) const
{
	chart_value_range_t range;
	calc_gui_chart_range(range);
	format_gui_chart_axis_labels(range, cmin, cmax, maximum_axis_len);
	calc_gui_chart_scale(range, baseline, scale);
}


sint64 gui_chart_t::get_curve_value(const curve_t& curve, int index) const
{
	sint64 value = curve.values[index*curve.size+curve.offset];
	return curve.convert ? curve.convert(value) : value;
}


double gui_chart_t::get_curve_display_value(const curve_t& curve, sint64 value) const
{
	if (curve.type == KMPH) {
		return (double)speed_to_kmh(value*10)/10.0;
	}
	return value / pow(10, curve.precision);
}


void gui_chart_t::show_curve_tooltip(const curve_t& curve, int index, int tooltip_index, sint64 value, double display_value, sint64 baseline, double scale) const
{
	if (index == tooltip_index && abs((int)(baseline-(int)(value/scale)-tooltipcoord.y)) < 10) {
		if (curve.type == TIME) {
			world()->sprintf_time_secs(tooltip, sizeof(tooltip), display_value);
		}
		else {
			number_to_string(tooltip, display_value, curve.precision);
			if (curve.suffix) {
				strcat(tooltip, curve.suffix);
			}
		}
		win_set_tooltip(get_mouse_pos() + TOOLTIP_MOUSE_OFFSET, tooltip );
	}
}


void gui_chart_t::calc_gui_chart_range(chart_value_range_t& range) const
{
	sint64 tmp = 0;
	range.min_value = 0;
	range.max_value = 0;
	range.min_suffix = NULL;
	range.max_suffix = NULL;
	range.precision = 0;
	range.convert_kmph = false;
	range.convert_time = false;

	for(curve_t const& c : curves) {
		if(  c.show  ) {
			for(  int i=0;  i<c.elements;  i++  ) {
				tmp = get_curve_value(c, i);
				if (!c.convert && c.type == KMPH) {
					range.convert_kmph = true;
					range.precision = 0;
				}
				else if (!c.convert && c.type == TIME) {
					range.convert_time = true;
					range.precision = 0;
				}
				else if (!c.convert && c.type == FORCE) {
// 					// convert_n_to_kn remains unused for the existing force chart.
				}
				if (range.min_value > tmp) {
					range.min_value = tmp;
					range.precision = c.precision;
					range.min_suffix = c.suffix;
				}
				if (range.max_value < tmp) {
					range.max_value = tmp;
					range.precision = c.precision;
					range.max_suffix = c.suffix;
				}
			}
		}
	}

	// max happend due to rounding errors
	if( range.precision == 0   &&  range.min_value == range.max_value ) {
		range.max_value += 1;
	}
}


void gui_chart_t::format_gui_chart_axis_labels(const chart_value_range_t& range, char* cmin, char* cmax, int maximum_axis_len) const
{
	if (range.convert_time) {
		world()->sprintf_time_secs(cmin, 32, (uint32)range.min_value);
		world()->sprintf_time_secs(cmax, 32, (uint32)range.max_value);
	}
	else {
		// if accel chart => Drawing accuracy hack: simspeed to integer km/h. (Do not rewrite min max for scaling)
		number_to_string_fit(cmin, range.convert_kmph ? speed_to_kmh((int)range.min_value) : (double)range.min_value/pow(10,range.precision), range.precision, maximum_axis_len - (range.min_suffix != 0));
		number_to_string_fit(cmax, range.convert_kmph ? speed_to_kmh((int)range.max_value) : (double)range.max_value/pow(10,range.precision), range.precision, maximum_axis_len - (range.max_suffix != 0));

		if(  range.min_suffix  ) {
			strcat( cmin, range.min_suffix );
		}
		if(  range.max_suffix  ) {
			strcat( cmax, range.max_suffix );
		}
	}
}


void gui_chart_t::calc_gui_chart_scale(const chart_value_range_t& range, sint64* baseline, double* scale) const
{
	// scale: factor to calculate money with, to get y-pos offset
	*scale = (double)(range.max_value - range.min_value) / (size.h-1-4-LINESPACE);
	if(*scale==0.0) {
		*scale = 1.0;
	}

	// baseline: y-pos for the "zero" line in the chart
	*baseline = (sint64)(size.h-1-4-LINESPACE - labs((sint64)(range.min_value / *scale )));
}


/**
 * Events are notified to GUI components via this method
 */
bool gui_chart_t::infowin_event(const event_t *ev)
{
	if(ev->button_state==1) {
		// tooptip to show?
		tooltipcoord = scr_coord(ev->mouse_pos.x,ev->mouse_pos.y);
	}
	else {
		tooltipcoord = scr_coord::invalid;
		tooltip[0] = 0;
	}
	return true;
}
