// SPDX-License-Identifier: GPL-2.0
/* planner.c
 *
 * code that allows us to plan future dives
 *
 * (c) Dirk Hohndel 2013
 */
#include <assert.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include "dive.h"
#include "deco.h"
#include "divelist.h"
#include "planner.h"
#include "gettext.h"
#include "libdivecomputer/parser.h"
#include "qthelperfromc.h"
#include "version.h"

int diveplan_duration(struct diveplan *diveplan)
{
	struct divedatapoint *dp = diveplan->dp;
	int duration = 0;
	while(dp) {
		if (dp->time > duration)
			duration = dp->time;
		dp = dp->next;
	}
	return (duration + 30) / 60;
}

/* Add the icd results of one trimix gas change to the dive plan html buffer. Two rows are added to the table, one
 * indicating fractions of gas, the other indication partial pressures of gas. This function makes use of the
 * icd_data structure that was filled with information by the function isobaric_counterdiffusion().
 * Parameters: 1) Pointer to the output buffer position at which writing should start.
 *             2) The size of the part of the unused output buffer that remains unused.
 *             3) The data structure containing icd calculation results: icdvalues.
 *             4) The names of the gasses in the gas change: gas-from and gas-to.
 * Returns:    The size of the output buffer that has been used after the new results have been added.
 */
int add_icd_entry(char *icdbuffer, unsigned int maxsize, struct icd_data *icdvalues, int time_seconds, int ambientpressure_mbar, struct gasmix *gas_from, struct gasmix *gas_to)
{
	int len;
	len = snprintf(icdbuffer, maxsize, 
		"<tr><td rowspan='2' style= 'vertical-align:top;'>%3d%s</td>"
		"<td rowspan=2 style= 'vertical-align:top;'>%s&#10137;",
		(time_seconds + 30) / 60, translate("gettextFromC", "min"), gasname(gas_from));
	len += snprintf(icdbuffer + len, maxsize - len, 
		"%s</td><td style='padding-left: 10px;'>%+5.2f%%</td>"
		"<td style= 'padding-left: 15px; color:%s;'>%+5.2f%%</td>"
		"<td style='padding-left: 15px;'>%+5.2f%%</td></tr>"
		"<tr><td style='padding-left: 10px;'>%+5.2f%s</td>"
		"<td style='padding-left: 15px; color:%s;'>%+5.2f%s</td>"
		"<td style='padding-left: 15px;'>%+5.2f%s</td></tr>",
		gasname(gas_to), icdvalues->dHe / 10.0, 
		((5 * icdvalues->dN2) > -icdvalues->dHe) ? "red" : "#383838", icdvalues->dN2 / 10.0 , 0.2 * (-icdvalues->dHe / 10.0), 
		ambientpressure_mbar * icdvalues->dHe / 1e6f, translate("gettextFromC", "bar"), ((5 * icdvalues->dN2) > -icdvalues->dHe) ? "red" : "#383838",
		ambientpressure_mbar * icdvalues->dN2 / 1e6f, translate("gettextFromC", "bar"), 
		ambientpressure_mbar * -icdvalues->dHe / 5e6f, translate("gettextFromC", "bar"));

	return len;
}

