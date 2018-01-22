// SPDX-License-Identifier: GPL-2.0
#include "profile-widget/diveeventitem.h"
#include "qt-models/diveplotdatamodel.h"
#include "profile-widget/divecartesianaxis.h"
#include "profile-widget/animationfunctions.h"
#include "core/libdivecomputer.h"
#include "core/profile.h"
#include "core/gettextfromc.h"
#include "core/metrics.h"
#include "core/membuffer.h"

extern struct ev_select *ev_namelist;
extern int evn_used;

DiveEventItem::DiveEventItem(QObject *parent) : DivePixmapItem(parent),
	vAxis(NULL),
	hAxis(NULL),
	dataModel(NULL),
	internalEvent(NULL)
{
	setFlag(ItemIgnoresTransformations);
}

DiveEventItem::~DiveEventItem()
{
	free(internalEvent);
}

void DiveEventItem::setHorizontalAxis(DiveCartesianAxis *axis)
{
	hAxis = axis;
	recalculatePos(true);
}

void DiveEventItem::setModel(DivePlotDataModel *model)
{
	dataModel = model;
	recalculatePos(true);
}

void DiveEventItem::setVerticalAxis(DiveCartesianAxis *axis)
{
	vAxis = axis;
	recalculatePos(true);
	connect(vAxis, SIGNAL(sizeChanged()), this, SLOT(recalculatePos()));
}

struct event *DiveEventItem::getEvent()
{
	return internalEvent;
}

void DiveEventItem::setEvent(struct event *ev, struct gasmix *lastgasmix)
{
	if (!ev)
		return;

	free(internalEvent);
	internalEvent = clone_event(ev);
	setupPixmap(lastgasmix);
	setupToolTipString(lastgasmix);
	recalculatePos(true);
}

