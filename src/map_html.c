#include <stdint.h>

#ifndef MAP_HTML_C
#define MAP_HTML_C

const char MAP_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html xmlns="https://www.w3.org/1999/xhtml">
	<head>
		<title>GPS data</title>
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
				
			<div id="gv_infobox" class="gv_infobox" style="font:11px Arial; border:solid #666666 1px; background-color:#ffffff; padding:4px; overflow:auto; display:none; max-width:400px;">
				<!-- Although GPS Visualizer didn't create an legend/info box with your map, you can use this space for something else if you'd like; enable it by setting gv_options.infobox_options.enabled to true -->
			</div>



			<div id="gv_marker_list" class="gv_marker_list" style="background-color:#ffffff; overflow:auto; display:none;"><!-- --></div>

			<div id="gv_clear_margins" style="height:0px; clear:both;"><!-- clear the "float" --></div>
		</div>

		
		<!-- begin GPS Visualizer setup script (must come after loading of API code) -->
		<script type="text/javascript">
			/* Global variables used by the GPS Visualizer functions (20240902164052): */
			gv_options = {};
			
			// basic map parameters:
			gv_options.center = [51.4607297214285,-0.5476643625];  // [latitude,longitude] - be sure to keep the square brackets
			gv_options.zoom = 17;  // higher number means closer view; can also be 'auto' for automatic zoom/center based on map elements
			gv_options.map_type = 'GV_OSM_RELIEF';  // popular map_type choices are 'GV_STREET', 'GV_SATELLITE', 'GV_HYBRID', 'GV_TERRAIN', 'GV_OSM', 'GV_TOPO_US', 'GV_TOPO_WORLD' (https://www.gpsvisualizer.com/misc/google_map_types.html)
			gv_options.map_opacity = 1.00;  // number from 0 to 1
			gv_options.full_screen = true;  // true|false: should the map fill the entire page (or frame)?
			gv_options.width = 700;  // width of the map, in pixels
			gv_options.height = 700;  // height of the map, in pixels
			
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
			gv_options.utilities_menu = { 'maptype':true, 'opacity':true, 'measure':true, 'geolocate':true, 'profile':false };
			gv_options.allow_export = false;  // true|false
			
			gv_options.infobox_options = {}; // options for a floating info box (id="gv_infobox"), which can contain anything
			  gv_options.infobox_options.enabled = true;  // true|false: enable or disable the info box altogether
			  gv_options.infobox_options.position = ['LEFT_TOP',52,4];  // [Google anchor name, relative x, relative y]
			  gv_options.infobox_options.draggable = true;  // true|false: can it be moved around the screen?
			  gv_options.infobox_options.collapsible = true;  // true|false: can it be collapsed by double-clicking its top bar?

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
			  gv_options.marker_list_options.enabled = false;  // true|false: enable or disable the marker list altogether
			  gv_options.marker_list_options.floating = true;  // is the list a floating box inside the map itself?
			  gv_options.marker_list_options.position = ['RIGHT_BOTTOM',6,38];  // floating list only: position within map
			  gv_options.marker_list_options.min_width = 160; // minimum width, in pixels, of the floating list
			  gv_options.marker_list_options.max_width = 160;  // maximum width
			  gv_options.marker_list_options.min_height = 0;  // minimum height, in pixels, of the floating list
			  gv_options.marker_list_options.max_height = 310;  // maximum height
			  gv_options.marker_list_options.draggable = true;  // true|false, floating list only: can it be moved around the screen?
			  gv_options.marker_list_options.collapsible = true;  // true|false, floating list only: can it be collapsed by double-clicking its top bar?
			  gv_options.marker_list_options.include_tickmarks = false;  // true|false: are distance/time tickmarks included in the list?
			  gv_options.marker_list_options.include_trackpoints = false;  // true|false: are "trackpoint" markers included in the list?
			  gv_options.marker_list_options.dividers = false;  // true|false: will a thin line be drawn between each item in the list?
			  gv_options.marker_list_options.desc = false;  // true|false: will the markers' descriptions be shown below their names in the list?
			  gv_options.marker_list_options.icons = true;  // true|false: should the markers' icons appear to the left of their names in the list?
			  gv_options.marker_list_options.icon_scale = 'x1'; // size of the icons in the list; you can also supply a size in pixels (e.g. '16px')
			  gv_options.marker_list_options.thumbnails = false;  // true|false: should markers' thumbnails be shown in the list?
			  gv_options.marker_list_options.folders_collapsed = false;  // true|false: do folders in the list start out in a collapsed state?
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
				
				
				
				GV_Draw_Marker({lat:51.4620417,lon:-0.5489747,name:'',desc:'01N Canoe 3m',label:'Canoe 3m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4609043,lon:-0.5492113,name:'',desc:'02N Sub 4m',label:'Sub 4m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4603470,lon:-0.5489195,name:'',desc:'03N Scimitar Car 5.5m',label:'Scimitar Car 5.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4601029,lon:-0.5488384,name:'',desc:'04N Spitfire Car 6m',label:'Spitfire Car 6m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4605855,lon:-0.5489017,name:'',desc:'05N The Lightning Boat 5.5m',label:'The Lightning Boat 5.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4609476,lon:-0.5487832,name:'',desc:'06aN Caves Centre',label:'Caves Centre',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4608170,lon:-0.5487340,name:'',desc:'06bN Caves Lion Entrance Caves',label:'Caves Lion Entrance Caves',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4608980,lon:-0.5487013,name:'',desc:'06cN Red Isis Bike @ Cave',label:'Red Isis Bike @ Cave',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4608584,lon:-0.5487363,name:'',desc:'06dN Blue Raleigh Bike @ Caves',label:'Blue Raleigh Bike @ Caves',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4609369,lon:-0.5487749,name:'',desc:'06eN Cave PC Laptop',label:'Cave PC Laptop',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4600140,lon:-0.5487350,name:'',desc:'07P Cargo 2.5m',label:'Cargo 2.5m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4604302,lon:-0.5486882,name:'',desc:'08B The Hole',label:'The Hole',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4601540,lon:-0.5486870,name:'',desc:'09P Dance Platform 6m',label:'Dance Platform 6m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4600730,lon:-0.5485150,name:'',desc:'10N Bus 2m',label:'Bus 2m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4599718,lon:-0.5485828,name:'',desc:'11N Confined Area',label:'Confined Area',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4613356,lon:-0.5484697,name:'',desc:'12N Commer Van 6m',label:'Commer Van 6m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4605198,lon:-0.5484217,name:'',desc:'13B White Boat 7m',label:'White Boat 7m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4602986,lon:-0.5483127,name:'',desc:'14P Cargo 8m',label:'Cargo 8m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4601920,lon:-0.5482830,name:'',desc:'15P Cargo Rusty 8m',label:'Cargo Rusty 8m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4603400,lon:-0.5481730,name:'',desc:'16P Portacabin 8m',label:'Portacabin 8m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4599705,lon:-0.5481082,name:'',desc:'17P Shallow Platform 2m',label:'Shallow Platform 2m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4601746,lon:-0.5480586,name:'',desc:'18N Milk Float 6.5m',label:'Milk Float 6.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4604027,lon:-0.5480400,name:'',desc:'19N Chicken Hutch Boat 6.5m',label:'Chicken Hutch Boat 6.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4600385,lon:-0.5478824,name:'',desc:'20N Skittles Sweet Bowl 5.5m',label:'Skittles Sweet Bowl 5.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4602514,lon:-0.5478916,name:'',desc:'21B Sticky Up Boat 5m',label:'Sticky Up Boat 5m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4599186,lon:-0.5476810,name:'',desc:'22B Lady of Kent Search Light 5m',label:'Lady of Kent Search Light 5m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4600559,lon:-0.5476773,name:'',desc:'23N Traffic Lights 7m',label:'Traffic Lights 7m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4607730,lon:-0.5476209,name:'',desc:'24N Half Die Hard Taxi 8m',label:'Half Die Hard Taxi 8m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4599545,lon:-0.5475547,name:'',desc:'25N Boat In A Hole 7m',label:'Boat In A Hole 7m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4595937,lon:-0.5474898,name:'',desc:'26N Iron Fish 2m',label:'Iron Fish 2m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4604301,lon:-0.5473834,name:'',desc:'27aB Wreck Site 6m',label:'Wreck Site 6m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4604383,lon:-0.5472080,name:'',desc:'27bB 4 Wreck Site 6m',label:'4 Wreck Site 6m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4601316,lon:-0.5474179,name:'',desc:'29B Dive/Spike Boat 7m',label:'Dive/Spike Boat 7m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4598131,lon:-0.5473803,name:'',desc:'30B White Day boat by platform 6m',label:'White Day boat by platform 6m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4597660,lon:-0.5473470,name:'',desc:'31P 6m',label:'6m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4596580,lon:-0.5472500,name:'',desc:'32P 6m',label:'6m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4595563,lon:-0.5472633,name:'',desc:'33N New (Row) Boat 4.5m',label:'New (Row) Boat 4.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4603120,lon:-0.5471650,name:'',desc:'34P 6m',label:'6m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4599637,lon:-0.5471543,name:'',desc:'35N Dragon Boat 7.5m',label:'Dragon Boat 7.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4595550,lon:-0.5470800,name:'',desc:'36P 6m',label:'6m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4594757,lon:-0.5470871,name:'',desc:'37N Dive Bell 4m',label:'Dive Bell 4m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4598394,lon:-0.5469307,name:'',desc:'38B Lifeboat 6.5m',label:'Lifeboat 6.5m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4597290,lon:-0.5469929,name:'',desc:'39N London Black Cab 7m',label:'London Black Cab 7m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4602360,lon:-0.5468476,name:'',desc:'40N RIB Boat 6m',label:'RIB Boat 6m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4596766,lon:-0.5468125,name:'',desc:'41N Tin/Cabin Boat 7m',label:'Tin/Cabin Boat 7m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4594910,lon:-0.5468670,name:'',desc:'42P 6m',label:'6m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4602073,lon:-0.5467877,name:'',desc:'43N Thorpe Orange Boat 5.5m',label:'Thorpe Orange Boat 5.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4593680,lon:-0.5467601,name:'',desc:'44N VW Camper Van and Seahorse 5.5m',label:'VW Camper Van and Seahorse 5.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4598099,lon:-0.5467037,name:'',desc:'45B Listing Sharon 7.5m',label:'Listing Sharon 7.5m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4597450,lon:-0.5466490,name:'',desc:'46N Plane 6m',label:'Plane 6m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4593990,lon:-0.5465940,name:'',desc:'47P 6m',label:'6m',label_color:'blue',color:'white',icon:'square'});
				GV_Draw_Marker({lat:51.4594384,lon:-0.5465238,name:'',desc:'48N Holey Ship 4.5m',label:'Holey Ship 4.5m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4596344,lon:-0.5464664,name:'',desc:'49B Claymore 6.5m',label:'Claymore 6.5m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4591437,lon:-0.5460323,name:'',desc:'50aN Swim Through - no crates 6m',label:'Swim Through - no crates 6m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4591694,lon:-0.5459990,name:'',desc:'50bN Swim Through - mid 6m',label:'Swim Through - mid 6m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4592045,lon:-0.5459126,name:'',desc:'50cN Swim Through - crates 6m',label:'Swim Through - crates 6m',label_color:'blue',color:'magenta',icon:'cross'});
				GV_Draw_Marker({lat:51.4591431,lon:-0.5459369,name:'',desc:'51B Orca Van 5.5m',label:'Orca Van 5.5m',label_color:'blue',color:'blue',icon:'circle'});
				GV_Draw_Marker({lat:51.4601285,lon:-0.5488505,name:'',desc:'X01 Dinghy Boat',label:'Dinghy Boat',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4604340,lon:-0.5489210,name:'',desc:'X02 Quarry Machine in Reeds',label:'Quarry Machine in Reeds',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4599583,lon:-0.5476486,name:'',desc:'X03 Metal Grated Box',label:'Metal Grated Box',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4599019,lon:-0.5471413,name:'',desc:'X04 4 crates in a line',label:'4 crates in a line',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4598468,lon:-0.5472127,name:'',desc:'X05 Lone crate',label:'Lone crate',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4596708,lon:-0.5472531,name:'',desc:'X06 Collapsed Metal',label:'Collapsed Metal',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4600386,lon:-0.5487241,name:'',desc:'X07 Boat with Chain Links',label:'Boat with Chain Links',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4599406,lon:-0.5485203,name:'',desc:'X08 Pot in a box',label:'Pot in a box',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4600703,lon:-0.5486457,name:'',desc:'X09 Seahorse Mid-Water',label:'Seahorse Mid-Water',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4600603,lon:-0.5486717,name:'',desc:'X10 Headless Nick',label:'Headless Nick',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4600452,lon:-0.5488188,name:'',desc:'X11 Headless Tom Reeds',label:'Headless Tom Reeds',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4602002,lon:-0.5478815,name:'',desc:'X12 Cement Mixer',label:'Cement Mixer',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4600531,lon:-0.5481839,name:'',desc:'X13 Tyre',label:'Tyre',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4595778,lon:-0.5473580,name:'',desc:'X14 Roadworks Sign',label:'Roadworks Sign',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4599975,lon:-0.5481015,name:'',desc:'X15 Fireworks Launcher',label:'Fireworks Launcher',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4593265,lon:-0.5469361,name:'',desc:'X16 2 Buried Boats in Reeds',label:'2 Buried Boats in Reeds',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4596635,lon:-0.5470603,name:'',desc:'X17 Half Buried Solo Boat',label:'Half Buried Solo Boat',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4600594,lon:-0.5475755,name:'',desc:'X18 Half Buried Bike',label:'Half Buried Bike',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4599240,lon:-0.5476152,name:'',desc:'X19 Desk with Keyboard',label:'Desk with Keyboard',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4607400,lon:-0.5477130,name:'',desc:'X20 La Mouette Boat',label:'La Mouette Boat',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4609930,lon:-0.5480060,name:'',desc:'X21 Memorial Stone - Kit 7.5m',label:'Memorial Stone - Kit 7.5m',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4593530,lon:-0.5469390,name:'',desc:'X22 Fruit Machine 5.5m',label:'Fruit Machine 5.5m',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4612050,lon:-0.5488430,name:'',desc:'X23 X23 ? near caves 7m',label:'X23 ? near caves 7m',label_color:'blue',color:'pink',icon:'star'});
				GV_Draw_Marker({lat:51.4612490,lon:-0.5486880,name:'',desc:'X24 X24 ? near commer van 7m',label:'X24 ? near commer van 7m',label_color:'blue',color:'pink',icon:'star'});
				GV_Draw_Marker({lat:51.4597840,lon:-0.5465500,name:'',desc:'X25 X25 ? near plane 6m',label:'X25 ? near plane 6m',label_color:'blue',color:'pink',icon:'star'});
				GV_Draw_Marker({lat:51.4600280,lon:-0.5482990,name:'',desc:'X26 X26 ? near cafe jetty 6m',label:'X26 ? near cafe jetty 6m',label_color:'blue',color:'pink',icon:'star'});
				GV_Draw_Marker({lat:51.4603850,lon:-0.5474270,name:'',desc:'X27 X27 ? near wreck site 7m',label:'X27 ? near wreck site 7m',label_color:'blue',color:'pink',icon:'star'});
				GV_Draw_Marker({lat:51.4623163,lon:-0.5494161,name:'',desc:'X28 Cotton Reel 2m',label:'Cotton Reel 2m',label_color:'blue',color:'yellow',icon:'triangle'});
				GV_Draw_Marker({lat:51.4609593,lon:-0.5480156,name:'',desc:'X29 *X29 close memorial*',label:'*X29 close memorial*',label_color:'blue',color:'pink',icon:'star'});
				GV_Draw_Marker({lat:51.4595700,lon:-0.5473067,name:'',desc:'X30 *X30 near mid jetty*',label:'*X30 near mid jetty*',label_color:'blue',color:'pink',icon:'star'});
				GV_Draw_Marker({lat:51.4600631,lon:-0.5480722,name:'',desc:'X31 *X31 near cafe jetty*',label:'*X31 near cafe jetty*',label_color:'blue',color:'pink',icon:'star'});
				GV_Draw_Marker({lat:51.4600150,lon:-0.5483160,name:'',desc:'Z01 Cafe Jetty',label:'Cafe Jetty',label_color:'blue',color:'green',icon:'diamond'});
				GV_Draw_Marker({lat:51.4595470,lon:-0.5474610,name:'',desc:'Z02 Mid Jetty',label:'Mid Jetty',label_color:'blue',color:'green',icon:'diamond'});
				GV_Draw_Marker({lat:51.4591660,lon:-0.5469993,name:'',desc:'Z03 Old Jetty',label:'Old Jetty',label_color:'blue',color:'green',icon:'diamond'});
				
				GV_Finish_Map();
					
			}
			GV_Map(); // execute the above code
			// https://www.gpsvisualizer.com/map_input?form=google&format=google&google_wpt_labels=1&units=metric
		</script>
	</body>
</html>)rawliteral";

const uint32_t MAP_HTML_SIZE = sizeof(MAP_HTML);

#endif
