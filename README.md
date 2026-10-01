# easyMFD architecture & setup guide

easyMFD turns any ESP32 + TFT display pair into a SignalK-driven marine instrument: pick a SignalK path in a Node-RED 
dashboard, describe how it should look, and the display renders it live over MQTT, either dynamically changing the
displayed values with changes to those paths in Signalk, or via seperate, user-configurable logic. A second, 
independent layer adds user-'shape-able' piezo-buzzer alerts with user-definable alert tones to the same hardware
(users can setup and save alert tones specific to function by setting their volume, frequency, and overall tone 
characteristics, ie. duration, repeat pattern, etc). 


## What easyMFD does
The package was written to automate the Signalk path data selection and data formatting, and all related I/O 
(ingesting data from Signalk, sending data out to displays), and make that easily re-usable with any Node Red 
triggering or data-processing logic.  Since each boat will have different display requirements, different size 
and numbers of displays, and different needs (when, where, and under what conditions to send display data and 
alerts), the package allows users to easily add/integrate their own Node Red logic into these flows, essentially 
enabling limitless flexibility and control over how data is managed and displayed such ESP32 controlled TFT displays. 

The package includes ESP32 firmeare (for seperate ESP32/TFT pairs, that is, I have not yet released a version for 
off-the-shelf integated ESP32/TFT displays, like the WaveShare displays, but that is coming soon).  FlowFuse 
dashboard nodes are included for the the Node Red control GUI, and two Node-RED "universal" functions, which allows 
the user to create bespoke logic in Node Red to trigger when and where the display and alerts are sent.  When data 
changes in Signalk, or when a users logic dictates change to the display data and its look/feel (how it is formatted
on the display), when an alert should fire, what logic connects the two, etc., is entirely up to the user, and common 
display and alerting 'profiles' can be easily created, saved, and edited with the Node Red GUI, with integration into
your logic done simply with ordinary drag-and-drop Node-RED nodes.

In essence, in the GUI a user can enter the SignalK path in a dashboard, describes how it should look (title, unit, 
font, color, brightness), and save it. From then on, any change to that path is automatically formatted and pushed 
to the named display over MQTT. Alerts work the same way but for sound: a user defines a named buzzer "profile" 
(loudness, tone, duration, pattern) once, and any flow - however it decides an alert is warranted - can fire that 
profile at any display by name.

By design, display logic and alert logic are two fully independent systems that happen to share the same physical 
hardware (ESP32 + TFT + cheap 3-5v piezo buzzer). Nothing about how a display renders depends on alerting, and 
nothing about how an alert sounds depends on what is currently on screen.

# What is in the repo:
- ESP32 firmware 
- Node Red GUI and function nodes

# Architecture overview
One SignalK on-delta node feeds both pipelines. The display side matches the incoming path against saved display 
configs and formats a value; the alert side only acts when the user's own trigger logic decides to. Both pipelines 
publish to their own MQTT topic and only reconverge at the ESP32, which runs one WiFi/MQTT connection subscribed to 
two topics per physical display.

# Design principles
A handful of decisions repeat throughout the system. Knowing them makes the rest of this guide - and any future 
extension of easyMFD - easier to follow: 

• Total separation of display and alerting. Separate SignalK subscriptions, separate Node-RED functions, separate 
	MQTT topics, separate MQTT-out nodes, separate config stores, separate dashboard panels. A display can change with 
	no alert firing; an alert can fire with the display untouched. Nothing is shared between the two pipelines except 
	the physical ESP32 and its one WiFi/MQTT connection.
• Infinite logic in the middle. easyMFD supplies the parts that are genuinely hard to get right - correct display formatting, 
	correct non-blocking buzzer timing - once, centrally. Everything about when something should happen (a threshold, a 
	compound condition, an edge-triggered alert, a debounce) is left to the user's own Node-RED flow, built from ordinary 
	nodes (Switch, Change, rbe, delay). easyMFD never tries to anticipate every use case; it only makes every use case easy to wire up.
