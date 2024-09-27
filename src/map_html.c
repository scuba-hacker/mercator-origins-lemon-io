#include <stdint.h>

#ifndef MAP_HTML_C
#define MAP_HTML_C

const char MAP_HTML[] = R"rawliteral(

<!DOCTYPE html>
<html xmlns="https://www.w3.org/1999/xhtml">
	<head>
		<title>wraysbury-traces + wraysbury-waypoints</title>
		<base target="_top"></base>
		<meta http-equiv="Content-Type" content="text/html; charset=UTF-8" />
		<meta name="viewport" content="initial-scale=1.0, user-scalable=no" />
		<meta name="geo.position" content="51.4607297; -0.5476644" />
		<meta name="ICBM" content="51.4607297, -0.5476644" />
	</head>
	<body style="margin:0px;">
		
		<script type="text/javascript">
			API = 'google'; // can be either 'leaflet' or 'google'
			if (self.API && API.match(/^g/i)) {
				google_api_key = ''; // Your project's Google Maps API key goes here (https://code.google.com/apis/console)
				language_code = '';
				document.writeln('<script src="https://maps.googleapis.com/maps/api/js?v=3&amp;libraries=geometry&amp;language='+(self.language_code?self.language_code:'')+'&amp;key='+(self.google_api_key?self.google_api_key:'')+'" type="text/javascript"><'+'/script>');
			} else {
				document.writeln('<link href="https://www.gpsvisualizer.com/leaflet/leaflet.css" rel="stylesheet" />');
				document.writeln('<script src="https://www.gpsvisualizer.com/leaflet/leaflet.js" type="text/javascript"><'+'/script>');
			}
			thunderforest_api_key = ''; // To display OpenStreetMap tiles from ThunderForest, you need a key (https://www.thunderforest.com/docs/apikeys/)
			ign_api_key = ''; // To display topo tiles from IGN.fr, you need a key (https://api.ign.fr/)
		</script>

		
		<!--
			If you want to transplant this map into another Web page, by far the best method is to
			simply include it in a IFRAME tag (see https://www.gpsvisualizer.com/faq.html#google_html).
			But, if you must paste the code into another page, be sure to include all of these parts:
			   1. The "div" tags that contain the map and its widgets, below
			   2. Three sections of JavaScript code:
			      a. The API code (from googleapis.com or /leaflet), above
			      b. "gv_options" and the code that calls a .js file on gpsvisualizer.com
			      c. The "GV_Map" function, which contains all the geographic info for the map
		-->
		<div style="margin-left:0px; margin-right:0px; margin-top:0px; margin-bottom:0px;">
			<div id="gmap_div" style="width:100%; height:100%; margin:0px; margin-right:12px; background-color:#f0f0f0; float:left; overflow:hidden;">
				<p style="text-align:center; font:10px Arial;">This map was created using <a target="_blank" href="https://www.gpsvisualizer.com/">GPS Visualizer</a>'s do-it-yourself geographic utilities.<br /><br />Please wait while the map data loads...</p>
			</div>
				
			<div id="gv_infobox" class="gv_infobox" style="font:11px Arial; border:solid #666666 1px; background-color:#ffffff; padding:4px; overflow:auto; display:none; max-width:700px;">
				<!-- Although GPS Visualizer didn't create an legend/info box with your map, you can use this space for something else if you'd like; enable it by setting gv_options.infobox_options.enabled to true -->
			</div>

			<div id="gv_tracklist" class="gv_tracklist" style="font:11px Arial; line-height:11px; background-color:#ffffff; overflow:auto; display:none;"><!-- --></div>

			<div id="gv_marker_list" class="gv_marker_list" style="background-color:#ffffff; overflow:auto; display:none;"><!-- --></div>

			<div id="gv_clear_margins" style="height:0px; clear:both;"><!-- clear the "float" --></div>
		</div>

		
		<!-- begin GPS Visualizer setup script (must come after loading of API code) -->
		<script type="text/javascript">
			/* Global variables used by the GPS Visualizer functions (20240903030415): */
			gv_options = {};
			
			// basic map parameters:
			gv_options.center = [51.4607297214285,-0.5476643625];  // [latitude,longitude] - be sure to keep the square brackets
			gv_options.zoom = 17;  // higher number means closer view; can also be 'auto' for automatic zoom/center based on map elements
			gv_options.map_type = 'GV_OSM';  // popular map_type choices are 'GV_STREET', 'GV_SATELLITE', 'GV_HYBRID', 'GV_TERRAIN', 'GV_OSM', 'GV_TOPO_US', 'GV_TOPO_WORLD' (https://www.gpsvisualizer.com/misc/google_map_types.html)
			gv_options.map_opacity = 1.00;  // number from 0 to 1
			gv_options.full_screen = true;  // true|false: should the map fill the entire page (or frame)?
			gv_options.width = 1000;  // width of the map, in pixels
			gv_options.height = 1000;  // height of the map, in pixels
			
			gv_options.map_div = 'gmap_div';  // the name of the HTML "div" tag containing the map itself; usually 'gmap_div'
			gv_options.doubleclick_zoom = true;  // true|false: zoom in when mouse is double-clicked?
			gv_options.doubleclick_center = true;  // true|false: re-center the map on the point that was double-clicked?
			gv_options.scroll_zoom = true; // true|false; or 'reverse' for down=in and up=out
			gv_options.page_scrolling = true; // true|false; does the map relenquish control of the scroll wheel when embedded in scrollable pages?
			gv_options.autozoom_adjustment = 0; gv_options.autozoom_default = 11;
			gv_options.centering_options = { 'open_info_window':true, 'partial_match':true, 'center_key':'center', 'default_zoom':null } // URL-based centering (e.g., ?center=name_of_marker&zoom=14)
			gv_options.street_view = false; // true|false: allow Google Street View on the map (Google Maps only)
			gv_options.tilt = false; // true|false: allow Google Maps to show 45-degree tilted aerial imagery?
			gv_options.disable_google_pois = false;  // true|false: if you disable clickable POIs on Google Maps, you also lose the labels on parks, airports, etc.
			gv_options.animated_zoom = true; // true|false: only affects Leaflet maps
			
			// widgets on the map:
			gv_options.zoom_control = 'auto'; // 'auto'|'large'|'small'|'none'
			gv_options.recenter_button = true; // true|false: is there a 'click to recenter' button above the zoom control?
			gv_options.geolocation_control = false; // true|false; only works on secure servers
			gv_options.geolocation_options = { center:true, zoom:null, marker:true, info_window:true };
			gv_options.scale_control = true; // true|false
			gv_options.map_opacity_control = false;  // true|false
			gv_options.map_type_control = {};  // widget to change the background map
			  gv_options.map_type_control.visible = 'auto'; // true|false|'auto': is a map type control placed on the map itself?
			  gv_options.map_type_control.filter = false;  // true|false: when map loads, are irrelevant maps ignored?
			  gv_options.map_type_control.excluded = [];  // comma-separated list of quoted map IDs that will never show in the list ('included' also works)
			gv_options.center_coordinates = true;  // true|false: show a "center coordinates" box and crosshair?
			gv_options.measurement_tools = true; // true|false: put a measurement ruler on the map?
			gv_options.measurement_options = { visible:false, distance_color:'', area_color:'' };
			gv_options.crosshair_hidden = true;  // true|false: hide the crosshair initially?
			gv_options.mouse_coordinates = false;  // true|false: show a "mouse coordinates" box?
			gv_options.utilities_menu = { 'maptype':true, 'opacity':true, 'measure':true, 'geolocate':true, 'profile':true };
			gv_options.allow_export = true;  // true|false
			
			gv_options.infobox_options = {}; // options for a floating info box (id="gv_infobox"), which can contain anything
			  gv_options.infobox_options.enabled = true;  // true|false: enable or disable the info box altogether
			  gv_options.infobox_options.position = ['LEFT_TOP',52,4];  // [Google anchor name, relative x, relative y]
			  gv_options.infobox_options.draggable = true;  // true|false: can it be moved around the screen?
			  gv_options.infobox_options.collapsible = true;  // true|false: can it be collapsed by double-clicking its top bar?
			
			// track-related options:
			gv_options.track_optimization = 1; // sets Leaflet's smoothFactor parameter
			gv_options.track_tooltips = false; // true|false: should the name of a track appear on the map when you mouse over the track itself?
			gv_options.tracklist_options = {}; // options for a floating list of the tracks visible on the map
			  gv_options.tracklist_options.enabled = true;  // true|false: enable or disable the tracklist altogether
			  gv_options.tracklist_options.position = ['RIGHT_TOP',4,32];  // [Google anchor name, relative x, relative y]
			  gv_options.tracklist_options.min_width = 100; // minimum width of the tracklist, in pixels
			  gv_options.tracklist_options.max_width = 180; // maximum width of the tracklist, in pixels
			  gv_options.tracklist_options.min_height = 0; // minimum height of the tracklist, in pixels; if the list is longer, scrollbars will appear
			  gv_options.tracklist_options.max_height = 460; // maximum height of the tracklist, in pixels; if the list is longer, scrollbars will appear
			  gv_options.tracklist_options.desc = true;  // true|false: should tracks' descriptions be shown in the list
			  gv_options.tracklist_options.toggle = false;  // true|false: should clicking on a track's name turn it on or off?
			  gv_options.tracklist_options.checkboxes = true;  // true|false: should there be a separate icon/checkbox for toggling visibility?
			  gv_options.tracklist_options.zoom_links = true;  // true|false: should each item include a small icon that will zoom to that track?
			  gv_options.tracklist_options.highlighting = true;  // true|false: should the track be highlighted when you mouse over the name in the list?
			  gv_options.tracklist_options.tooltips = false;  // true|false: should the name of the track appear on the map when you mouse over the name in the list?
			  gv_options.tracklist_options.draggable = true;  // true|false: can it be moved around the screen?
			  gv_options.tracklist_options.collapsible = true;  // true|false: can it be collapsed by double-clicking its top bar?
			  gv_options.tracklist_options.header = 'Tracks:'; // HTML code; be sure to put backslashes in front of any single quotes, and don't include any line breaks
			  gv_options.tracklist_options.footer = ''; // HTML code
			gv_options.profile_options = { visible:false, icon:true, units:'metric', filled:true, waypoints:true, height:120, width:'100%', y_min:null, y_max:null, gap_between_tracks:false }; // see https://www.gpsvisualizer.com/tutorials/profiles_in_maps.html


			// marker-related options:
			gv_options.default_marker = { color:'red',icon:'googlemini',scale:1 }; // icon can be a URL, but be sure to also include size:[w,h] and optionally anchor:[x,y]
			gv_options.vector_markers = true; // are the icons on the map in embedded SVG format?
			gv_options.marker_tooltips = true; // do the names of the markers show up when you mouse-over them?
			gv_options.marker_shadows = true; // true|false: do the standard markers have "shadows" behind them?
			gv_options.marker_link_target = '_blank'; // the name of the window or frame into which markers' URLs will load
			gv_options.info_window_width = 0;  // in pixels, the width of the markers' pop-up info "bubbles" (can be overridden by 'window_width' in individual markers)
			gv_options.thumbnail_width = 0;  // in pixels, the width of the markers' thumbnails (can be overridden by 'thumbnail_width' in individual markers)
			gv_options.photo_size = [0,0];  // in pixels, the size of the photos in info windows (can be overridden by 'photo_width' or 'photo_size' in individual markers)
			gv_options.hide_labels = false;  // true|false: hide labels when map first loads?
			gv_options.labels_behind_markers = false; // true|false: are the labels behind other markers (true) or in front of them (false)?
			gv_options.label_offset = [0,0];  // [x,y]: shift all markers' labels (positive numbers are right and down)
			gv_options.label_centered = false;  // true|false: center labels with respect to their markers?  (label_left is also a valid option.)
			gv_options.driving_directions = false;  // put a small "driving directions" form in each marker's pop-up window? (override with dd:true or dd:false in a marker's options)
			gv_options.garmin_icon_set = 'gpsmap'; // 'gpsmap' are the small 16x16 icons; change it to '24x24' for larger icons
			gv_options.marker_list_options = {};  // options for a dynamically-created list of markers
			  gv_options.marker_list_options.enabled = true;  // true|false: enable or disable the marker list altogether
			  gv_options.marker_list_options.floating = true;  // is the list a floating box inside the map itself?
			  gv_options.marker_list_options.position = ['RIGHT_BOTTOM',6,38];  // floating list only: position within map
			  gv_options.marker_list_options.min_width = 160; // minimum width, in pixels, of the floating list
			  gv_options.marker_list_options.max_width = 160;  // maximum width
			  gv_options.marker_list_options.min_height = 0;  // minimum height, in pixels, of the floating list
			  gv_options.marker_list_options.max_height = 460;  // maximum height
			  gv_options.marker_list_options.draggable = true;  // true|false, floating list only: can it be moved around the screen?
			  gv_options.marker_list_options.collapsible = true;  // true|false, floating list only: can it be collapsed by double-clicking its top bar?
			  gv_options.marker_list_options.include_tickmarks = false;  // true|false: are distance/time tickmarks included in the list?
			  gv_options.marker_list_options.include_trackpoints = false;  // true|false: are "trackpoint" markers included in the list?
			  gv_options.marker_list_options.dividers = true;  // true|false: will a thin line be drawn between each item in the list?
			  gv_options.marker_list_options.desc = true;  // true|false: will the markers' descriptions be shown below their names in the list?
			  gv_options.marker_list_options.icons = true;  // true|false: should the markers' icons appear to the left of their names in the list?
			  gv_options.marker_list_options.icon_scale = 'x1'; // size of the icons in the list; you can also supply a size in pixels (e.g. '16px')
			  gv_options.marker_list_options.thumbnails = false;  // true|false: should markers' thumbnails be shown in the list?
			  gv_options.marker_list_options.folders_collapsed = true;  // true|false: do folders in the list start out in a collapsed state?
			  gv_options.marker_list_options.folders_hidden = false;  // true|false: do folders in the list start out in a hidden state?
			  gv_options.marker_list_options.collapsed_folders = []; // an array of folder names
			  gv_options.marker_list_options.hidden_folders = []; // an array of folder names
			  gv_options.marker_list_options.count_folder_items = false;  // true|false: list the number of items in each folder?
			  gv_options.marker_list_options.folder_zoom = true;  // true|false: is there a zoom link next to each folder name?
			  gv_options.marker_list_options.folders_first = true;  // true|false: do folders in the list come before un-foldered markers?
			  gv_options.marker_list_options.wrap_names = true;  // true|false: should marker's names be allowed to wrap onto more than one line?
			  gv_options.marker_list_options.unnamed = '[unnamed]';  // what 'name' should be assigned to  unnamed markers in the list?
			  gv_options.marker_list_options.colors = false;  // true|false: should the names/descs of the points in the list be colorized the same as their markers?
			  gv_options.marker_list_options.default_color = '';  // default HTML color code for the names/descs in the list
			  gv_options.marker_list_options.limit = 0;  // how many markers to show in the list; 0 for no limit
			  gv_options.marker_list_options.center = false;  // true|false: does the map center upon a marker when you click its name in the list?
			  gv_options.marker_list_options.zoom = false;  // true|false: does the map zoom to a certain level when you click on a marker's name in the list?
			  gv_options.marker_list_options.zoom_level = 18;  // if 'zoom' is true, what level should the map zoom to?
			  gv_options.marker_list_options.info_window = true;  // true|false: do info windows pop up when the markers' names are clicked in the list?
			  gv_options.marker_list_options.url_links = false;  // true|false: do the names in the list become instant links to the markers' URLs?
			  gv_options.marker_list_options.toggle = false;  // true|false: does a marker disappear if you click on its name in the list?
			  gv_options.marker_list_options.help_tooltips = false;  // true|false: do "tooltips" appear on marker names that tell you what happens when you click?
			  gv_options.marker_list_options.id = 'gv_marker_list';  // id of a DIV tag that holds the list
			  gv_options.marker_list_options.header = ''; // HTML code; be sure to put backslashes in front of any single quotes, and don't include any line breaks
			  gv_options.marker_list_options.footer = ''; // HTML code
			gv_options.marker_filter_options = {};  // options for removing waypoints that are out of the current view
			  gv_options.marker_filter_options.enabled = false;  // true|false: should out-of-range markers be removed?
			  gv_options.marker_filter_options.movement_threshold = 8;  // in pixels, how far the map has to move to trigger filtering
			  gv_options.marker_filter_options.limit = 0;  // maximum number of markers to display on the map; 0 for no limit
			  gv_options.marker_filter_options.update_list = true;  // true|false: should the marker list be updated with only the filtered markers?
			  gv_options.marker_filter_options.sort_list_by_distance = false;  // true|false: should the marker list be sorted by distance from the center of the map?
			  gv_options.marker_filter_options.min_zoom = 0;  // below this zoom level, don't show any markers at all
			  gv_options.marker_filter_options.zoom_message = '';  // message to put in the marker list if the map is below the min_zoom threshold
			gv_options.synthesize_fields = {}; // for example: {label:'{name}'} would cause all markers' names to become visible labels
				

			
			// Load GPS Visualizer's mapping functions (this must be loaded AFTER gv_options are set):
			var script_file = (self.API && API.match(/^g/i)) ? 'google_maps/functions3.js' : 'leaflet/functions.js';
			document.writeln('<script src="https://www.gpsvisualizer.com/'+script_file+'" type="text/javascript"><'+'/script>');
		</script>
		<style type="text/css">
			/* Put any custom style definitions here (e.g., .gv_marker_info_window, .gv_marker_info_window_name, .gv_marker_list_item, .gv_tooltip, .gv_label, etc.) */
			#gmap_div .gv_marker_info_window {
				font-size:11px !important;
			}
			#gmap_div .gv_label {
				opacity:0.90; filter:alpha(opacity=90);
				color:white; background:#333333; border:1px solid black; padding:1px;
				font-family:Verdana !important; font-size:10px;
				font-weight:normal !important;
			}
			.legend_block {
				display:inline-block; border:solid 1px black; width:9px; height:9px; margin:0px 2px 0px 0px;
			}
			
		</style>
		
		<!-- end GPSV setup script and styles; begin map-drawing script (they must be separate) -->
		<script type="text/javascript">
			function GV_Map() {
				GV_Setup_Map();
				
				// Track #1
				t = 1; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Bus'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = 'green'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = 'green'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.460035,-0.548529,-3.7],[51.460037,-0.548529,-3.8],[51.460036,-0.548531,-3.7],[51.460034,-0.548532,-3.7],[51.460034,-0.548535,-3.7],[51.460032,-0.54854,-3.7],[51.460029,-0.548547,-3.7],[51.460028,-0.548553,-3.7],[51.460026,-0.548558,-3.7],[51.460023,-0.548565,-3.7],[51.460021,-0.548572,-3.7],[51.460018,-0.548579,-3.7],[51.460017,-0.548586,-3.7],[51.460017,-0.548594,-3.8],[51.46002,-0.548601,-3.8],[51.460023,-0.548605,-3.8],[51.460028,-0.548609,-3.8],[51.460034,-0.548616,-3.8],[51.460034,-0.54862,-3.7],[51.460038,-0.548623,-3.7],[51.460043,-0.548623,-3.7],[51.460047,-0.54862,-3.7],[51.460048,-0.548614,-3.7],[51.46005,-0.548606,-3.7],[51.460054,-0.5486,-3.8],[51.460056,-0.548598,-3.7],[51.460059,-0.548591,-3.7],[51.46006,-0.548585,-3.7],[51.460062,-0.548579,-3.7],[51.460064,-0.548572,-3.8],[51.460069,-0.548567,-3.7],[51.460072,-0.548561,-3.8],[51.460074,-0.548555,-3.7],[51.460078,-0.548547,-3.8],[51.460081,-0.54854,-3.8],[51.460084,-0.548535,-3.7],[51.460086,-0.548532,-3.7],[51.460089,-0.548524,-4],[51.460092,-0.548517,-4.1],[51.460095,-0.548512,-4],[51.460098,-0.548493,-4.2],[51.460097,-0.548487,-4.1],[51.460094,-0.548482,-4.1],[51.460092,-0.54848,-4.1],[51.460091,-0.548476,-4],[51.46009,-0.548474,-4.2],[51.460088,-0.548473,-4.6],[51.460086,-0.548471,-4.6],[51.460084,-0.54847,-4.6],[51.460082,-0.54847,-4.6],[51.46008,-0.548472,-4.6],[51.460081,-0.548473,-4.6],[51.460081,-0.548474,-4.6],[51.460079,-0.548474,-4.6],[51.460077,-0.548477,-4.7],[51.460076,-0.548478,-4.6],[51.460074,-0.548474,-4.6],[51.460074,-0.548471,-4.6],[51.460071,-0.548474,-4.6],[51.460069,-0.548477,-4.6],[51.460066,-0.548481,-4.6],[51.460064,-0.548488,-4.6],[51.460061,-0.548492,-4.5],[51.460059,-0.548497,-4.6],[51.460056,-0.548502,-4.6],[51.460054,-0.548506,-4.6],[51.460053,-0.54851,-4.6],[51.460051,-0.548513,-4.6],[51.460049,-0.548516,-4.6],[51.460048,-0.54852,-4.7],[51.460047,-0.548523,-4.7],[51.460047,-0.548524,-4.7],[51.460046,-0.548526,-4.7],[51.460045,-0.548527,-4.7],[51.460043,-0.548527,-4.7],[51.460042,-0.548528,-4.8],[51.460041,-0.54853,-4.8],[51.46004,-0.548531,-4.7],[51.460038,-0.548535,-4.8],[51.460037,-0.548538,-4.7],[51.460036,-0.548538,-4.7],[51.460034,-0.54854,-4.7] ] });
				GV_Draw_Track(t);
				
				// Track #2
				t = 2; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Cave System'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = 'blue'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = 'blue'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.460846,-0.548827,-5.5],[51.46085,-0.548829,-5.6],[51.460853,-0.54883,-5.6],[51.460857,-0.548832,-5.5],[51.46086,-0.548835,-5.6],[51.460862,-0.548837,-5.5],[51.460865,-0.54884,-5.5],[51.460871,-0.548845,-5.5],[51.460875,-0.548847,-5.4],[51.460876,-0.548849,-5.4],[51.460881,-0.548852,-5.4],[51.460885,-0.548856,-5.4],[51.460889,-0.54886,-5.4],[51.460894,-0.548864,-5.4],[51.460898,-0.548868,-5.4],[51.460903,-0.548872,-5.4],[51.460908,-0.548875,-5.4],[51.460914,-0.548876,-5.5],[51.460919,-0.548874,-5.5],[51.460923,-0.548869,-5.5],[51.460928,-0.548864,-5.5],[51.460932,-0.548859,-5.5],[51.460934,-0.548853,-5.5],[51.460937,-0.548848,-5.4],[51.460941,-0.54884,-5.5],[51.460944,-0.548834,-5.5],[51.460946,-0.548829,-5.4],[51.460949,-0.548823,-5.5],[51.460952,-0.548816,-5.8],[51.460954,-0.54881,-6],[51.460956,-0.548803,-6.1],[51.460957,-0.548796,-6.2],[51.460959,-0.548788,-6.2],[51.460959,-0.54878,-6.3],[51.460957,-0.548772,-6.3],[51.460955,-0.548767,-6.3],[51.460951,-0.548764,-6.3],[51.460947,-0.54876,-6.3],[51.460943,-0.548758,-6.2],[51.46094,-0.548755,-6.2],[51.460936,-0.548751,-6.2],[51.460932,-0.548747,-6.2],[51.460929,-0.548743,-6.2],[51.460926,-0.548739,-6.2],[51.460923,-0.548735,-6.2],[51.460921,-0.548731,-6.3],[51.460917,-0.548724,-6.4],[51.460912,-0.548718,-6.4],[51.460909,-0.548714,-6.4],[51.460906,-0.548711,-6.4],[51.460902,-0.548705,-6.6],[51.460896,-0.548698,-6.8],[51.460892,-0.548695,-6.8],[51.460888,-0.548691,-6.9],[51.460883,-0.548686,-6.9],[51.460879,-0.548683,-6.9],[51.460874,-0.548678,-6.9],[51.46087,-0.548674,-6.9],[51.460866,-0.548669,-6.9],[51.460862,-0.548665,-6.8],[51.460858,-0.548659,-6.8],[51.460853,-0.548654,-6.9],[51.460848,-0.54865,-6.9],[51.460844,-0.548646,-6.8],[51.460839,-0.54864,-6.8],[51.460834,-0.548636,-6.8],[51.46083,-0.548633,-6.8],[51.460825,-0.548632,-6.9],[51.46082,-0.548632,-6.9],[51.460815,-0.548633,-6.7],[51.46081,-0.548638,-6.8],[51.460805,-0.548643,-6.6],[51.460802,-0.548647,-6.6],[51.460799,-0.548652,-6.6],[51.460796,-0.548659,-6.6],[51.460794,-0.548664,-6.6],[51.460792,-0.548672,-6.6],[51.46079,-0.548678,-6.6],[51.460789,-0.548684,-6.6],[51.46079,-0.548689,-6.6],[51.460793,-0.548696,-6.6],[51.460795,-0.5487,-6.6],[51.460797,-0.548705,-6.6],[51.460798,-0.548711,-6.6],[51.460798,-0.548718,-6.6],[51.460797,-0.548727,-6.6],[51.460794,-0.548737,-6.6],[51.460793,-0.548744,-6.6],[51.460793,-0.548751,-6.6],[51.460795,-0.548759,-6.6],[51.460797,-0.548766,-6.6],[51.4608,-0.548771,-6.6],[51.460804,-0.548777,-6.6],[51.460808,-0.548781,-6.6],[51.460811,-0.548784,-6.6],[51.460816,-0.548788,-6.6],[51.46082,-0.548791,-6.6],[51.460823,-0.548793,-6.5],[51.460828,-0.548796,-6.4],[51.460831,-0.548799,-6.3],[51.460834,-0.548803,-6.2],[51.46084,-0.548806,-6],[51.460845,-0.54881,-6],[51.460848,-0.548813,-5.7],[51.460851,-0.548816,-5.7] ] });
				GV_Draw_Track(t);
				
				// Track #3
				t = 3; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Wreck Graveyard'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = 'red'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = 'red'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.460467,-0.547375,-6.3],[51.460464,-0.547372,-6.4],[51.460461,-0.54737,-6.4],[51.460459,-0.547366,-6.4],[51.460457,-0.547359,-6.2],[51.460456,-0.547354,-6.3],[51.460456,-0.547352,-6.3],[51.460456,-0.547348,-6.2],[51.460456,-0.547344,-6.1],[51.460458,-0.54734,-6.1],[51.460459,-0.547336,-6.2],[51.460461,-0.547332,-6.1],[51.460464,-0.547327,-6.4],[51.460467,-0.547322,-6.4],[51.46047,-0.547315,-6.2],[51.460473,-0.547311,-6.2],[51.460476,-0.547305,-6.3],[51.460478,-0.5473,-6.3],[51.46048,-0.547294,-6.3],[51.460481,-0.547288,-6.2],[51.460483,-0.547281,-6.3],[51.460484,-0.547275,-6.2],[51.460485,-0.547269,-6.2],[51.460487,-0.547262,-6.2],[51.460486,-0.547257,-6],[51.460486,-0.547252,-5.8],[51.460485,-0.547247,-6],[51.460483,-0.547238,-6],[51.460482,-0.547235,-5.9],[51.460482,-0.547234,-5.8],[51.460481,-0.547234,-5.8],[51.460479,-0.547233,-5.8],[51.460478,-0.547232,-5.7],[51.460476,-0.547235,-5.8],[51.460476,-0.547237,-5.8],[51.460475,-0.547239,-5.8],[51.460474,-0.54724,-5.9],[51.460473,-0.54724,-5.9],[51.460472,-0.54724,-6],[51.46047,-0.54724,-6.1],[51.460468,-0.547239,-6.1],[51.460467,-0.54724,-5.9],[51.460465,-0.547242,-5.6],[51.460462,-0.547243,-5.5],[51.460461,-0.547243,-5.5],[51.460462,-0.547235,-5.6],[51.460461,-0.547228,-5.6],[51.46046,-0.547221,-5.5],[51.460459,-0.547216,-5.5],[51.460459,-0.547207,-5.7],[51.460456,-0.547199,-5.7],[51.460455,-0.547195,-5.7],[51.460451,-0.54719,-5.7],[51.460447,-0.547186,-5.7],[51.460442,-0.547182,-5.7],[51.46044,-0.547179,-5.6],[51.460437,-0.547177,-5.7],[51.460432,-0.547173,-5.9],[51.460427,-0.547169,-6],[51.460418,-0.547165,-6.3],[51.46041,-0.547166,-6.4],[51.460406,-0.547167,-6.3],[51.460403,-0.547169,-6.3],[51.460402,-0.54717,-6.2],[51.4604,-0.547172,-6.1],[51.460397,-0.547176,-6.1],[51.460393,-0.547183,-6.2],[51.460391,-0.547188,-6.2],[51.460388,-0.547192,-6.3],[51.460386,-0.547193,-6.6],[51.460383,-0.547195,-6.6],[51.460379,-0.547198,-6.7],[51.460376,-0.547199,-6.8],[51.460373,-0.547199,-6.8],[51.46037,-0.547199,-6.8],[51.460368,-0.547198,-6.7],[51.460366,-0.547198,-6.7],[51.460362,-0.547199,-6.8],[51.460361,-0.547197,-6.7],[51.46036,-0.547194,-6.6],[51.460361,-0.547194,-6.7],[51.460358,-0.547196,-6.8],[51.460355,-0.547193,-6.6],[51.460354,-0.547191,-6.6],[51.46035,-0.54719,-6.7],[51.460345,-0.54719,-6.7],[51.460341,-0.547192,-7],[51.460337,-0.547195,-7],[51.460334,-0.547196,-6.9],[51.460331,-0.5472,-6.9],[51.460328,-0.547207,-7.2],[51.460328,-0.547211,-7.2],[51.460327,-0.547215,-7.3],[51.460327,-0.547222,-7.2],[51.460329,-0.547229,-7.3],[51.460332,-0.547236,-7.3],[51.460334,-0.547237,-7.3],[51.460335,-0.547237,-7.2],[51.460339,-0.547241,-7.3],[51.460346,-0.54725,-6.8],[51.460349,-0.547253,-6.9],[51.460352,-0.547258,-6.8],[51.460357,-0.547263,-6.7],[51.460359,-0.547267,-6.6],[51.460361,-0.54727,-6.6],[51.460363,-0.547273,-6.5],[51.460366,-0.54728,-6.7],[51.460368,-0.547287,-6.8],[51.460371,-0.547295,-6.9],[51.460372,-0.5473,-6.9],[51.460373,-0.547307,-6.8],[51.460375,-0.547314,-6.9],[51.460376,-0.547318,-6.8],[51.460377,-0.547322,-6.6],[51.460378,-0.547327,-6.7],[51.460379,-0.547332,-6.7],[51.460381,-0.547337,-6.7],[51.460384,-0.547343,-6.7],[51.460386,-0.547348,-6.7],[51.460388,-0.547352,-6.8],[51.460391,-0.547357,-6.8],[51.460395,-0.547363,-6.7],[51.460397,-0.547368,-6.7],[51.4604,-0.547373,-6.4],[51.460402,-0.547374,-6.5],[51.460404,-0.547373,-6.5],[51.460406,-0.547379,-6.5],[51.460406,-0.547388,-6.7],[51.460405,-0.547394,-6.7],[51.460404,-0.547399,-6.7],[51.460403,-0.547405,-6.7],[51.460402,-0.547411,-6.7],[51.460401,-0.547415,-6.7],[51.4604,-0.547426,-6.7],[51.4604,-0.547432,-6.8],[51.460401,-0.547437,-6.7],[51.460403,-0.547443,-6.7],[51.460406,-0.547448,-6.7],[51.460409,-0.547455,-6.7],[51.460412,-0.547462,-6.9],[51.460417,-0.547467,-7.1],[51.46042,-0.547471,-7.1],[51.460425,-0.547476,-7.4],[51.46043,-0.547481,-7.4],[51.460434,-0.547485,-7.4],[51.460438,-0.547488,-7.4],[51.460443,-0.547489,-7.5],[51.460447,-0.54749,-7.5],[51.460452,-0.547489,-7.6],[51.460457,-0.547485,-7.6],[51.460458,-0.547481,-7.5],[51.46046,-0.547476,-7.5],[51.460462,-0.547472,-7.5],[51.460461,-0.547465,-7.3],[51.460461,-0.547461,-7.2],[51.460461,-0.547457,-7.1],[51.46046,-0.547453,-6.9],[51.46046,-0.547449,-6.6],[51.46046,-0.547445,-6.6],[51.460459,-0.547441,-6.5],[51.460458,-0.547435,-6.5],[51.460458,-0.547429,-6.5],[51.460459,-0.547423,-6.5],[51.46046,-0.547418,-6.4],[51.460461,-0.547411,-6.4],[51.460461,-0.547404,-6.3],[51.460461,-0.547397,-6.3],[51.460462,-0.547391,-6.3],[51.460463,-0.547381,-6.1],[51.460464,-0.547376,-6],[51.460464,-0.547372,-5.9],[51.460466,-0.547368,-5.8] ] });
				GV_Draw_Track(t);
				
				// Track #4
				t = 4; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Overhead Hose Hazard'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = 'orange'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = 'orange'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.460448,-0.548984,-3.6],[51.460449,-0.548985,-3.6],[51.460451,-0.548987,-3.6],[51.460453,-0.548987,-3.6],[51.460455,-0.548987,-3.6],[51.460456,-0.548986,-3.7],[51.460456,-0.548983,-3.6],[51.460455,-0.54898,-3.7],[51.460456,-0.548977,-3.6],[51.460457,-0.548976,-3.6],[51.460457,-0.548972,-3.6],[51.460458,-0.548968,-3.6],[51.460458,-0.548964,-3.6],[51.460458,-0.548961,-3.6],[51.460458,-0.548958,-3.6],[51.460458,-0.548954,-3.6],[51.460457,-0.548951,-3.5],[51.460456,-0.548949,-3.5],[51.460456,-0.548946,-3.5],[51.460455,-0.548941,-3.5],[51.460454,-0.548936,-3.5],[51.460454,-0.548932,-3.5],[51.460453,-0.548928,-3.5],[51.460453,-0.548925,-3.4],[51.460453,-0.548921,-3.7],[51.460452,-0.548917,-3.8],[51.460452,-0.548913,-3.8],[51.460451,-0.548909,-3.9],[51.46045,-0.548905,-3.8],[51.46045,-0.548902,-3.8],[51.460449,-0.548898,-4],[51.460449,-0.548894,-4.1],[51.460449,-0.54889,-4],[51.460449,-0.548887,-4.3],[51.460448,-0.548883,-4.3],[51.460448,-0.54888,-4.6],[51.460448,-0.548877,-4.7],[51.460448,-0.548874,-4.7],[51.460448,-0.548872,-4.9],[51.460448,-0.548869,-5.1],[51.460447,-0.548866,-5.1],[51.460447,-0.548864,-5.2],[51.460447,-0.548861,-5.4],[51.460446,-0.548857,-5.6],[51.460446,-0.548854,-5.5],[51.460446,-0.548852,-5.7],[51.460446,-0.548849,-6],[51.460445,-0.548846,-6.2],[51.460445,-0.548843,-6.2],[51.460445,-0.54884,-6.2],[51.460445,-0.548837,-6.2],[51.460444,-0.548835,-6.2] ] });
				GV_Draw_Track(t);
				
				// Track #5
				t = 5; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Plane'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = 'brown'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = 'brown'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.459719,-0.546611,-6],[51.459719,-0.546613,-6],[51.459719,-0.546615,-6],[51.459719,-0.546617,-5.9],[51.459719,-0.546621,-5.9],[51.459719,-0.546625,-5.9],[51.459719,-0.546629,-5.8],[51.459719,-0.546632,-5.8],[51.459718,-0.54664,-5.8],[51.459718,-0.546643,-5.8],[51.459718,-0.546646,-5.8],[51.459718,-0.546649,-5.8],[51.459719,-0.546652,-5.8],[51.459719,-0.546655,-5.8],[51.459719,-0.546659,-5.8],[51.459719,-0.546662,-5.8],[51.459719,-0.546665,-5.8],[51.459719,-0.546668,-5.8],[51.45972,-0.546671,-5.8],[51.45972,-0.546674,-5.8],[51.45972,-0.546677,-5.8],[51.459721,-0.54668,-5.8],[51.459721,-0.546682,-5.7],[51.459721,-0.546684,-5.7],[51.459722,-0.546686,-5.7],[51.459722,-0.546688,-5.7],[51.459723,-0.546692,-5.6],[51.459722,-0.546696,-5.6],[51.459722,-0.546699,-5.6],[51.459722,-0.546704,-5.6],[51.459723,-0.546709,-5.6],[51.459724,-0.546715,-5.6],[51.459728,-0.546723,-5.7],[51.45973,-0.54673,-5.8],[51.459731,-0.546735,-5.8],[51.459732,-0.54674,-5.8],[51.459732,-0.546745,-5.8],[51.459733,-0.546749,-5.8],[51.459737,-0.546758,-5.9],[51.459739,-0.546761,-5.9],[51.459742,-0.546761,-6],[51.459743,-0.546759,-6],[51.459743,-0.546761,-5.9],[51.459745,-0.546759,-5.9],[51.459746,-0.546759,-5.9],[51.459749,-0.546757,-6],[51.459751,-0.546756,-6],[51.459752,-0.546755,-5.9],[51.459753,-0.546752,-5.9],[51.459753,-0.54675,-5.9],[51.459754,-0.546748,-5.9],[51.459755,-0.546745,-5.9],[51.459755,-0.546743,-5.9],[51.459755,-0.54674,-5.9],[51.459755,-0.546737,-5.9],[51.459756,-0.546734,-5.9],[51.459756,-0.546731,-5.9],[51.459757,-0.546724,-5.9],[51.459757,-0.54672,-5.9],[51.459757,-0.546717,-5.9],[51.459757,-0.546713,-5.9],[51.459757,-0.54671,-5.9],[51.459756,-0.546707,-5.9],[51.459756,-0.546704,-5.9],[51.459757,-0.5467,-5.9],[51.459758,-0.546697,-5.9],[51.459758,-0.546693,-5.9],[51.459758,-0.54669,-5.9],[51.459758,-0.546687,-5.9],[51.459759,-0.546683,-5.8],[51.459759,-0.54668,-5.8],[51.459759,-0.546677,-5.8],[51.45976,-0.546674,-5.8],[51.45976,-0.54667,-5.8],[51.45976,-0.546666,-5.8],[51.45976,-0.546663,-5.8],[51.45976,-0.546659,-5.8],[51.459759,-0.546651,-5.8],[51.45976,-0.546647,-5.8],[51.459759,-0.546643,-5.9],[51.459759,-0.546639,-5.9],[51.459759,-0.546635,-5.9],[51.459759,-0.54663,-5.9],[51.459759,-0.546627,-5.9],[51.459759,-0.546622,-5.9],[51.459759,-0.546617,-5.9],[51.459758,-0.546613,-5.9],[51.459758,-0.54661,-5.9],[51.459758,-0.546607,-6],[51.459756,-0.546605,-6],[51.459755,-0.546603,-6],[51.459753,-0.546602,-6],[51.459751,-0.546602,-5.9],[51.459749,-0.546601,-5.9],[51.459747,-0.546601,-5.9],[51.459744,-0.5466,-5.9],[51.459741,-0.546599,-5.9],[51.459739,-0.546599,-5.9],[51.459736,-0.5466,-5.9],[51.459731,-0.5466,-6],[51.459729,-0.5466,-6],[51.459727,-0.5466,-6],[51.459724,-0.546602,-6],[51.459723,-0.546604,-6],[51.459723,-0.546605,-6],[51.459722,-0.546604,-5.9],[51.459723,-0.546606,-6],[51.459723,-0.546608,-6],[51.459723,-0.54661,-6],[51.459722,-0.546611,-6] ] });
				GV_Draw_Track(t);
                
				// Track #6
				t = 6; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'The Hole'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = '#e60000'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = '#e60000'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.460432,-0.548717,-9.8],[51.460433,-0.548716,-9.7],[51.460433,-0.548715,-9.7],[51.460434,-0.548714,-9.8],[51.460436,-0.548713,-9.8],[51.460437,-0.548712,-9.8],[51.460438,-0.548711,-9.8],[51.460439,-0.548709,-9.8],[51.46044,-0.548708,-9.7],[51.460441,-0.548706,-9.7],[51.460442,-0.548705,-9.8],[51.460443,-0.548703,-9.8],[51.460445,-0.548701,-9.8],[51.460446,-0.548698,-9.8],[51.460447,-0.548696,-9.8],[51.460448,-0.548694,-9.8],[51.460448,-0.548693,-9.8],[51.460449,-0.548688,-9.8],[51.46045,-0.548685,-9.8],[51.46045,-0.548682,-9.8],[51.460451,-0.54868,-9.7],[51.460451,-0.548679,-9.7],[51.460451,-0.548677,-9.7],[51.460451,-0.548675,-9.7],[51.460451,-0.548674,-9.7],[51.460451,-0.548672,-9.6],[51.460451,-0.548671,-9.7],[51.460451,-0.548669,-9.6],[51.460451,-0.548667,-9.6],[51.460451,-0.548661,-9.6],[51.460451,-0.548659,-9.6],[51.46045,-0.548654,-9.6],[51.460449,-0.548652,-9.7],[51.460448,-0.54865,-9.7],[51.460446,-0.548645,-9.7],[51.460445,-0.548643,-9.7],[51.460444,-0.548642,-9.7],[51.460443,-0.54864,-9.7],[51.460442,-0.548639,-9.7],[51.46044,-0.548638,-9.6],[51.460439,-0.548637,-9.6],[51.460437,-0.548636,-9.7],[51.460435,-0.548636,-9.6],[51.460433,-0.548635,-9.7],[51.460432,-0.548635,-9.6],[51.460431,-0.548635,-9.6],[51.46043,-0.548636,-9.6],[51.460428,-0.548636,-9.6],[51.460427,-0.548636,-9.5],[51.460425,-0.548637,-9.5],[51.460423,-0.548633,-9.6],[51.46042,-0.548634,-9.6],[51.460418,-0.548635,-9.6],[51.460417,-0.548637,-9.7],[51.460417,-0.548638,-9.7],[51.460415,-0.54864,-9.6],[51.460414,-0.548643,-9.6],[51.460413,-0.548645,-9.6],[51.460413,-0.548646,-9.6],[51.460412,-0.548646,-9.6],[51.460411,-0.548646,-9.6],[51.460411,-0.548649,-9.6],[51.46041,-0.548652,-9.6],[51.46041,-0.548654,-9.5],[51.46041,-0.548656,-9.5],[51.460409,-0.548658,-9.5],[51.460409,-0.54866,-9.5],[51.460409,-0.548662,-9.5],[51.460408,-0.548665,-9.5],[51.460408,-0.548667,-9.5],[51.460407,-0.548668,-9.5],[51.460408,-0.548671,-9.5],[51.460408,-0.548675,-9.6],[51.460409,-0.548678,-9.5],[51.460409,-0.548681,-9.5],[51.460409,-0.548684,-9.6],[51.46041,-0.548687,-9.5],[51.460411,-0.54869,-9.6],[51.460411,-0.548693,-9.6],[51.460413,-0.548696,-9.6],[51.460414,-0.548699,-9.6],[51.460416,-0.5487,-9.6],[51.460417,-0.548702,-9.6],[51.460419,-0.548705,-9.6],[51.46042,-0.548708,-9.6],[51.460421,-0.54871,-9.7],[51.460423,-0.548711,-9.6],[51.460425,-0.548712,-9.6],[51.460428,-0.548712,-9.6],[51.46043,-0.548713,-9.6],[51.460432,-0.548713,-9.6],[51.460434,-0.548714,-9.6],[51.460437,-0.548716,-9.6],[51.460441,-0.548715,-9.6],[51.460443,-0.548713,-9.7],[51.460445,-0.548711,-9.6],[51.460447,-0.54871,-9.6],[51.460448,-0.548708,-9.7],[51.460449,-0.548706,-9.6] ] });
				GV_Draw_Track(t);
				
				// Track #7
				t = 7; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Lightning'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = '#e6b100'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = '#e6b100'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.46059,-0.548929,-5],[51.460592,-0.548929,-5.1],[51.460594,-0.548928,-5.1],[51.460596,-0.548927,-5.1],[51.460598,-0.548926,-5.1],[51.460599,-0.548924,-5.1],[51.4606,-0.548922,-5.1],[51.460602,-0.54892,-5.1],[51.460603,-0.548916,-5.1],[51.460604,-0.548914,-5.1],[51.460605,-0.548911,-5.1],[51.460605,-0.548908,-5.1],[51.460604,-0.548906,-5.1],[51.460603,-0.548904,-5.1],[51.460602,-0.548903,-5.1],[51.460601,-0.548901,-5.1],[51.4606,-0.548899,-5.1],[51.460599,-0.548897,-5.1],[51.460597,-0.548895,-5.1],[51.460596,-0.548893,-5.1],[51.460595,-0.54889,-5.1],[51.460594,-0.548888,-5.1],[51.460592,-0.548885,-5.1],[51.460591,-0.548883,-5.1],[51.46059,-0.548881,-5.1],[51.460587,-0.548875,-5.1],[51.460584,-0.54887,-5.1],[51.460583,-0.548869,-5.1],[51.460582,-0.548867,-5.1],[51.460581,-0.548866,-5.1],[51.46058,-0.548865,-5.1],[51.460577,-0.548863,-5.1],[51.460573,-0.54886,-5.1],[51.460572,-0.548859,-5],[51.460571,-0.548857,-5],[51.460569,-0.548856,-5],[51.460567,-0.548854,-5],[51.460565,-0.548853,-5],[51.460563,-0.548852,-5.1],[51.460561,-0.548851,-5],[51.460557,-0.548848,-5],[51.460555,-0.548849,-5.1],[51.460554,-0.548851,-5.1],[51.460552,-0.548853,-5.1],[51.46055,-0.548855,-5.1],[51.460549,-0.548857,-5.1],[51.460549,-0.548859,-5.1],[51.460548,-0.548861,-5.1],[51.460547,-0.548863,-5.1],[51.460547,-0.548865,-5.1],[51.460547,-0.548867,-5.1],[51.460546,-0.54887,-5.1],[51.460547,-0.548871,-5.1],[51.460548,-0.548874,-5.1],[51.460549,-0.548876,-5.2],[51.46055,-0.548878,-5.2],[51.460552,-0.54888,-5.2],[51.460552,-0.548882,-5.1],[51.460553,-0.548883,-5.1],[51.460554,-0.548885,-5.1],[51.460555,-0.548888,-5.1],[51.460556,-0.548891,-5.1],[51.460557,-0.548894,-5.1],[51.460558,-0.548895,-5.1],[51.46056,-0.548897,-5.1],[51.460561,-0.548899,-5.1],[51.460563,-0.548901,-5.1],[51.460565,-0.548902,-5],[51.460567,-0.548903,-5],[51.460569,-0.548905,-5],[51.460572,-0.548907,-5],[51.460575,-0.548908,-5],[51.460577,-0.548909,-5.1],[51.46058,-0.54891,-5.1],[51.460581,-0.54891,-5],[51.460582,-0.548909,-5.1],[51.460583,-0.54891,-5.1],[51.460584,-0.54891,-5.1],[51.460586,-0.548913,-5.1],[51.460587,-0.548915,-5.1],[51.46059,-0.548916,-5],[51.460591,-0.548915,-5.1],[51.460594,-0.548914,-5.1],[51.460595,-0.548914,-5.1],[51.460597,-0.548914,-5.1] ] });
				GV_Draw_Track(t);
				
				// Track #8
				t = 8; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'La Mouette'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = '#69e600'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = '#69e600'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.460749,-0.547686,-6.6],[51.460752,-0.547687,-6.7],[51.460754,-0.547688,-6.7],[51.460755,-0.54769,-6.7],[51.460756,-0.547691,-6.7],[51.460757,-0.547692,-6.7],[51.460757,-0.547694,-6.7],[51.460758,-0.547696,-6.7],[51.460758,-0.547698,-6.7],[51.460758,-0.5477,-6.7],[51.460757,-0.547702,-6.7],[51.460757,-0.547705,-6.7],[51.460756,-0.547707,-6.7],[51.460756,-0.547709,-6.6],[51.460755,-0.547711,-6.6],[51.460754,-0.547713,-6.6],[51.460753,-0.547716,-6.7],[51.460752,-0.547718,-6.7],[51.460751,-0.547721,-6.7],[51.46075,-0.547722,-6.7],[51.460749,-0.547724,-6.7],[51.460748,-0.547724,-6.7],[51.460747,-0.547726,-6.7],[51.460746,-0.547728,-6.7],[51.460744,-0.547729,-6.7],[51.460743,-0.547731,-6.7],[51.460742,-0.547732,-6.7],[51.460741,-0.547733,-6.7],[51.460739,-0.547735,-6.8],[51.460738,-0.547736,-6.7],[51.460732,-0.547741,-6.7],[51.46073,-0.547743,-6.9],[51.460728,-0.547744,-6.9],[51.460726,-0.547745,-6.9],[51.460725,-0.547747,-7],[51.460723,-0.547748,-7],[51.460722,-0.547748,-7.2],[51.460721,-0.547749,-7.2],[51.46072,-0.547749,-7.1],[51.460718,-0.547749,-7],[51.460717,-0.547748,-6.9],[51.460716,-0.547748,-6.9],[51.460716,-0.547747,-7],[51.460716,-0.547746,-7],[51.460715,-0.547745,-7],[51.460715,-0.547743,-7],[51.460714,-0.547742,-7],[51.460713,-0.547741,-7.1],[51.460712,-0.54774,-7.1],[51.460711,-0.547738,-7.2],[51.46071,-0.547737,-7.1],[51.46071,-0.547735,-7.1],[51.46071,-0.547733,-7.1],[51.46071,-0.547732,-7.1],[51.460711,-0.547731,-7],[51.460712,-0.54773,-7],[51.460713,-0.547728,-7],[51.460714,-0.547726,-6.8],[51.460713,-0.547725,-6.9],[51.460713,-0.547723,-6.9],[51.460715,-0.547723,-6.8],[51.460715,-0.547721,-6.7],[51.460717,-0.547719,-6.7],[51.460719,-0.547717,-6.8],[51.46072,-0.547716,-6.8],[51.460721,-0.547715,-6.7],[51.460723,-0.547713,-6.8],[51.460724,-0.547712,-6.8],[51.460724,-0.54771,-6.8],[51.460726,-0.547708,-6.8],[51.46073,-0.547702,-6.8],[51.460731,-0.547701,-6.8],[51.460733,-0.5477,-6.8],[51.460735,-0.547699,-6.7],[51.460736,-0.547697,-6.8],[51.460738,-0.547696,-6.8],[51.46074,-0.547694,-6.8],[51.460741,-0.547693,-6.8],[51.460742,-0.547693,-6.8],[51.460743,-0.547692,-6.8],[51.460744,-0.547691,-6.7],[51.460746,-0.547691,-6.7],[51.460747,-0.54769,-6.8],[51.460749,-0.54769,-6.8],[51.460751,-0.54769,-6.8],[51.460753,-0.54769,-6.8],[51.460755,-0.547691,-6.8],[51.460757,-0.547691,-6.8] ] });
				GV_Draw_Track(t);
				
				// Track #9
				t = 9; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Lifeboat'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = '#00e646'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = '#00e646'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.459855,-0.546851,-6.7],[51.459854,-0.54685,-6.8],[51.459851,-0.54685,-6.9],[51.45985,-0.546851,-6.9],[51.459849,-0.546852,-7],[51.459848,-0.546853,-6.9],[51.459847,-0.546853,-6.8],[51.459846,-0.546853,-6.8],[51.459844,-0.546853,-6.8],[51.459842,-0.546854,-6.8],[51.459841,-0.546856,-6.8],[51.45984,-0.546858,-6.8],[51.459838,-0.546861,-6.8],[51.459836,-0.546863,-6.8],[51.459835,-0.546864,-6.8],[51.459834,-0.546867,-6.8],[51.459833,-0.54687,-6.8],[51.459832,-0.546873,-6.8],[51.459832,-0.546877,-6.8],[51.459831,-0.546879,-6.8],[51.45983,-0.546881,-6.7],[51.459829,-0.546883,-6.7],[51.459829,-0.546884,-6.7],[51.459828,-0.546886,-6.6],[51.459828,-0.546887,-6.6],[51.459827,-0.546888,-6.6],[51.459826,-0.54689,-6.6],[51.459825,-0.546892,-6.6],[51.459824,-0.546894,-6.6],[51.459823,-0.546897,-6.5],[51.459823,-0.546899,-6.6],[51.459822,-0.546903,-6.6],[51.45982,-0.546906,-6.6],[51.45982,-0.546908,-6.6],[51.459819,-0.54691,-6.5],[51.459819,-0.546913,-6.5],[51.459818,-0.546916,-6.5],[51.459817,-0.54692,-6.6],[51.459816,-0.546922,-6.6],[51.459816,-0.546925,-6.6],[51.459814,-0.546934,-6.6],[51.459814,-0.546937,-6.6],[51.459814,-0.546941,-6.6],[51.459813,-0.546944,-6.5],[51.459812,-0.546946,-6.5],[51.459813,-0.546949,-6.6],[51.459813,-0.546952,-6.6],[51.459813,-0.546955,-6.6],[51.459813,-0.546958,-6.6],[51.459813,-0.546961,-6.6],[51.459813,-0.546965,-6.6],[51.459813,-0.546969,-6.6],[51.459813,-0.546973,-6.7],[51.459813,-0.546976,-6.6],[51.459813,-0.546979,-6.7],[51.459814,-0.546982,-6.7],[51.459814,-0.546986,-6.7],[51.459815,-0.546989,-6.7],[51.459816,-0.546991,-6.7],[51.459818,-0.546993,-6.8],[51.459819,-0.546993,-6.8],[51.45982,-0.546993,-6.8],[51.459821,-0.546992,-6.8],[51.459821,-0.546991,-6.8],[51.459822,-0.54699,-6.8],[51.459823,-0.54699,-6.8],[51.459823,-0.546989,-6.7],[51.459823,-0.546988,-6.7],[51.459824,-0.546988,-6.7],[51.459824,-0.546987,-6.7],[51.459825,-0.546986,-6.6],[51.459824,-0.546985,-6.5],[51.459824,-0.546986,-6.4],[51.459825,-0.546986,-6.5],[51.459825,-0.546985,-6.4],[51.459825,-0.546984,-6.2],[51.459825,-0.546983,-6.2],[51.459825,-0.546982,-6.3],[51.459825,-0.546981,-6.4],[51.459826,-0.546982,-6.4],[51.45983,-0.546986,-6.5],[51.45983,-0.546987,-6.4],[51.459828,-0.546983,-6.2],[51.459827,-0.546983,-6.2],[51.459826,-0.54698,-6.1],[51.459825,-0.54698,-6],[51.459826,-0.54698,-6.2],[51.459827,-0.54698,-6.7],[51.459829,-0.546977,-6.7],[51.45984,-0.546965,-6.6],[51.459844,-0.546958,-6.9],[51.459845,-0.546954,-7],[51.459846,-0.546951,-7],[51.459848,-0.546947,-7],[51.459849,-0.546942,-6.9],[51.45985,-0.546939,-6.8],[51.459851,-0.546935,-6.9],[51.459851,-0.546932,-6.8],[51.459852,-0.546928,-6.8],[51.459852,-0.546924,-6.8],[51.459853,-0.54692,-6.8],[51.459853,-0.546917,-6.8],[51.459853,-0.546913,-6.8],[51.459854,-0.54691,-6.8],[51.459854,-0.546907,-6.8],[51.459855,-0.546904,-6.9],[51.459856,-0.546895,-6.9],[51.459854,-0.546893,-6.9],[51.459853,-0.546887,-6.9],[51.459854,-0.546883,-7],[51.459853,-0.546878,-6.9],[51.459853,-0.546875,-6.9],[51.45986,-0.546861,-7],[51.459862,-0.546857,-7],[51.45986,-0.546853,-7],[51.459859,-0.546851,-6.9],[51.459859,-0.546849,-6.9],[51.459858,-0.546849,-6.9],[51.459857,-0.546848,-6.9],[51.459855,-0.546847,-6.9],[51.459853,-0.546846,-7],[51.459852,-0.546845,-7],[51.459851,-0.546845,-6.9],[51.45985,-0.546845,-6.9],[51.459849,-0.546846,-6.9],[51.45985,-0.546835,-6.9],[51.459847,-0.546839,-6.7],[51.459848,-0.546842,-6.3],[51.459847,-0.546844,-6.3],[51.459847,-0.546847,-6.2],[51.459847,-0.546848,-6.1],[51.459847,-0.54685,-6.1],[51.459846,-0.546851,-6.1],[51.459846,-0.546853,-6.1],[51.459847,-0.546856,-6.1],[51.459847,-0.546862,-5.9],[51.459848,-0.546864,-5.9],[51.459848,-0.546865,-5.8],[51.459849,-0.546868,-5.9],[51.459849,-0.54687,-5.8],[51.459849,-0.546872,-5.8],[51.459849,-0.546874,-5.8],[51.45985,-0.546875,-5.7],[51.45985,-0.546876,-5.6],[51.459851,-0.546878,-5.6],[51.459851,-0.546879,-5.6],[51.459851,-0.54688,-5.6],[51.45985,-0.546881,-5.7],[51.45985,-0.546883,-5.7],[51.459849,-0.546884,-5.7],[51.459849,-0.546886,-5.8],[51.459848,-0.546888,-5.8],[51.459848,-0.54689,-5.9],[51.459848,-0.546893,-5.8],[51.459848,-0.546895,-5.8],[51.459848,-0.546897,-5.7],[51.459848,-0.5469,-5.8],[51.459848,-0.546903,-5.8],[51.459848,-0.546906,-5.8],[51.459848,-0.54691,-5.8],[51.459848,-0.546914,-5.8],[51.459848,-0.546917,-5.8],[51.459848,-0.546919,-5.8],[51.459848,-0.546921,-5.8],[51.459848,-0.546923,-5.7],[51.459848,-0.546924,-5.7],[51.459848,-0.546925,-5.6],[51.459848,-0.546927,-5.7],[51.459848,-0.546928,-5.7],[51.459848,-0.546929,-5.6],[51.459848,-0.546932,-5.6],[51.459848,-0.546936,-5.7],[51.459848,-0.546939,-5.7],[51.459848,-0.546943,-5.7],[51.459849,-0.546946,-5.7],[51.45985,-0.546948,-5.6],[51.459851,-0.546952,-5.6],[51.459852,-0.546956,-5.6],[51.459853,-0.546959,-5.6],[51.459854,-0.546961,-5.6],[51.459854,-0.546963,-5.5],[51.459855,-0.546964,-5.4] ] });
				GV_Draw_Track(t);
				
				// Track #10
				t = 10; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Boat In a Hole'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = '#00d4e6'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = '#00d4e6'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.459988,-0.547562,0],[51.459992,-0.54756,0],[51.459994,-0.547557,0],[51.459992,-0.547555,0],[51.45999,-0.547552,0],[51.459994,-0.547552,0],[51.459991,-0.547548,0],[51.459989,-0.547545,0],[51.459988,-0.547542,0],[51.459987,-0.54754,0],[51.459985,-0.547539,0],[51.459984,-0.547537,0],[51.459983,-0.547538,0],[51.459982,-0.547536,0],[51.459981,-0.547536,0],[51.459979,-0.547536,0],[51.459977,-0.547536,0],[51.459975,-0.54754,0],[51.459974,-0.54754,0],[51.459972,-0.54754,0],[51.459971,-0.54754,0],[51.459969,-0.547545,0],[51.459967,-0.547543,0],[51.459956,-0.547547,0],[51.459957,-0.54755,0],[51.459954,-0.547552,0],[51.459932,-0.547542,0],[51.459932,-0.547547,0],[51.459934,-0.547554,0],[51.459929,-0.547563,0],[51.45993,-0.547567,0],[51.459933,-0.547578,0],[51.459935,-0.547581,0],[51.459937,-0.547584,0],[51.459937,-0.547586,0],[51.459938,-0.547589,0],[51.459939,-0.547592,0],[51.45994,-0.547595,0],[51.45994,-0.547597,0],[51.459941,-0.547603,0],[51.459943,-0.547606,0],[51.459942,-0.547607,0],[51.459944,-0.547608,0],[51.459946,-0.54761,0],[51.459948,-0.54761,0],[51.45995,-0.547611,0],[51.459952,-0.547611,0],[51.459954,-0.54761,0],[51.459956,-0.547609,0],[51.459958,-0.547608,0],[51.459959,-0.547607,0],[51.45996,-0.547606,0],[51.459963,-0.547604,0],[51.459964,-0.547603,0],[51.459965,-0.547602,0],[51.459967,-0.5476,0],[51.459968,-0.547598,0],[51.45997,-0.547595,0],[51.459971,-0.547592,0],[51.459972,-0.54759,0],[51.459973,-0.547588,0],[51.459974,-0.547587,0],[51.459975,-0.547585,0],[51.459976,-0.547584,0],[51.459977,-0.547582,0],[51.459977,-0.547581,0],[51.459978,-0.54758,0],[51.459978,-0.547578,0],[51.45998,-0.547576,0],[51.459981,-0.547574,0],[51.459981,-0.547572,0],[51.459982,-0.547569,0],[51.459983,-0.547566,0],[51.459983,-0.547564,0],[51.459984,-0.547561,0],[51.459984,-0.547557,0],[51.459986,-0.547559,0],[51.459985,-0.547557,0],[51.459982,-0.547554,0],[51.459981,-0.547551,0],[51.459981,-0.54755,0],[51.459978,-0.54755,0],[51.459977,-0.547548,0],[51.459976,-0.547546,0],[51.459976,-0.547545,0],[51.459976,-0.547544,0],[51.459976,-0.547543,0],[51.459975,-0.547542,0],[51.459975,-0.547541,0],[51.459974,-0.547541,0],[51.459973,-0.54754,0],[51.459972,-0.54754,0],[51.45997,-0.54754,0],[51.459969,-0.547539,0],[51.459968,-0.547538,0],[51.459967,-0.547537,0],[51.459965,-0.547536,0],[51.459964,-0.547535,0],[51.459963,-0.547533,0],[51.459962,-0.547532,0],[51.459961,-0.54753,0],[51.459959,-0.547529,0],[51.459958,-0.547528,0],[51.459956,-0.547526,0],[51.459955,-0.547524,0],[51.459955,-0.547523,0],[51.459953,-0.547521,0],[51.459952,-0.547519,0],[51.459951,-0.547517,0],[51.45995,-0.547515,0],[51.459948,-0.547513,0],[51.459947,-0.547511,0],[51.459945,-0.547508,0],[51.459943,-0.547506,0],[51.459942,-0.547503,0],[51.459941,-0.5475,0],[51.45994,-0.547498,0],[51.459938,-0.547495,0],[51.459938,-0.547492,0],[51.459937,-0.54749,0],[51.459936,-0.547487,0],[51.459936,-0.547484,0],[51.459935,-0.547482,0],[51.459934,-0.547479,0],[51.459933,-0.547476,0],[51.459933,-0.547473,0],[51.459932,-0.54747,0],[51.459932,-0.547467,0],[51.459931,-0.547464,0],[51.45993,-0.547461,0],[51.459927,-0.547458,0],[51.459925,-0.547457,0] ] });
				GV_Draw_Track(t);
				
				// Track #11
				t = 11; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Tin Boat'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = '#0023e6'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = '#0023e6'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.459714,-0.546822,-7.7],[51.459713,-0.54682,-7.7],[51.459712,-0.546817,-7.7],[51.459711,-0.546815,-7.7],[51.45971,-0.546814,-7.7],[51.45971,-0.546812,-7.7],[51.459709,-0.54681,-7.7],[51.459709,-0.546808,-7.7],[51.45971,-0.546801,-7.7],[51.45971,-0.546799,-7.7],[51.45971,-0.546797,-7.7],[51.45971,-0.546794,-7.7],[51.45971,-0.546792,-7.7],[51.459707,-0.546786,-7.7],[51.459706,-0.546785,-7.7],[51.459705,-0.546784,-7.6],[51.459704,-0.546783,-7.6],[51.459703,-0.546782,-7.5],[51.459701,-0.546781,-7.4],[51.4597,-0.54678,-7.3],[51.459699,-0.546779,-7.3],[51.459697,-0.546778,-7.3],[51.459696,-0.546777,-7.2],[51.459695,-0.546776,-7.3],[51.459693,-0.546775,-7.3],[51.459692,-0.546776,-7.3],[51.45969,-0.546777,-7.4],[51.459689,-0.546779,-7.3],[51.459688,-0.54678,-7.3],[51.459686,-0.546782,-7.4],[51.459685,-0.546785,-7.4],[51.459684,-0.546787,-7.4],[51.459683,-0.546789,-7.4],[51.459682,-0.546791,-7.4],[51.45968,-0.546794,-7.3],[51.45968,-0.546796,-7.3],[51.459679,-0.546798,-7.3],[51.459678,-0.546801,-7.3],[51.459677,-0.546803,-7.3],[51.459677,-0.546805,-7.3],[51.459675,-0.546808,-7.3],[51.459674,-0.546811,-7.3],[51.459674,-0.546814,-7.3],[51.459673,-0.546817,-7.3],[51.459672,-0.54682,-7.3],[51.459671,-0.546822,-7.3],[51.45967,-0.546825,-7.3],[51.459669,-0.546829,-7.4],[51.459668,-0.546832,-7.4],[51.459667,-0.546836,-7.4],[51.459667,-0.546839,-7.4],[51.459666,-0.546843,-7.4],[51.459666,-0.546847,-7.4],[51.459665,-0.546851,-7.4],[51.459665,-0.546853,-7.4],[51.459665,-0.546855,-7.4],[51.459665,-0.546861,-7.4],[51.459666,-0.546864,-7.4],[51.459666,-0.546867,-7.4],[51.459666,-0.546869,-7.4],[51.459666,-0.546872,-7.4],[51.459667,-0.546875,-7.4],[51.459668,-0.546878,-7.4],[51.459668,-0.54688,-7.4],[51.459669,-0.546882,-7.4],[51.45967,-0.546884,-7.4],[51.459671,-0.546886,-7.4],[51.459672,-0.546887,-7.4],[51.459674,-0.546888,-7.4],[51.459677,-0.546888,-7.4],[51.459679,-0.546888,-7.4],[51.45968,-0.546888,-7.4],[51.459682,-0.546888,-7.4],[51.459684,-0.546886,-7.4],[51.459685,-0.546884,-7.4],[51.459687,-0.546883,-7.4],[51.459688,-0.54688,-7.4],[51.45969,-0.546878,-7.4],[51.459691,-0.546876,-7.4],[51.459693,-0.546874,-7.4],[51.459694,-0.546872,-7.3],[51.459695,-0.546869,-7.3],[51.459696,-0.546867,-7.3],[51.459696,-0.546863,-7.3],[51.459697,-0.54686,-7.3],[51.459698,-0.546857,-7.3],[51.459699,-0.546854,-7.3],[51.459699,-0.54685,-7.2],[51.4597,-0.546847,-7.3],[51.4597,-0.546842,-7.3],[51.459701,-0.546839,-7.2],[51.459701,-0.546835,-7.2],[51.459702,-0.546831,-7.2],[51.459702,-0.546827,-7.2],[51.459703,-0.546824,-7.2],[51.459703,-0.54682,-7.2],[51.459705,-0.546813,-7.2],[51.459705,-0.54681,-7.2],[51.459705,-0.546807,-7.3],[51.459705,-0.546804,-7.3],[51.459705,-0.546801,-7.3] ] });
				GV_Draw_Track(t);
				
				// Track #12
				t = 12; trk[t] = {info:[],segments:[]};
				trk[t].info.name = 'Claymore'; trk[t].info.desc = ''; trk[t].info.clickable = true;
				trk[t].info.color = '#8e00e6'; trk[t].info.width = 3; trk[t].info.opacity = 0.9; trk[t].info.hidden = false; trk[t].info.z_index = null;
				trk[t].info.outline_color = 'black'; trk[t].info.outline_width = 0; trk[t].info.fill_color = '#8e00e6'; trk[t].info.fill_opacity = 0;
				trk[t].info.elevation = true;
				trk[t].segments.push({ points:[ [51.459658,-0.546421,-7.6],[51.459657,-0.54642,-7.6],[51.459656,-0.546418,-7.6],[51.459655,-0.546416,-7.7],[51.459654,-0.546413,-7.7],[51.459654,-0.546411,-7.7],[51.459654,-0.546408,-7.7],[51.459653,-0.546405,-7.7],[51.459653,-0.546403,-7.7],[51.459653,-0.5464,-7.7],[51.459653,-0.546399,-7.7],[51.459653,-0.546397,-7.7],[51.459653,-0.546395,-7.7],[51.459653,-0.546394,-7.7],[51.459652,-0.546392,-7.7],[51.459652,-0.54639,-7.7],[51.459651,-0.546388,-7.7],[51.45965,-0.546386,-7.7],[51.459649,-0.546385,-7.7],[51.459648,-0.546384,-7.7],[51.459646,-0.546383,-7.7],[51.459645,-0.546382,-7.7],[51.459643,-0.546382,-7.7],[51.459641,-0.546381,-7.7],[51.459639,-0.546381,-7.7],[51.459638,-0.546381,-7.7],[51.459636,-0.546381,-7.7],[51.459634,-0.546382,-7.7],[51.459633,-0.546383,-7.7],[51.459632,-0.546385,-7.7],[51.459631,-0.546388,-7.7],[51.459631,-0.54639,-7.7],[51.45963,-0.546392,-7.7],[51.459629,-0.546395,-7.7],[51.459628,-0.546397,-7.6],[51.459627,-0.5464,-7.6],[51.459626,-0.546402,-7.6],[51.459625,-0.546404,-7.6],[51.459625,-0.546407,-7.6],[51.459624,-0.54641,-7.6],[51.459624,-0.546413,-7.6],[51.459623,-0.546417,-7.6],[51.459623,-0.54642,-7.6],[51.459623,-0.546423,-7.6],[51.459622,-0.546427,-7.6],[51.459622,-0.54643,-7.6],[51.459622,-0.546433,-7.6],[51.459622,-0.546436,-7.6],[51.459622,-0.546439,-7.6],[51.459622,-0.546443,-7.6],[51.459622,-0.546446,-7.6],[51.459622,-0.54645,-7.6],[51.459622,-0.546454,-7.6],[51.459623,-0.546457,-7.6],[51.459623,-0.546461,-7.6],[51.459624,-0.546464,-7.6],[51.459625,-0.546467,-7.5],[51.459626,-0.546469,-7.5],[51.459627,-0.546472,-7.5],[51.459627,-0.546474,-7.5],[51.459628,-0.546475,-7.5],[51.459628,-0.546477,-7.4],[51.459629,-0.546478,-7.5],[51.459631,-0.546479,-7.5],[51.459632,-0.54648,-7.5],[51.459634,-0.54648,-7.5],[51.459637,-0.546479,-7.5],[51.459639,-0.546476,-7.5],[51.45964,-0.546473,-7.5],[51.459642,-0.546472,-7.5],[51.459643,-0.54647,-7.5],[51.459645,-0.546468,-7.4],[51.459646,-0.546465,-7.5],[51.459646,-0.546462,-7.4],[51.459647,-0.546458,-7.5],[51.459648,-0.546454,-7.5],[51.459649,-0.54645,-7.4],[51.459649,-0.546447,-7.4],[51.459649,-0.546443,-7.4],[51.45965,-0.546439,-7.4],[51.45965,-0.546435,-7.4],[51.459651,-0.546431,-7.5],[51.459652,-0.546427,-7.5],[51.459652,-0.546424,-7.5],[51.459651,-0.546413,-7.5] ] });
				GV_Draw_Track(t);
                
                
                
                
				
				t = 1; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 2; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 3; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 4; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 5; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 6; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 7; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 8; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 9; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 10; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 11; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});
				t = 12; GV_Add_Track_to_Tracklist({bullet:'- ',name:trk[t].info.name,desc:trk[t].info.desc,color:trk[t].info.color,number:t});




				
				GV_Draw_Marker({lat:51.4620417,lon:-0.5489747,name:'Canoe 3m',desc:'01N Canoe 3m',label:'Canoe 3m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4609043,lon:-0.5492113,name:'Sub 4m',desc:'02N Sub 4m',label:'Sub 4m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4603470,lon:-0.5489195,name:'Scimitar Car 5.5m',desc:'03N Scimitar Car 5.5m',label:'Scimitar Car 5.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4601029,lon:-0.5488384,name:'Spitfire Car 6m',desc:'04N Spitfire Car 6m',label:'Spitfire Car 6m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4605855,lon:-0.5489017,name:'The Lightning Boat 5.5m',desc:'05N The Lightning Boat 5.5m',label:'The Lightning Boat 5.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4608746,lon:-0.5487572,name:'Caves Centre',desc:'06aN Caves Centre',label:'Caves Centre',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4608170,lon:-0.5487340,name:'Caves Lion Entrance Caves',desc:'06bN Caves Lion Entrance Caves',label:'Caves Lion Entrance Caves',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4608980,lon:-0.5487013,name:'Red Isis Bike @ Cave',desc:'06cN Red Isis Bike @ Cave',label:'Red Isis Bike @ Cave',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4608584,lon:-0.5487363,name:'Blue Raleigh Bike @ Caves',desc:'06dN Blue Raleigh Bike @ Caves',label:'Blue Raleigh Bike @ Caves',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4609369,lon:-0.5487749,name:'Cave PC Laptop',desc:'06eN Cave PC Laptop',label:'Cave PC Laptop',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4600140,lon:-0.5487350,name:'Cargo 2.5m',desc:'07P Cargo 2.5m',label:'Cargo 2.5m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4604302,lon:-0.5486882,name:'The Hole',desc:'08B The Hole',label:'The Hole',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4601540,lon:-0.5486870,name:'Dance Platform 6m',desc:'09P Dance Platform 6m',label:'Dance Platform 6m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4600730,lon:-0.5485150,name:'Bus 2m',desc:'10N Bus 2m',label:'Bus 2m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4599718,lon:-0.5485828,name:'Confined Area',desc:'11N Confined Area',label:'Confined Area',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4613356,lon:-0.5484697,name:'Commer Van 6m',desc:'12N Commer Van 6m',label:'Commer Van 6m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4605198,lon:-0.5484217,name:'White Boat 7m',desc:'13B White Boat 7m',label:'White Boat 7m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4602986,lon:-0.5483127,name:'Cargo 8m',desc:'14P Cargo 8m',label:'Cargo 8m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4601920,lon:-0.5482830,name:'Cargo Rusty 8m',desc:'15P Cargo Rusty 8m',label:'Cargo Rusty 8m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4603400,lon:-0.5481730,name:'Portacabin 8m',desc:'16P Portacabin 8m',label:'Portacabin 8m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4599705,lon:-0.5481082,name:'Shallow Platform 2m',desc:'17P Shallow Platform 2m',label:'Shallow Platform 2m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4601746,lon:-0.5480586,name:'Milk Float 6.5m',desc:'18N Milk Float 6.5m',label:'Milk Float 6.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4604027,lon:-0.5480400,name:'Chicken Hutch Boat 6.5m',desc:'19N Chicken Hutch Boat 6.5m',label:'Chicken Hutch Boat 6.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4600385,lon:-0.5478824,name:'Skittles Sweet Bowl 5.5m',desc:'20N Skittles Sweet Bowl 5.5m',label:'Skittles Sweet Bowl 5.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4602514,lon:-0.5478916,name:'Sticky Up Boat 5m',desc:'21B Sticky Up Boat 5m',label:'Sticky Up Boat 5m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4599186,lon:-0.5476810,name:'Lady of Kent Search Light 5m',desc:'22B Lady of Kent Search Light 5m',label:'Lady of Kent Search Light 5m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4600559,lon:-0.5476773,name:'Traffic Lights 7m',desc:'23N Traffic Lights 7m',label:'Traffic Lights 7m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4607730,lon:-0.5476209,name:'Half Die Hard Taxi 8m',desc:'24N Half Die Hard Taxi 8m',label:'Half Die Hard Taxi 8m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4599545,lon:-0.5475547,name:'Boat In A Hole 7m',desc:'25N Boat In A Hole 7m',label:'Boat In A Hole 7m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4595937,lon:-0.5474898,name:'Iron Fish 2m',desc:'26N Iron Fish 2m',label:'Iron Fish 2m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4604301,lon:-0.5473834,name:'Wreck Site 6m',desc:'27aB Wreck Site 6m',label:'Wreck Site 6m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4601316,lon:-0.5474179,name:'Dive/Spike Boat 7m',desc:'29B Dive/Spike Boat 7m',label:'Dive/Spike Boat 7m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4598131,lon:-0.5473803,name:'White Day boat by platform 6m',desc:'30B White Day boat by platform 6m',label:'White Day boat by platform 6m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4597660,lon:-0.5473470,name:'6m',desc:'31P 6m',label:'6m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4596580,lon:-0.5472500,name:'6m',desc:'32P 6m',label:'6m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4595563,lon:-0.5472633,name:'Port Holes Boat 4.5m',desc:'33N Port Holes Boat 4.5m',label:'Port Holes Boat 4.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4603120,lon:-0.5471650,name:'6m',desc:'34P 6m',label:'6m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4599637,lon:-0.5471543,name:'Dragon Boat 7.5m',desc:'35N Dragon Boat 7.5m',label:'Dragon Boat 7.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4595550,lon:-0.5470800,name:'6m',desc:'36P 6m',label:'6m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4594757,lon:-0.5470871,name:'Dive Bell 4m',desc:'37N Dive Bell 4m',label:'Dive Bell 4m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4598394,lon:-0.5469307,name:'Lifeboat 6.5m',desc:'38B Lifeboat 6.5m',label:'Lifeboat 6.5m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4597290,lon:-0.5469929,name:'London Black Cab 7m',desc:'39N London Black Cab 7m',label:'London Black Cab 7m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4602360,lon:-0.5468476,name:'RIB Boat 6m',desc:'40N RIB Boat 6m',label:'RIB Boat 6m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4596766,lon:-0.5468125,name:'Tin/Cabin Boat 7m',desc:'41N Tin/Cabin Boat 7m',label:'Tin/Cabin Boat 7m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4594910,lon:-0.5468670,name:'6m',desc:'42P 6m',label:'6m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4602073,lon:-0.5467877,name:'Thorpe Orange Boat 5.5m',desc:'43N Thorpe Orange Boat 5.5m',label:'Thorpe Orange Boat 5.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4593680,lon:-0.5467601,name:'VW Camper Van and Seahorse 5.5m',desc:'44N VW Camper Van and Seahorse 5.5m',label:'VW Camper Van and Seahorse 5.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4598099,lon:-0.5467037,name:'Listing Sharon 7.5m',desc:'45B Listing Sharon 7.5m',label:'Listing Sharon 7.5m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4597450,lon:-0.5466490,name:'Plane 6m',desc:'46N Plane 6m',label:'Plane 6m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4593990,lon:-0.5465940,name:'6m',desc:'47P 6m',label:'6m',label_color:'blue',color:'white',icon:'square',folder:'Platform'});
				GV_Draw_Marker({lat:51.4594384,lon:-0.5465238,name:'Holey Ship 4.5m',desc:'48N Holey Ship 4.5m',label:'Holey Ship 4.5m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4596344,lon:-0.5464664,name:'Claymore 6.5m',desc:'49B Claymore 6.5m',label:'Claymore 6.5m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4591437,lon:-0.5460323,name:'Swim Through - no crates 6m',desc:'50aN Swim Through - no crates 6m',label:'Swim Through - no crates 6m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4591694,lon:-0.5459990,name:'Swim Through - mid 6m',desc:'50bN Swim Through - mid 6m',label:'Swim Through - mid 6m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4592045,lon:-0.5459126,name:'Swim Through - crates 6m',desc:'50cN Swim Through - crates 6m',label:'Swim Through - crates 6m',label_color:'blue',color:'magenta',icon:'cross',folder:'No Buoy'});
				GV_Draw_Marker({lat:51.4591431,lon:-0.5459369,name:'Orca Van 5.5m',desc:'51B Orca Van 5.5m',label:'Orca Van 5.5m',label_color:'blue',color:'blue',icon:'circle',folder:'Buoy'});
				GV_Draw_Marker({lat:51.4601285,lon:-0.5488505,name:'Dinghy Boat',desc:'X01 Dinghy Boat',label:'Dinghy Boat',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4604340,lon:-0.5489210,name:'Quarry Machine in Reeds',desc:'X02 Quarry Machine in Reeds',label:'Quarry Machine in Reeds',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4599583,lon:-0.5476486,name:'Metal Grated Box',desc:'X03 Metal Grated Box',label:'Metal Grated Box',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4599019,lon:-0.5471413,name:'4 crates in a line',desc:'X04 4 crates in a line',label:'4 crates in a line',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4598468,lon:-0.5472127,name:'Lone crate',desc:'X05 Lone crate',label:'Lone crate',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4596708,lon:-0.5472531,name:'Collapsed Metal',desc:'X06 Collapsed Metal',label:'Collapsed Metal',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4600386,lon:-0.5487241,name:'Boat with Chain Links',desc:'X07 Boat with Chain Links',label:'Boat with Chain Links',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4599406,lon:-0.5485203,name:'Pot in a box',desc:'X08 Pot in a box',label:'Pot in a box',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4600703,lon:-0.5486457,name:'Seahorse Mid-Water',desc:'X09 Seahorse Mid-Water',label:'Seahorse Mid-Water',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4600603,lon:-0.5486717,name:'Headless Nick',desc:'X10 Headless Nick',label:'Headless Nick',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4600452,lon:-0.5488188,name:'Headless Tom Reeds',desc:'X11 Headless Tom Reeds',label:'Headless Tom Reeds',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4602002,lon:-0.5478815,name:'Cement Mixer',desc:'X12 Cement Mixer',label:'Cement Mixer',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4600531,lon:-0.5481839,name:'Tyre',desc:'X13 Tyre',label:'Tyre',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4595778,lon:-0.5473580,name:'Roadworks Sign',desc:'X14 Roadworks Sign',label:'Roadworks Sign',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4599975,lon:-0.5481015,name:'Fireworks Launcher',desc:'X15 Fireworks Launcher',label:'Fireworks Launcher',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4593265,lon:-0.5469361,name:'2 Buried Boats in Reeds',desc:'X16 2 Buried Boats in Reeds',label:'2 Buried Boats in Reeds',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4596635,lon:-0.5470603,name:'Half Buried Solo Boat',desc:'X17 Half Buried Solo Boat',label:'Half Buried Solo Boat',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4600594,lon:-0.5475755,name:'Half Buried Bike',desc:'X18 Half Buried Bike',label:'Half Buried Bike',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4599240,lon:-0.5476152,name:'Desk with Keyboard',desc:'X19 Desk with Keyboard',label:'Desk with Keyboard',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4607400,lon:-0.5477130,name:'La Mouette Boat',desc:'X20 La Mouette Boat',label:'La Mouette Boat',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4609930,lon:-0.5480060,name:'Memorial Stone - Kit 7.5m',desc:'X21 Memorial Stone - Kit 7.5m',label:'Memorial Stone - Kit 7.5m',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4593530,lon:-0.5469390,name:'Fruit Machine 5.5m',desc:'X22 Fruit Machine 5.5m',label:'Fruit Machine 5.5m',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4612490,lon:-0.5486880,name:'Lone Crate 7m',desc:'X23 Lone Crate 7m',label:'Lone Crate 7m',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4597840,lon:-0.5465500,name:'X24 ? near plane 6m',desc:'X24 X24 ? near plane 6m',label:'X24 ? near plane 6m',label_color:'blue',color:'pink',icon:'star',folder:'Check'});
				GV_Draw_Marker({lat:51.4623163,lon:-0.5494161,name:'Cotton Reel 2m',desc:'X25 Cotton Reel 2m',label:'Cotton Reel 2m',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4600631,lon:-0.5480722,name:'Dumpy Cylinder 6m',desc:'X26 Dumpy Cylinder 6m',label:'Dumpy Cylinder 6m',label_color:'blue',color:'yellow',icon:'triangle',folder:'Unmarked'});
				GV_Draw_Marker({lat:51.4600150,lon:-0.5483160,name:'Cafe Jetty',desc:'Z01 Cafe Jetty',label:'Cafe Jetty',label_color:'blue',color:'green',icon:'diamond',folder:'Jetty'});
				GV_Draw_Marker({lat:51.4595470,lon:-0.5474610,name:'Mid Jetty',desc:'Z02 Mid Jetty',label:'Mid Jetty',label_color:'blue',color:'green',icon:'diamond',folder:'Jetty'});
				GV_Draw_Marker({lat:51.4591660,lon:-0.5469993,name:'Old Jetty',desc:'Z03 Old Jetty',label:'Old Jetty',label_color:'blue',color:'green',icon:'diamond',folder:'Jetty'});
				
				GV_Finish_Map();
					
			}
			GV_Map(); // execute the above code
			// https://www.gpsvisualizer.com/map_input?allow_export=1&bg_map=GV_OSM&cumulative_distance=1&form=google&format=google&marker_list_options:folders_collapsed=1&units=metric&width=1000&wpt_list=desc_border
		</script>
		
		
		
	</body>

</html>)rawliteral";

const uint32_t MAP_HTML_SIZE = sizeof(MAP_HTML);

#endif