void add_plan_to_notes(struct diveplan *diveplan, struct dive *dive, bool show_disclaimer, int error)
{
	const unsigned int sz_buffer = 2000000;
	const unsigned int sz_temp = 100000;
	const unsigned int sz_icdbuf = 10000;
	char *buffer = (char *)malloc(sz_buffer);
	char *temp = (char *)malloc(sz_temp);
	char *icdbuffer = (char *)malloc(sz_icdbuf);
	const char *deco, *segmentsymbol;
	static char buf[1000];
	int len, lastdepth = 0, lasttime = 0, lastsetpoint = -1, newdepth = 0, lastprintdepth = 0, lastprintsetpoint = -1;
	int icyl, icdlen = 0;
	struct gasmix lastprintgasmix = {{ -1 }, { -1 }};
	struct divedatapoint *dp = diveplan->dp;
	bool plan_verbatim = prefs.verbatim_plan;
	bool plan_display_runtime = prefs.display_runtime;
	bool plan_display_duration = prefs.display_duration;
	bool plan_display_transitions = prefs.display_transitions;
	bool gaschange_after = !plan_verbatim;
	bool gaschange_before;
	bool lastentered = true;
	bool istrimix = false;
	bool icdwarning = false;
	struct divedatapoint *nextdp = NULL;
	struct divedatapoint *lastbottomdp = NULL;
	struct icd_data icdvalues;

	if (decoMode() == VPMB) {
		deco = translate("gettextFromC", "VPM-B");
	} else {
		deco = translate("gettextFromC", "BUHLMANN");
	}

	snprintf(buf, sizeof(buf), translate("gettextFromC", "DISCLAIMER / WARNING: THIS IS A NEW IMPLEMENTATION OF THE %s "
		"ALGORITHM AND A DIVE PLANNER IMPLEMENTATION BASED ON THAT WHICH HAS "
		"RECEIVED ONLY A LIMITED AMOUNT OF TESTING. WE STRONGLY RECOMMEND NOT TO "
		"PLAN DIVES SIMPLY BASED ON THE RESULTS GIVEN HERE."), deco);
	disclaimer = buf;

	if (!dp)
		goto finished;

	if (error) {
		snprintf(temp, sz_temp, "%s",
			translate("gettextFromC", "Decompression calculation aborted due to excessive time"));
		snprintf(buffer, sz_buffer, "<span style='color: red;'>%s </span> %s<br>",
			translate("gettextFromC", "Warning:"), temp);
		dive->notes = strdup(buffer);
		goto finished;
	}

	len = show_disclaimer ? snprintf(buffer, sz_buffer, "<div><b>%s</b><br></div>", disclaimer) : 0;

	if (diveplan->surface_interval < 0) {
		len += snprintf(buffer + len, sz_buffer - len, "<div><b>%s (%s) %s<br>",
			translate("gettextFromC", "Subsurface"),
			subsurface_canonical_version(),
			translate("gettextFromC", "dive plan</b> (overlapping dives detected)"));
		dive->notes = strdup(buffer);
		free((void *)buffer);
		free((void *)temp);
		return;
	} else if (diveplan->surface_interval >= 48 * 60 *60) {
		const char *current_date = get_current_date();
		len += snprintf(buffer + len, sz_buffer - len, "<div><b>%s (%s) %s %s</b><br>",
			translate("gettextFromC", "Subsurface"),
			subsurface_canonical_version(),
			translate("gettextFromC", "dive plan</b> created on"),
			current_date);
		free((void *)current_date);
	} else {
		const char *current_date = get_current_date();
		len += snprintf(buffer + len, sz_buffer - len, "<div><b>%s (%s) %s %d:%02d) %s %s<br>",
			translate("gettextFromC", "Subsurface"),
			subsurface_canonical_version(),
			translate("gettextFromC", "dive plan</b> (surface interval "),
			FRACTION(diveplan->surface_interval / 60, 60),
			translate("gettextFromC", "created on"),
			current_date);
		free((void *)current_date);
	}

	if (prefs.display_variations && decoMode() != RECREATIONAL)
		len += snprintf(buffer + len, sz_buffer - len, translate("gettextFromC", "Runtime: %dmin%s"),
			diveplan_duration(diveplan), "VARIATIONS<br></div>");
	else
		len += snprintf(buffer + len, sz_buffer - len, translate("gettextFromC", "Runtime: %dmin<br></div>"),
			diveplan_duration(diveplan));

	if (!plan_verbatim) {
		len += snprintf(buffer + len, sz_buffer - len, "<table><thead><tr><th></th><th>%s</th>",
			translate("gettextFromC", "depth"));
		if (plan_display_duration)
			len += snprintf(buffer + len, sz_buffer - len, "<th style='padding-left: 10px;'>%s</th>",
				translate("gettextFromC", "duration"));
		if (plan_display_runtime)
			len += snprintf(buffer + len, sz_buffer - len, "<th style='padding-left: 10px;'>%s</th>",
				translate("gettextFromC", "runtime"));
		len += snprintf(buffer + len, sz_buffer - len,
				"<th style='padding-left: 10px; float: left;'>%s</th></tr></thead><tbody style='float: left;'>",
				translate("gettextFromC", "gas"));
	}
	for (icyl = 0; icyl < MAX_CYLINDERS; icyl++) {	// If dive plan has an OC cylinder with helium, then initialise ICD table:
		if ((dive->cylinder[icyl].cylinder_use == OC_GAS) && (get_he(&dive->cylinder[icyl].gasmix) > 0)) {	
			istrimix = true;
			icdlen = 0;
			icdlen += snprintf(icdbuffer + icdlen, sz_icdbuf - icdlen, "<div>%s:",
				translate("gettextFromC","Isobaric counterdiffusion information"));
			icdlen += snprintf(icdbuffer + icdlen, sz_icdbuf - icdlen, "<table><tr><td align='left'><b>%s</b></td>",
				translate("gettextFromC", "runtime"));
			icdlen += snprintf(icdbuffer + icdlen, sz_icdbuf - icdlen, "<td align='center'><b>%s</b></td>",
				translate("gettextFromC", "gaschange"));
			icdlen += snprintf(icdbuffer + icdlen, sz_icdbuf - icdlen, "<td style='padding-left: 15px;'><b>%s</b></td>",
				translate("gettextFromC", "&#916;He"));
			icdlen += snprintf(icdbuffer + icdlen, sz_icdbuf - icdlen, "<td style='padding-left: 20px;'><b>%s</b></td>",
				translate("gettextFromC", "&#916;N&#8322;"));
			icdlen += snprintf(icdbuffer + icdlen, sz_icdbuf - icdlen, "<td style='padding-left: 10px;'><b>%s</b></td></tr>",
				translate("gettextFromC", "max &#916;N&#8322;"));
			break;
		}
	}

	do {
		struct gasmix gasmix, newgasmix = {};
		const char *depth_unit;
		double depthvalue;
		int decimals;
		bool isascent = (dp->depth.mm < lastdepth);

		nextdp = dp->next;
		if (dp->time == 0)
			continue;
		gasmix = dive->cylinder[dp->cylinderid].gasmix;
		depthvalue = get_depth_units(dp->depth.mm, &decimals, &depth_unit);
		/* analyze the dive points ahead */
		while (nextdp && nextdp->time == 0)
			nextdp = nextdp->next;
		if (nextdp)
			newgasmix = dive->cylinder[nextdp->cylinderid].gasmix;
		gaschange_after = (nextdp && (gasmix_distance(&gasmix, &newgasmix) || dp->setpoint != nextdp->setpoint));
		gaschange_before =  (gasmix_distance(&lastprintgasmix, &gasmix) || lastprintsetpoint != dp->setpoint);
		/* do we want to skip this leg as it is devoid of anything useful? */
		if (!dp->entered &&
		    nextdp &&
		    dp->depth.mm != lastdepth &&
		    nextdp->depth.mm != dp->depth.mm &&
		    !gaschange_before &&
		    !gaschange_after)
			continue;
		if ((dp->time - lasttime < 10 && lastdepth == dp->depth.mm) && !(gaschange_after && dp->next && dp->depth.mm != dp->next->depth.mm))
			continue;

		/* Store pointer to last entered datapoint for minimum gas calculation */
		if (dp->entered && !nextdp->entered)
			lastbottomdp = dp;

		len = strlen(buffer);
		if (plan_verbatim) {
			/* When displaying a verbatim plan, we output a waypoint for every gas change.
			 * Therefore, we do not need to test for difficult cases that mean we need to
			 * print a segment just so we don't miss a gas change.  This makes the logic
			 * to determine whether or not to print a segment much simpler than  with the
			 * non-verbatim plan.
			 */
			if (dp->depth.mm != lastprintdepth) {
				if (plan_display_transitions || dp->entered || !dp->next || (gaschange_after && dp->next && dp->depth.mm != nextdp->depth.mm)) {
					if (dp->setpoint) {
						snprintf(temp, sz_temp, translate("gettextFromC", "Transition to %.*f %s in %d:%02d min - runtime %d:%02u on %s (SP = %.1fbar)"),
							 decimals, depthvalue, depth_unit,
							 FRACTION(dp->time - lasttime, 60),
							 FRACTION(dp->time, 60),
							 gasname(&gasmix),
							 (double) dp->setpoint / 1000.0);
					} else {
						snprintf(temp, sz_temp, translate("gettextFromC", "Transition to %.*f %s in %d:%02d min - runtime %d:%02u on %s"),
							 decimals, depthvalue, depth_unit,
							 FRACTION(dp->time - lasttime, 60),
							 FRACTION(dp->time, 60),
							 gasname(&gasmix));
					}
					len += snprintf(buffer + len, sz_buffer - len, "%s<br>", temp);
				}
				newdepth = dp->depth.mm;
				lasttime = dp->time;
			} else {
				if ((nextdp && dp->depth.mm != nextdp->depth.mm) || gaschange_after) {
					if (dp->setpoint) {
						snprintf(temp, sz_temp, translate("gettextFromC", "Stay at %.*f %s for %d:%02d min - runtime %d:%02u on %s (SP = %.1fbar)"),
							decimals, depthvalue, depth_unit,
							FRACTION(dp->time - lasttime, 60),
							FRACTION(dp->time, 60),
							gasname(&gasmix),
							(double) dp->setpoint / 1000.0);
					} else {
						snprintf(temp, sz_temp, translate("gettextFromC", "Stay at %.*f %s for %d:%02d min - runtime %d:%02u on %s"),
							decimals, depthvalue, depth_unit,
							FRACTION(dp->time - lasttime, 60),
							FRACTION(dp->time, 60),
							gasname(&gasmix));
					}
					len += snprintf(buffer + len, sz_buffer - len, "%s<br>", temp);
					newdepth = dp->depth.mm;
					lasttime = dp->time;
				}
			}
		} else {
			/* When not displaying the verbatim dive plan, we typically ignore ascents between deco stops,
			 * unless the display transitions option has been selected.  We output a segment if any of the
			 * following conditions are met.
			 * 1) Display transitions is selected
			 * 2) The segment was manually entered
			 * 3) It is the last segment of the dive
			 * 4) The segment is not an ascent, there was a gas change at the start of the segment and the next segment
			 *    is a change in depth (typical deco stop)
			 * 5) There is a gas change at the end of the segment and the last segment was entered (first calculated
			 *    segment if it ends in a gas change)
			 * 6) There is a gaschange after but no ascent.  This should only occur when backgas breaks option is selected
			 * 7) It is an ascent ending with a gas change, but is not followed by a stop.   As case 5 already matches
			 *    the first calculated ascent if it ends with a gas change, this should only occur if a travel gas is
			 *    used for a calculated ascent, there is a subsequent gas change before the first deco stop, and zero
			 *    time has been allowed for a gas switch.
			 */
			if (plan_display_transitions || dp->entered || !dp->next ||
			    (nextdp && dp->depth.mm != nextdp->depth.mm) ||
			    (!isascent && gaschange_before && nextdp && dp->depth.mm != nextdp->depth.mm) ||
			    (gaschange_after && lastentered) || (gaschange_after && !isascent) ||
			    (isascent && gaschange_after && nextdp && dp->depth.mm != nextdp->depth.mm ) ||
			    (lastentered && !dp->entered)) {
				// Print a symbol to indicate whether segment is an ascent, descent, constant depth (user entered) or deco stop
				if (isascent)
					segmentsymbol = "&#10138;"; // up-right arrow for ascent
				else if (dp->depth.mm > lastdepth)
					segmentsymbol = "&#10136;"; // down-right arrow for descent
				else if (dp->entered)
					segmentsymbol = "&#10137;"; // right arrow for entered entered segment at constant depth
				else
					segmentsymbol = "-";        // minus sign (a.k.a. horizontal line) for deco stop

				len += snprintf(buffer + len, sz_buffer - len, "<tr><td style='padding-left: 10px; float: right;'>%s</td>", segmentsymbol);

				snprintf(temp, sz_temp, translate("gettextFromC", "%3.0f%s"), depthvalue, depth_unit);
				len += snprintf(buffer + len, sz_buffer - len, "<td style='padding-left: 10px; float: right;'>%s</td>", temp);
				if (plan_display_duration) {
					snprintf(temp, sz_temp, translate("gettextFromC", "%3dmin"), (dp->time - lasttime + 30) / 60);
					len += snprintf(buffer + len, sz_buffer - len, "<td style='padding-left: 10px; float: right;'>%s</td>", temp);
				}
				if (plan_display_runtime) {
					snprintf(temp, sz_temp, translate("gettextFromC", "%3dmin"), (dp->time + 30) / 60);
					len += snprintf(buffer + len, sz_buffer - len, "<td style='padding-left: 10px; float: right;'>%s</td>", temp);
				}

				/* Normally a gas change is displayed on the stopping segment, so only display a gas change at the end of
				 * an ascent segment if it is not followed by a stop
				 */
				if ((isascent || dp->entered) && gaschange_after && dp->next && nextdp && (dp->depth.mm != nextdp->depth.mm || nextdp->entered)) {
					if (dp->setpoint) {
						snprintf(temp, sz_temp, translate("gettextFromC", "(SP = %.1fbar)"), (double) nextdp->setpoint / 1000.0);
						len += snprintf(buffer + len, sz_buffer - len, "<td style='padding-left: 10px; color: red; float: left;'><b>%s %s</b></td>",
							gasname(&newgasmix), temp);
					} else {
						len += snprintf(buffer + len, sz_buffer - len, "<td style='padding-left: 10px; color: red; float: left;'><b>%s</b></td>",
							gasname(&newgasmix));
						if (isascent && (get_he(&lastprintgasmix) > 0)) {		// For a trimix gas change on ascent, save ICD info if previous cylinder had helium
							if (isobaric_counterdiffusion(&lastprintgasmix, &newgasmix, &icdvalues)) 					// Do icd calulations
								icdwarning = true;
							icdlen += add_icd_entry(icdbuffer+icdlen, sz_icdbuf-icdlen, &icdvalues, dp->time, depth_to_mbar(dp->depth.mm, dive), &lastprintgasmix, &newgasmix); // Print calculations to buffer.
						}
					}
					lastprintsetpoint = nextdp->setpoint;
					lastprintgasmix = newgasmix;
					gaschange_after = false;
				} else if (gaschange_before) {
					// If a new gas has been used for this segment, now is the time to show it
					if (dp->setpoint) {
						snprintf(temp, sz_temp, translate("gettextFromC", "(SP = %.1fbar)"), (double) dp->setpoint / 1000.0);
						len += snprintf(buffer + len, sz_buffer - len, "<td style='padding-left: 10px; color: red; float: left;'><b>%s %s</b></td>",
							gasname(&gasmix), temp);
					} else {
						len += snprintf(buffer + len, sz_buffer - len, "<td style='padding-left: 10px; color: red; float: left;'><b>%s</b></td>",
							gasname(&gasmix));
						if (get_he(&lastprintgasmix) > 0) {						// For a trimix gas change, save ICD info if previous cylinder had helium
							if (isobaric_counterdiffusion(&lastprintgasmix, &gasmix, &icdvalues))			// Do icd calculations
								icdwarning = true;
							icdlen += add_icd_entry(icdbuffer+icdlen, sz_icdbuf-icdlen, &icdvalues, lasttime, depth_to_mbar(dp->depth.mm, dive), &lastprintgasmix, &gasmix); // and print them to buffer.
						} 
					}
					// Set variables so subsequent iterations can test against the last gas printed
					lastprintsetpoint = dp->setpoint;
					lastprintgasmix = gasmix;
					gaschange_after = false;
				} else {
					len += snprintf(buffer + len, sz_buffer - len, "<td>&nbsp;</td>");
				}
				len += snprintf(buffer + len, sz_buffer - len, "</tr>");
				newdepth = dp->depth.mm;
				lasttime = dp->time;
			}
		}
		if (gaschange_after) {
			// gas switch at this waypoint for verbatim
			if (plan_verbatim) {
				if (lastsetpoint >= 0) {
					if (nextdp && nextdp->setpoint) {
						snprintf(temp, sz_temp, translate("gettextFromC", "Switch gas to %s (SP = %.1fbar)"), gasname(&newgasmix), (double) nextdp->setpoint / 1000.0);
					} else {
						snprintf(temp, sz_temp, translate("gettextFromC", "Switch gas to %s"), gasname(&newgasmix));
						if ((isascent) && (get_he(&lastprintgasmix) > 0)) {										//	For a trimix gas change on ascent:
							if (isobaric_counterdiffusion(&lastprintgasmix, &newgasmix, &icdvalues))				// Do icd calculations
								icdwarning = true;
							icdlen += add_icd_entry(icdbuffer+icdlen, sz_icdbuf-icdlen, &icdvalues, dp->time, depth_to_mbar(dp->depth.mm, dive), &lastprintgasmix, &newgasmix); // and print them to buffer.
						}
					}
					len += snprintf(buffer + len, sz_buffer - len, "%s<br>", temp);
				}
				lastprintgasmix = newgasmix;
				gaschange_after = false;
				gasmix = newgasmix;
			}
		}
		lastprintdepth = newdepth;
		lastdepth = dp->depth.mm;
		lastsetpoint = dp->setpoint;
		lastentered = dp->entered;
	} while ((dp = nextdp) != NULL);
	if (!plan_verbatim)
		len += snprintf(buffer + len, sz_buffer - len, "</tbody></table><br>");

	/* Print the CNS and OTU next.*/
	dive->cns = 0;
	dive->maxcns = 0;
	update_cylinder_related_info(dive);
	snprintf(temp, sz_temp, "%s", translate("gettextFromC", "CNS"));
	len += snprintf(buffer + len, sz_buffer - len, "<div>%s: %i%%", temp, dive->cns);
	snprintf(temp, sz_temp, "%s", translate("gettextFromC", "OTU"));
	len += snprintf(buffer + len, sz_buffer - len, "<br>%s: %i<br></div>", temp, dive->otu);

	/* Print the settings for the diveplan next. */
	if (decoMode() == BUEHLMANN) {
		snprintf(temp, sz_temp, translate("gettextFromC", "Deco model: Bühlmann ZHL-16C with GFLow = %d%% and GFHigh = %d%%"),
			diveplan->gflow, diveplan->gfhigh);
	} else if (decoMode() == VPMB){
		int temp_len;
		if (diveplan->vpmb_conservatism == 0)
			temp_len = snprintf(temp, sz_temp, "%s", translate("gettextFromC", "Deco model: VPM-B at nominal conservatism"));
		else
			temp_len = snprintf(temp, sz_temp, translate("gettextFromC", "Deco model: VPM-B at +%d conservatism"), diveplan->vpmb_conservatism);
		if (diveplan->eff_gflow)
			temp_len += snprintf(temp + temp_len, sz_temp - temp_len,  translate("gettextFromC", ", effective GF=%d/%d"), diveplan->eff_gflow,
				diveplan->eff_gfhigh);

	} else if (decoMode() == RECREATIONAL){
		snprintf(temp, sz_temp, translate("gettextFromC", "Deco model: Recreational mode based on Bühlmann ZHL-16B with GFLow = %d%% and GFHigh = %d%%"),
			diveplan->gflow, diveplan->gfhigh);
	}
	len += snprintf(buffer + len, sz_buffer - len, "<div>%s<br>",temp);

	const char *depth_unit;
	int altitude = (int) get_depth_units((int) (log(1013.0 / diveplan->surface_pressure) * 7800000), NULL, &depth_unit);

	len += snprintf(buffer + len, sz_buffer - len, translate("gettextFromC", "ATM pressure: %dmbar (%d%s)<br></div>"),
		diveplan->surface_pressure, altitude, depth_unit);

	/* Get SAC values and units for printing it in gas consumption */
	double bottomsacvalue, decosacvalue;
	int sacdecimals;
	const char* sacunit;

	bottomsacvalue = get_volume_units(prefs.bottomsac, &sacdecimals, &sacunit);
	decosacvalue = get_volume_units(prefs.decosac, NULL, NULL);

	/* Reduce number of decimals from 1 to 0 for bar/min, keep 2 for cuft/min */
	if (sacdecimals==1) sacdecimals--;

	/* Print the gas consumption next.*/
	if (dive->dc.divemode == CCR)
		snprintf(temp, sz_temp, "%s", translate("gettextFromC", "Gas consumption (CCR legs excluded):"));
	else
		snprintf(temp, sz_temp, "%s %.*f|%.*f%s/min):", translate("gettextFromC", "Gas consumption (based on SAC"),
			sacdecimals, bottomsacvalue, sacdecimals, decosacvalue, sacunit);
	len += snprintf(buffer + len, sz_buffer - len, "<div>%s<br>", temp);

	/* Print gas consumption: This loop covers all cylinders */
	for (int gasidx = 0; gasidx < MAX_CYLINDERS; gasidx++) {
		double volume, pressure, deco_volume, deco_pressure, mingas_volume, mingas_pressure, mingas_d_pressure, mingas_depth;
		const char *unit, *pressure_unit, *depth_unit;
		char warning[1000] = "";
		char mingas[1000] = "";
		cylinder_t *cyl = &dive->cylinder[gasidx];
		if (cylinder_none(cyl))
			break;

		volume = get_volume_units(cyl->gas_used.mliter, NULL, &unit);
		deco_volume = get_volume_units(cyl->deco_gas_used.mliter, NULL, &unit);
		if (cyl->type.size.mliter) {
			int remaining_gas = lrint((double)cyl->end.mbar * cyl->type.size.mliter / 1000.0 / gas_compressibility_factor(&cyl->gasmix, cyl->end.mbar / 1000.0));
			double deco_pressure_mbar = isothermal_pressure(&cyl->gasmix, 1.0, remaining_gas + cyl->deco_gas_used.mliter,
				cyl->type.size.mliter) * 1000 - cyl->end.mbar;
			deco_pressure = get_pressure_units(lrint(deco_pressure_mbar), &pressure_unit);
			pressure = get_pressure_units(cyl->start.mbar - cyl->end.mbar, &pressure_unit);
			/* Warn if the plan uses more gas than is available in a cylinder
			 * This only works if we have working pressure for the cylinder
			 * 10bar is a made up number - but it seemed silly to pretend you could breathe cylinder down to 0 */
			if (cyl->end.mbar < 10000)
				snprintf(warning, sizeof(warning), "<br>&nbsp;&mdash; <span style='color: red;'>%s </span> %s",
					translate("gettextFromC", "Warning:"),
					translate("gettextFromC", "this is more gas than available in the specified cylinder!"));
			else
				if (cyl->end.mbar / 1000.0 * cyl->type.size.mliter / gas_compressibility_factor(&cyl->gasmix, cyl->end.mbar / 1000.0)
				    < cyl->deco_gas_used.mliter)
					snprintf(warning, sizeof(warning), "<br>&nbsp;&mdash; <span style='color: red;'>%s </span> %s",
						translate("gettextFromC", "Warning:"),
						translate("gettextFromC", "not enough reserve for gas sharing on ascent!"));

			/* Do and print minimum gas calculation for last bottom gas, but only for OC mode, */
			/* not for recreational mode and if no other warning was set before. */
			else
				if (lastbottomdp && gasidx == lastbottomdp->cylinderid
					&& dive->dc.divemode == OC && decoMode() != RECREATIONAL) {
					/* Calculate minimum gas volume. */
					volume_t mingasv;
					mingasv.mliter = lrint(prefs.sacfactor / 100.0 * prefs.problemsolvingtime * prefs.bottomsac
						* depth_to_bar(lastbottomdp->depth.mm, dive)
						+ prefs.sacfactor / 100.0 * cyl->deco_gas_used.mliter);
					/* Calculate minimum gas pressure for cyclinder. */
					lastbottomdp->minimum_gas.mbar = lrint(isothermal_pressure(&cyl->gasmix, 1.0,
						mingasv.mliter, cyl->type.size.mliter) * 1000);
					/* Translate all results into correct units */
					mingas_volume = get_volume_units(mingasv.mliter, NULL, &unit);
					mingas_pressure = get_pressure_units(lastbottomdp->minimum_gas.mbar, &pressure_unit);
					mingas_d_pressure = get_pressure_units(lrint((double)cyl->end.mbar + deco_pressure_mbar - lastbottomdp->minimum_gas.mbar), &pressure_unit);
					mingas_depth = get_depth_units(lastbottomdp->depth.mm, NULL, &depth_unit);
					/* Print it to results */
					if (cyl->start.mbar > lastbottomdp->minimum_gas.mbar) {
						snprintf(mingas, sizeof(mingas), "<br>&nbsp;&mdash; <span style='color: %s;'>%s</span> (%s %.1fx%s/+%d%s@%.0f%s): "
							"%.0f%s/%.0f%s<span style='color: %s;'>/&Delta;:%+.0f%s</span>",
							mingas_d_pressure > 0 ? "green" :"red",
							translate("gettextFromC", "Minimum gas"),
							translate("gettextFromC", "based on"),
							prefs.sacfactor / 100.0,
							translate("gettextFromC", "SAC"),
							prefs.problemsolvingtime,
							translate("gettextFromC", "min"),
							mingas_depth, depth_unit,
							mingas_volume, unit,
							mingas_pressure, pressure_unit,
							mingas_d_pressure > 0 ? "grey" :"indianred",
							mingas_d_pressure, pressure_unit);
					} else {
						snprintf(warning, sizeof(warning), "<br>&nbsp;&mdash; <span style='color: red;'>%s </span> %s",
							translate("gettextFromC", "Warning:"),
							translate("gettextFromC", "required minimum gas for ascent already exceeding start pressure of cylinder!"));
					}
				}
			/* Print the gas consumption for every cylinder here to temp buffer. */
			if (lrint(volume) > 0) {
				snprintf(temp, sz_temp, translate("gettextFromC", "%.0f%s/%.0f%s of <span style='color: red;'><b>%s</b></span> (%.0f%s/%.0f%s in planned ascent)"),
					volume, unit, pressure, pressure_unit, gasname(&cyl->gasmix), deco_volume, unit, deco_pressure, pressure_unit);
			} else {
				snprintf(temp, sz_temp, translate("gettextFromC", "%.0f%s/%.0f%s of <span style='color: red;'><b>%s</b></span>"),
					volume, unit, pressure, pressure_unit, gasname(&cyl->gasmix));
			}
		} else {
			if (lrint(volume) > 0) {
				snprintf(temp, sz_temp, translate("gettextFromC", "%.0f%s of <span style='color: red;'><b>%s</b></span> (%.0f%s during planned ascent)"),
					volume, unit, gasname(&cyl->gasmix), deco_volume, unit);
			} else {
				snprintf(temp, sz_temp, translate("gettextFromC", "%.0f%s of <span style='color: red;'><b>%s</b></span>"),
					volume, unit, gasname(&cyl->gasmix));
			}
		}
		/* Gas consumption: Now finally print all strings to output */
		len += snprintf(buffer + len, sz_buffer - len, "%s%s%s<br></div>", temp, warning, mingas);
	}

	/* For trimix OC dives, add the ICD table here */
	if (istrimix) {
		icdlen += snprintf(icdbuffer + icdlen, sz_icdbuf - icdlen,"</tbody></table>");	// End the ICD table
		len += snprintf(buffer + len, sz_buffer - len, "%s", icdbuffer);				// ..and add it to the html buffer
		if (icdwarning) {																// If necessary, add a warning message to html buffer
			len += snprintf(buffer + len, sz_buffer - len, "<span style='color: red;'>%s</span> %s",
				translate("gettextFromC", "Warning:"),
				translate("gettextFromC", "Isobaric counterdiffusion conditions exceeded"));
		}
		len += snprintf(buffer + len, sz_buffer - len, "<br></div>");
	}

	/* Print warnings for pO2 */
	dp = diveplan->dp;
	bool o2warning_exist = false;
	if (dive->dc.divemode != CCR) {
		while (dp) {
			if (dp->time != 0) {
				struct gas_pressures pressures;
				struct gasmix *gasmix = &dive->cylinder[dp->cylinderid].gasmix;
				fill_pressures(&pressures, depth_to_atm(dp->depth.mm, dive), gasmix, 0.0, dive->dc.divemode);

				if (pressures.o2 > (dp->entered ? prefs.bottompo2 : prefs.decopo2) / 1000.0) {
					const char *depth_unit;
					int decimals;
					double depth_value = get_depth_units(dp->depth.mm, &decimals, &depth_unit);
					len = strlen(buffer);
					if (!o2warning_exist)
						len += snprintf(buffer + len, sz_buffer - len, "<div>");
					o2warning_exist = true;
					snprintf(temp, sz_temp,
						 translate("gettextFromC", "high pO₂ value %.2f at %d:%02u with gas %s at depth %.*f %s"),
						 pressures.o2, FRACTION(dp->time, 60), gasname(gasmix), decimals, depth_value, depth_unit);
					len += snprintf(buffer + len, sz_buffer - len, "<span style='color: red;'>%s </span> %s<br>",
							translate("gettextFromC", "Warning:"), temp);
				} else if (pressures.o2 < 0.16) {
					const char *depth_unit;
					int decimals;
					double depth_value = get_depth_units(dp->depth.mm, &decimals, &depth_unit);
					len = strlen(buffer);
					if (!o2warning_exist)
						len += snprintf(buffer + len, sz_buffer - len, "<div>");
					o2warning_exist = true;
					snprintf(temp, sz_temp,
						 translate("gettextFromC", "low pO₂ value %.2f at %d:%02u with gas %s at depth %.*f %s"),
						 pressures.o2, FRACTION(dp->time, 60), gasname(gasmix), decimals, depth_value, depth_unit);
					len += snprintf(buffer + len, sz_buffer - len, "<span style='color: red;'>%s </span> %s<br>",
							translate("gettextFromC", "Warning:"), temp);

				}
			}
			dp = dp->next;
		}
	}
	snprintf(buffer + len, sz_buffer - len, "</div>");
	dive->notes = strdup(buffer);

finished:
	free((void *)buffer);
	free((void *)temp);
	free((void *)icdbuffer);

}