• Profiles, not hardcoding. Both a display's look and an alert's sound are saved as named, reusable configuration - not 
	hand-coded per use. A user builds a small library of profiles once ("Critical Alert", "Gentle Reminder") and references 
	them by name from any number of trigger flows.
• Persistence by default. All configuration - displays and alert profiles alike - is written to Node-RED's file-based global 
	context, so it survives a Node-RED restart or a power loss, not just an in-memory session.
• Non-blocking firmware. Both display rendering and buzzer playback run inside the same loop() on the ESP32 without blocking 
	the MQTT client. Buzzer timing in particular is a millis()-based state machine rather than delay(), so a 90-second 
	alert pattern never stalls incoming MQTT messages or display updates.
• One topic per display drives both pipelines. Each physical display has one MQTT topic, set once in firmware, which doubles 
	as its identity. Its buzzer topic is always <topic>/buzzer, derived automatically at boot - so the two pipelines stay 
	in sync without a second constant to maintain.

# Display pipeline: setup and use

Components (Node Red nodes, function)
- Display Manager (ui_template)
- Dashboard panel: list, add, edit, delete display configs
- Display config store (function)
- Reads/writes the config array to global context, key easyMFD_displays
- SignalK on-delta (flattened)
- Subscribes to all SignalK deltas
- Universal display function
- Matches each delta against saved configs, formats, publishes
- MQTT out (display)
- Publishes to the display's own topic, no retain assumed by default

Wiring: the Display Manager UI node and its config store form a closed loop - the UI's output feeds the store's input, and the 
	store's output feeds back into the UI's input - exactly the pattern Node-RED uses for a dashboard panel that lists, saves, 
	and deletes records. Separately, the SignalK on-delta node feeds the Universal display function, which feeds the MQTT-out 
	node. These two loops never touch: the UI only ever reads and writes the config store; it has no wire to SignalK, the 
	universal function, or MQTT out.