void DiveEventItem::setupPixmap(struct gasmix *lastgasmix)
{
	const IconMetrics& metrics = defaultIconMetrics();
#ifndef SUBSURFACE_MOBILE
	int sz_bigger = metrics.sz_med + metrics.sz_small; // ex 40px
#else
#if defined(Q_OS_IOS)
	 // on iOS devices we need to adjust for Device Pixel Ratio
	int sz_bigger = metrics.sz_med  * metrics.dpr;
#else
	// SUBSURFACE_MOBILE, seems a little big from the code,
	// but looks fine on device
	int sz_bigger = metrics.sz_big + metrics.sz_med;
#endif
#endif
	int sz_pix = sz_bigger/2; // ex 20px

#define EVENT_PIXMAP(PIX) QPixmap(QString(PIX)).scaled(sz_pix, sz_pix, Qt::KeepAspectRatio, Qt::SmoothTransformation)
#define EVENT_PIXMAP_BIGGER(PIX) QPixmap(QString(PIX)).scaled(sz_bigger, sz_bigger, Qt::KeepAspectRatio, Qt::SmoothTransformation)
	if (empty_string(internalEvent->name)) {
		setPixmap(EVENT_PIXMAP(":status-warning-icon"));
	} else if (internalEvent->type == SAMPLE_EVENT_BOOKMARK) {
		setPixmap(EVENT_PIXMAP(":dive-bookmark-icon"));
	} else if (event_is_gaschange(internalEvent)) {
		struct gasmix *mix = get_gasmix_from_event(&displayed_dive, internalEvent);
		struct icd_data icd_data;
		bool icd = isobaric_counterdiffusion(lastgasmix, mix, &icd_data);
		if (mix->he.permille) {
			if (icd)
				setPixmap(EVENT_PIXMAP_BIGGER(":gaschange-trimix-ICD-icon"));
			else
				setPixmap(EVENT_PIXMAP_BIGGER(":gaschange-trimix-icon"));
		} else if (gasmix_is_air(mix)) {
			if (icd)
				setPixmap(EVENT_PIXMAP_BIGGER(":gaschange-air-ICD-icon"));
			else
				setPixmap(EVENT_PIXMAP_BIGGER(":gaschange-air-icon"));
		} else if (mix->o2.permille == 1000) {
			if (icd)
				setPixmap(EVENT_PIXMAP_BIGGER(":gaschange-oxygen-ICD-icon"));
			else
				setPixmap(EVENT_PIXMAP_BIGGER(":gaschange-oxygen-icon"));
		} else {
			if (icd)
				setPixmap(EVENT_PIXMAP_BIGGER(":gaschange-ean-ICD-icon"));
			else
				setPixmap(EVENT_PIXMAP_BIGGER(":gaschange-ean-icon"));
		}
#ifdef SAMPLE_FLAGS_SEVERITY_SHIFT
	} else if ((((internalEvent->flags & SAMPLE_FLAGS_SEVERITY_MASK) >> SAMPLE_FLAGS_SEVERITY_SHIFT) == 1) ||
		    // those are useless internals of the dive computer
#else
	} else if (
#endif
		   same_string_caseinsensitive(internalEvent->name, "heading") ||
		   (same_string_caseinsensitive(internalEvent->name, "SP change") && internalEvent->time.seconds == 0)) {
		// 2 cases:
		// a) some dive computers have heading in every sample
		// b) at t=0 we might have an "SP change" to indicate dive type
		// in both cases we want to get the right data into the tooltip but don't want the visual clutter
		// so set an "almost invisible" pixmap (a narrow but somewhat tall, basically transparent pixmap)
		// that allows tooltips to work when we don't want to show a specific
		// pixmap for an event, but want to show the event value in the tooltip
		QPixmap transparentPixmap(4, 20);
		transparentPixmap.fill(QColor::fromRgbF(1.0, 1.0, 1.0, 0.01));
		setPixmap(transparentPixmap);
#ifdef SAMPLE_FLAGS_SEVERITY_SHIFT
	} else if (((internalEvent->flags & SAMPLE_FLAGS_SEVERITY_MASK) >> SAMPLE_FLAGS_SEVERITY_SHIFT) == 2) {
		setPixmap(EVENT_PIXMAP(":status-info-icon"));
	} else if (((internalEvent->flags & SAMPLE_FLAGS_SEVERITY_MASK) >> SAMPLE_FLAGS_SEVERITY_SHIFT) == 3) {
		setPixmap(EVENT_PIXMAP(":status-warning-icon"));
	} else if (((internalEvent->flags & SAMPLE_FLAGS_SEVERITY_MASK) >> SAMPLE_FLAGS_SEVERITY_SHIFT) == 4) {
		setPixmap(EVENT_PIXMAP(":status-violation-icon"));
#endif
	} else if (same_string_caseinsensitive(internalEvent->name, "violation") || // generic libdivecomputer
		   same_string_caseinsensitive(internalEvent->name, "Safety stop violation")  || // the rest are from the Uemis downloader
		   same_string_caseinsensitive(internalEvent->name, "pO₂ ascend alarm")  ||
		   same_string_caseinsensitive(internalEvent->name, "RGT alert")  ||
		   same_string_caseinsensitive(internalEvent->name, "Dive time alert")  ||
		   same_string_caseinsensitive(internalEvent->name, "Low battery alert")  ||
		   same_string_caseinsensitive(internalEvent->name, "Speed alarm")) {
		setPixmap(EVENT_PIXMAP(":status-violation-icon"));
	} else if (same_string_caseinsensitive(internalEvent->name, "non stop time") || // generic libdivecomputer
		   same_string_caseinsensitive(internalEvent->name, "safety stop") ||
		   same_string_caseinsensitive(internalEvent->name, "safety stop (voluntary)") ||
		   same_string_caseinsensitive(internalEvent->name, "Tank change suggested") || // Uemis downloader
		   same_string_caseinsensitive(internalEvent->name, "Marker")) {
		setPixmap(EVENT_PIXMAP(":status-info-icon"));
	} else {
		// we should do some guessing based on the type / name of the event;
		// for now they all get the warning icon
		setPixmap(EVENT_PIXMAP(":status-warning-icon"));
	}
#undef EVENT_PIXMAP
#undef EVENT_PIXMAP_BIGGER
}