# Setting up a display:
1. Open the Display Manager panel and click Add display.
2. Give it a Display Name (for your own reference) and the MQTT topic that identifies the destination ESP32/TFT pair (this 
	must match that device's MQTT_TOPIC constant in firmware exactly).
3. Enter the SignalK path to watch, and optionally a SignalK source to disambiguate when more than one sensor reports the same path.
4. Enter the Title and Unit to display, an optional math expression (x = the raw SignalK value - e.g. (x - 273.15) * 9/5 + 32 to 
convert Kelvin to Fahrenheit) and decimal places.
5. Set font color, font size (1-6), and brightness (0-255).
6. Save. The config is written to easyMFD_displays immediately and survives a restart.

From that point on, every matching SignalK delta is automatically evaluated, formatted, and pushed to that display - no further 
action needed. A display's settings persist on the ESP32 across updates within the same "session" (while title stays the same); 
changing title resets font color/size/brightness to firmware defaults before applying the new message's overrides, so a custom 
look never bleeds from one data source into the next.

# Alerting pipeline: setup and use
Components (Node, Role)
- Alert Manager (ui_template)
- Dashboard panel: list, add, edit, delete alert profiles
- Alert config store (function)
- Reads/writes the profile array to global context, key easyMFD_alerts
- Your trigger flow
- User-built logic that decides when an alert should fire
- Alert Sender function
- Looks up a profile by name, converts it to the wire JSON
- MQTT out (alert)
- Publishes to <topic>/buzzer - a separate node from the display's MQTT out, deliberately never shared, so a retain flag meant 
for display state can never leak onto a buzzer event.  An alert profile is pure sound, with no path and no threshold. It has a name, 
a loudness (0-100%, maps to PWM duty cycle - 100% is electrically identical to holding the pin HIGH), a frequency (100-15,000 Hz, 
log-scaled slider), a total duration, a tone-segment duration, and a pause duration between segments. It says nothing about when 
it should play or which display it targets - that is intentional, so one saved profile ("Critical Alert") can be reused by any number 
of unrelated trigger flows, aimed at any display, at trigger time.

Setting up a profile: open the Alert Manager panel, click Add alert profile, name it, set loudness/frequency/duration/segment/pause, 
and save. The profile is written to easyMFD_alerts immediately.

# Firing an alert always requires a flow of your own, built from three pieces in sequence:
1. A condition. Something that decides when to fire - typically a Switch node reading a SignalK value, though it can be anything 
(a compound condition across two paths, a calculated distance-to-waypoint, a timer). This node should sit directly on a live 
SignalK on-delta subscription so it is always evaluating current data.
2. A shaping step. A Change node (or similar) that, only when the condition's true branch fires, sets msg.topic to the 
destination display's MQTT topic and msg.payload to the alert profile's name as a plain string.
3. The Alert Sender function, wired downstream of that. It looks up the named profile in easyMFD_alerts, converts its 
seconds-based fields to milliseconds, and publishes the expanded JSON to <destination topic>/buzzer via the alert 
MQTT-out node.

The Alert Sender function expects only pre-decided, already-shaped trigger messages. It is not meant to sit directly 
on the raw SignalK firehose - every delta that reaches it without matching a saved profile name produces a 
node.warn() (visible in the Debug sidebar regardless of whether any Debug node is deployed), which is the system correctly 
telling you it received unshaped input, not a bug.

# JSON schemas reference
Display payload (published by the Universal display function to <topic>):
{
  "title": "Outside Temperature",
  "value": "67.5",
  "unit": "F",
  "font_color": "yellow",
  "brightness": 200,
  "font_size": 4
}
All fields except title, value, and unit are optional; an omitted setting keeps whatever the display currently has, 
so Node-RED only needs to send a setting once per "session" (while title stays unchanged).
Alert profile (stored in easyMFD_alerts, edited via the Alert Manager UI):
{
  "name": "Critical Alert",
  "loudness": 100,
  "frequency_hz": 3000,
  "duration_s": 90,
  "segment_s": 5,
  "pause_s": 1
}
Alert wire payload (published by the Alert Sender function to <topic>/buzzer):
{
  "loudness": 100,
  "frequency_hz": 3000,
  "duration_ms": 90000,
  "segment_ms": 5000,
  "pause_ms": 1000
}

The alert profile is stored and edited in seconds (human-friendly, matches the UI); the wire payload converts 
to milliseconds for firmware timing precision. The segment/pause pattern repeats until duration_ms elapses 
and then stops outright - even mid-segment or mid-pause - so an alert never overruns its stated total length.

# Writing your own trigger logic
A plain Switch node on a SignalK on-delta subscription fires once per matching delta - and deltas arrive 
continuously while a condition holds true, not once per state change. Whether that is what you want depends 
on the alert:
• Edge-triggered (fire once per entry into the alert state). For something like an anchor-drag or a "just 
crossed into danger" alert, you want one alert on the transition into the condition, then silence while it 
remains true. This needs state: an rbe (report-by-exception) node in "block unless value changes" mode, or 
a Function node comparing the current reading against a previous value held in context.get()/context.set(), 
forwards only on the false-to-true transition.
• Repeating (re-fire while the condition holds). For a sustained danger alarm that should keep sounding as long 
as a threshold is exceeded, let the condition keep re-firing, but throttle it with a delay node in rate-limit mode 
(e.g. one message per 10 seconds) so MQTT isn't flooded at delta frequency.

Both patterns end the same way: constructing {topic: <destination>, payload: <profile name>} and handing it to 
the Alert Sender function. The condition logic decides when and whether; the profile library decides what it sounds 
like. The same profile can be referenced by any number of differently-shaped trigger flows - a heel-angle Switch 
chain, an anchor-drag calculation, a waypoint-proximity check - without any of them needing to know how the others work.

# Troubleshooting notes
Learned the hard way during bring-up - worth checking first when something doesn't behave:
• Pin config must match the physical PCB. Each new PCB revision needs its LGFX pin block (pin_sclk, pin_mosi, pin_dc, 
pin_cs, pin_rst, pin_bl) updated to match that board's actual wiring - firmware copied from an earlier revision 
carries the old pins forward silently. A backlight-pin mismatch in particular looks exactly like "nothing is 
happening" (the panel may be rendering correctly underneath, just with no backlight to see it by). None of 
GPIO 7-13 are hardware-restricted on the ESP32-S3; the real strapping pins to avoid are 0, 3, 45, 46, and 
the N16R8's PSRAM interface uses 33-37.

• MQTT stuck on "Connecting..." after WiFi connects fine almost always means MQTT_BROKER doesn't point at the 
actual Mosquitto host - check the Serial Monitor for the rc= code on each retry, and confirm the IP against 
what Node-RED's own MQTT nodes are configured to use (not a router/gateway address).
• "JSON parse error: Empty input" / "Invalid input" on the display topic is usually a stale retained message 
replayed by the broker on subscribe, or two flows publishing to the same topic. Clear a retained message with 
an empty retained publish to that topic (mosquitto_pub -t <topic> -n -r).
• Never wire an Inject node's payload directly to an alert MQTT-out node. It must pass through the Alert 
Sender function, which expands a profile name into the full buzzer JSON - a raw profile-name string published 
directly is not valid JSON and the ESP32 will reject it with a parse error.
• A flood of "no alert profile named X" warnings in the Debug sidebar means a live SignalK on-delta node is wired 
directly into the Alert Sender function with no condition logic in between - every delta is being evaluated as a 
(mismatching) profile name. node.warn() writes to the sidebar regardless of whether any Debug node is deployed, 
so deleting a debug node won't stop it; add a Switch/condition node upstream instead (see "Writing your own 
trigger logic" above).


# More on easyMFD audible alerts:

(INSERT PIC OF NODE RED FLOWS WITH NODE CONFIGS IN GITHUB)

Alerting is idependent of display settings by design, since alerts may be sent for any 
reason, and may not be tied to whatever is displayed at the time (for example, a simple tone 
sent every hour, like a classic ships "bell", or if the windspeed exceeds some notable 
threshold, etc).  The idea is that the alert logic can be easily created and the alerts can be
easily sent with easyMFD, either using an exising 'Configured alert' in the GUI, or by creating a 
new alert (such configured alerts can be accumulated as a library, which will be saved by nodeRed,
to be called and used any time based on user logic.

The Alert Sender function is expecting exactly two things on the incoming message to send the
alert: 1. msg.topic set to the destination display's topic (for both display and alerting, the
topic is the 'destination display' for the flow'), and 2. msg.payload set to the alert profile's 
name (as a plain string). So to fire configured alert 'test', for example, at display named 'helm'
in the display manager (display topic for that display is set to 'helm' string), just inject(or
send with any logic equivalent to an 'inject' to an MQTT out node with the topic set to that
display name ('helm' in this example) with the payload set to 'test' (configured profile name).

Example (where Alert Configuration is named 'test' and topic is named 'display_name': 
- Drag  SIGNALK ON DELTA NODE into window and assign context to boat (eg. 'vessel.self') and click 
	'flatten' option to ON. 
- Create trigger logic.  For a sinple example, wire inject node (or ANY LOGIC FLOW) to inject payload 
	('test', in this example) to topic (destination display, display_name here)
	to the output of that signalk on delta node (as the input to the logic flow)
- Wire logic flow conditional logic output (eg., switch that states if X condition is encountered, send
	topic and payload to Sender Alert Function as output node (see flow diagram), 
	to an MQTT OUT node (with topic blank, as it is set upstream in this example).  IMPORTANT: 
	Do not leave SK ON DELTA node hard wired to the Sender Alert Function (some sort of logic gate, 
	eg said switch node, must exist between those two nodes (SIGNALK ON DELTA node and SENDER FUNCTION CODE, 
	otherwise (ie, if they are hardwired without logic gate of some kind in the middle, the MQTT OUT node 
	will get hosed with errors, since ON DELTA is constantly ingesting all signalk data.  This logic flow can be 
	anything, whatever conditions you may chose to alert on, either related to incoming signalk data (changes 
	in path data (changes in path values, thresholds, timers, etc), or data from other sources integrated with 
	node red (eg., send alarm-like alert when motion sensor is triggered, etc).  When that conditional logic is met/encountered, 
	it triggers the specified alert profile to be sent to the appropriate display(s).  

Alert Profile in a nutshell:
- Payload: type string, value {configuration Alert Profile}
- Topic: Display Name (must match your ESP32's MQTT_TOPIC constant exactly — case-sensitive, as 
	configured in the display GUI)
- Note: be sure to set MQTT out 'Server' field to localhost:1883 (the MQTT server).  Topic can be left blank
	if it is defined in an upstream node.  For example, if an 'inject' node is set to inject string
	'test' to topic 'helm', those values are established in that inject node and MQTT out just passes
	them through.

(INSERT LOGIC EXAMPLE)

How to trigger on SK path data (how to make this work with the Signalk data-model)
Triggering an alert based in Signalk data or deltas is actually simpler than matching against 
the full SignalK data model — because the signalk-on-delta node (with Flatten checked, same setting 
your existing functions already rely on) has already done that collapsing for you before your Switch 
node ever sees the message.

What actually arrives at your Switch node, once vessels.self fires on any path change:

msg.topic = the flattened SignalK path as a plain string, e.g. "environment.outside.temperature"
msg.payload = the raw SI value for that path, e.g. 295.2 (Kelvin — SignalK always transmits 
temperature in SI units, same reason universalPathFunction.js does the Kelvin→Fahrenheit conversion before displaying it)
msg["$source"] = which sensor/source reported it, if you need to disambiguate multiple sensors on the same path

So there's no nested delta JSON to dig into and no need to write a JSONata expression against the SignalK 
data model — that's exactly the flattening step's job, and it's why displayManager.js can just 
do d.path !== msg.topic as a plain string comparison rather than parsing anything.

Structurally, you need two separate checks, combined with AND: "is this the path I care about" and 
"does its value cross my threshold." A single Switch node rule can't do both at once (each rule tests one 
property against one condition), so you have two clean options:

Option A — two Switch nodes chained in series (pure config, no code):

First Switch node: property msg.topic, rule "is equal to", value environment.outside.temperature (string). Only 
messages for that exact path pass through.

Second Switch node downstream: property msg.payload, rule "is greater than", value your threshold in Kelvin 
(since the raw value hasn't been converted yet — e.g. 30°C would be 303.15, not 30).

Option B — one Function node doing both checks plus the unit conversion in one place, which is more consistent 
with how the rest of easyMFD already handles this:

javascript
if (msg.topic !== "environment.outside.temperature") {
    return null; // wrong path, drop it
}

const fahrenheit = (msg.payload - 273.15) * 9 / 5 + 32;
const THRESHOLD_F = 95;

if (fahrenheit > THRESHOLD_F) {
    msg.topic = "new_pcb"; // destination display
    msg.payload = "Critical Alert"; // profile name
    return msg;
}

return null; // condition not met, drop it

I would probably suggest Option B for anything that needs a unit conversion before the comparison makes sense 
(temperature, wind speed in m/s vs knots, etc.) — it's the same "convert raw SI, then compare/format" 
pattern your Universal display function and universalPathFunction.js already use, just producing a 
trigger decision instead of a display payload. Option A is fine and code-free for values that don't 
need conversion (e.g. a heel angle already in degrees, or a boolean-ish flag).

Either way, once the condition is true, that's the exact point where you'd set msg.topic/msg.payload 
to the destination + profile name and hand off to Alert Sender — same pattern we walked through 
earlier, just now you know precisely what's structurally available to test against.