void DiveEventItem::setupToolTipString(struct gasmix *lastgasmix)
{
	// we display the event on screen - so translate
	QString name = gettextFromC::instance()->tr(internalEvent->name);
	int value = internalEvent->value;
	int type = internalEvent->type;

	if (event_is_gaschange(internalEvent)) {
		struct icd_data icd_data;
		struct gasmix *mix = get_gasmix_from_event(&displayed_dive, internalEvent);
		struct membuffer mb = {};
		name += ": ";
		name += gasname(mix);

		/* Do we have an explicit cylinder index?  Show it. */
		if (internalEvent->gas.index >= 0)
			name += tr(" (cyl. %1)").arg(internalEvent->gas.index + 1);
		bool icd = isobaric_counterdiffusion(lastgasmix, mix, &icd_data);
		if (icd_data.dHe < 0) {
			put_format(&mb, "\n%s: %s=%+.3g%% %s=%+.3g%%%s%+.3g%%",
				tr("ICD").toUtf8().constData(),
				tr("ΔHe").toUtf8().constData(), icd_data.dHe / 10.0,
				tr("ΔN₂").toUtf8().constData(), icd_data.dN2 / 10.0,
				icd ? ">" : "<", lrint(-icd_data.dHe / 5.0) / 10.0);
			name += QString::fromUtf8(mb.buffer, mb.len);
		}
		*lastgasmix = *mix;
	} else if (value) {
		if (type == SAMPLE_EVENT_PO2 && same_string(internalEvent->name, "SP change")) {
			name += QString(": %1bar").arg((double)value / 1000, 0, 'f', 1);
		} else if (type == SAMPLE_EVENT_CEILING && same_string(internalEvent->name, "planned waypoint above ceiling")) {
			const char *depth_unit;
			double depth_value = get_depth_units(value*1000, NULL, &depth_unit);
			name += QString(": %1%2").arg((int) round(depth_value)).arg(depth_unit);
		} else {
			name += QString(": %1").arg(value);
		}
	} else if (type == SAMPLE_EVENT_PO2 && same_string(internalEvent->name, "SP change")) {
		// this is a bad idea - we are abusing an existing event type that is supposed to
		// warn of high or low pO₂ and are turning it into a setpoint change event
		name += ":\n" + tr("Manual switch to OC");
	} else {
		name += internalEvent->flags & SAMPLE_FLAGS_BEGIN ? tr(" begin", "Starts with space!") :
								    internalEvent->flags & SAMPLE_FLAGS_END ? tr(" end", "Starts with space!") : "";
	}
	setToolTip(name);
}

void DiveEventItem::eventVisibilityChanged(const QString &eventName, bool visible)
{
	//WARN: lookslike we should implement this.
	Q_UNUSED(eventName);
	Q_UNUSED(visible);
}

bool DiveEventItem::shouldBeHidden()
{
	struct event *event = internalEvent;
	struct dive *dive = &displayed_dive;
	struct divecomputer *dc = get_dive_dc(dive, dc_number);

	/*
	 * Some gas change events are special. Some dive computers just tell us the initial gas this way.
	 * Don't bother showing those
	 */
	struct sample *first_sample = &dc->sample[0];
	if (!strcmp(event->name, "gaschange") &&
	    (event->time.seconds == 0 ||
	     (first_sample && event->time.seconds == first_sample->time.seconds)))
		return true;

	/*
	 * Some divecomputers give "surface" events that just aren't interesting.
	 * Like at the beginning or very end of a dive. Well, duh.
	 */
	if (!strcmp(event->name, "surface")) {
		int time = event->time.seconds;
		if (time <= 30 || time + 30 >= (int)dc->duration.seconds)
			return true;
	}

	for (int i = 0; i < evn_used; i++) {
		if (!strcmp(event->name, ev_namelist[i].ev_name) && ev_namelist[i].plot_ev == false)
			return true;
	}
	return false;
}

void DiveEventItem::recalculatePos(bool instant)
{
	if (!vAxis || !hAxis || !internalEvent || !dataModel)
		return;

	QModelIndexList result = dataModel->match(dataModel->index(0, DivePlotDataModel::TIME), Qt::DisplayRole, internalEvent->time.seconds);
	if (result.isEmpty()) {
		Q_ASSERT("can't find a spot in the dataModel");
		hide();
		return;
	}
	if (!isVisible() && !shouldBeHidden())
		show();
	int depth = dataModel->data(dataModel->index(result.first().row(), DivePlotDataModel::DEPTH)).toInt();
	qreal x = hAxis->posAtValue(internalEvent->time.seconds);
	qreal y = vAxis->posAtValue(depth);
	if (!instant)
		Animations::moveTo(this, x, y);
	else
		setPos(x, y);
	if (isVisible() && shouldBeHidden())
		hide();
}
