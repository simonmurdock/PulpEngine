#include "AssetFxWidget.h"


#include <IAnimation.h>
#include <IEffect.h>

#include "IAssetEntry.h"

#include "ActionManager.h"
#include "ActionContainer.h"
#include "Command.h"

#include "Context.h"
#include "session.h"

#include <QtCharts\QChartView.h>
#include <QtCharts\Qchart.h>
#include <QtCharts\QLineSeries.h>
#include <QtCharts\qvalueaxis.h>

// #include <QLineSerie>
#include <QTableWidget>


using namespace QtCharts;


X_NAMESPACE_BEGIN(assman)


SpinBoxRange::SpinBoxRange(QWidget* parent) :
	QWidget(parent)
{
	QHBoxLayout* pLayout = new QHBoxLayout();
	pStart_ = new QSpinBox();
	pRange_ = new QSpinBox();

	pStart_->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	pRange_->setRange(0, std::numeric_limits<int>::max());

	QLabel* pLabel = new QLabel("+");

	pLayout->setContentsMargins(0, 0, 0, 0);
	pLayout->addWidget(pStart_, 1);
	pLayout->addWidget(pLabel, 0);
	pLayout->addWidget(pRange_, 1);
	setLayout(pLayout);

	connect(pStart_, &QSpinBox::editingFinished, this, &SpinBoxRange::valueChanged);
	connect(pRange_, &QSpinBox::editingFinished, this, &SpinBoxRange::valueChanged);
}

void SpinBoxRange::setValue(const Range& r)
{
	pStart_->setValue(r.start);
	pRange_->setValue(r.range);
}

void SpinBoxRange::getValue(Range& r)
{
	r.start = pStart_->value();
	r.range = pRange_->value();
}

// -----------------------------------


SpinBoxRangeDouble::SpinBoxRangeDouble(bool addative, QWidget* parent) :
	QWidget(parent),
	addative_(addative)
{
	QHBoxLayout* pLayout = new QHBoxLayout();
	pStart_ = new QDoubleSpinBox();
	pRange_ = new QDoubleSpinBox();
	pStart_->setSingleStep(0.05);
	pRange_->setSingleStep(0.05);

	pStart_->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	
	if (addative_) {
		pRange_->setRange(0, std::numeric_limits<int>::max());
	}
	else {
		pRange_->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
	}

	QLabel* pLabel = new QLabel(addative_ ? "+" : "to");

	pLayout->setContentsMargins(0, 0, 0, 0);
	pLayout->addWidget(pStart_, 1);
	pLayout->addWidget(pLabel, 0);
	pLayout->addWidget(pRange_, 1);
	setLayout(pLayout);

	connect(pStart_, &QSpinBox::editingFinished, this, &SpinBoxRangeDouble::valueChanged);
	connect(pRange_, &QSpinBox::editingFinished, this, &SpinBoxRangeDouble::valueChanged);
}

void SpinBoxRangeDouble::setValue(const RangeDouble& r)
{
	pStart_->setValue(r.start);
	pRange_->setValue(r.range);
}

void SpinBoxRangeDouble::getValue(RangeDouble& r)
{
	r.start = pStart_->value();
	r.range = pRange_->value();
}


// -----------------------------------

GraphEditorView::ResetGraph::ResetGraph(GraphEditorView* pView, Graph& graph) :
	pView_(pView),
	graph_(graph)
{
	graphPoints_.resize(graph.series.size());
	for (int32_t i = 0; i<graphPoints_.size(); i++)
	{
		graphPoints_[i] = graph.series[i]->pointsVector();
	}
}

void GraphEditorView::ResetGraph::redo(void)
{
	const auto minVal = pView_->getMinY();
	const auto maxVal = pView_->getMaxY();

	// reset all to default.
	for (int32_t i = 0; i < graphPoints_.size(); i++)
	{
		auto* pSeries = graph_.series[i];
		pSeries->clear();
		// what is default?
		pSeries->append(0, maxVal);
		pSeries->append(1, minVal);
	}

	pView_->onPointsChanged();
}

void GraphEditorView::ResetGraph::undo(void)
{
	for (int32_t i = 0; i < graphPoints_.size(); i++)
	{
		graph_.series[i]->replace(graphPoints_[i]);
	}

	pView_->onPointsChanged();
}

// -----------------------------------

GraphEditorView::ClearPoints::ClearPoints(GraphEditorView* pView, QtCharts::QLineSeries* pSeries) :
	pView_(pView),
	pSeries_(pSeries)
{
	points_ = pSeries->pointsVector();
}


void GraphEditorView::ClearPoints::redo(void)
{
	if (points_.size() > 0) {
		pSeries_->clear();
		pSeries_->append(points_.front());
		pSeries_->append(points_.back());
	}

	pView_->onPointsChanged();
}

void GraphEditorView::ClearPoints::undo(void)
{
	pSeries_->replace(points_);

	pView_->onPointsChanged();
}


// -----------------------------------

GraphEditorView::AddPoint::AddPoint(GraphEditorView* pView, Graph& graph, int32_t index, QPointF point) :
	pView_(pView),
	graph_(graph),
	index_(index),
	point_(point)
{

}


void GraphEditorView::AddPoint::redo(void)
{
	for (auto* pSeries : graph_.series)
	{
		pSeries->insert(index_, point_);
	}
	
	pView_->onPointsChanged();
}

void GraphEditorView::AddPoint::undo(void)
{
	for (auto* pSeries : graph_.series)
	{
		pSeries->remove(index_);
	}

	pView_->onPointsChanged();
}

// -----------------------------------


GraphEditorView::MovePoint::MovePoint(GraphEditorView* pView, Graph& graph, int32_t activeSeries, int32_t index, QPointF delta) :
	pView_(pView),
	graph_(graph),
	delta_(delta),
	activeSeries_(activeSeries),
	index_(index)
{
}

void GraphEditorView::MovePoint::redo(void)
{
	auto& s = graph_.series;
	for (int32_t i = 0; i < s.size(); i++)
	{
		auto pos = s[i]->at(index_);
		if (i == activeSeries_)
		{
			pos += delta_;
		}
		else
		{
			pos.setX(pos.x() + delta_.x());
		}

		s[i]->replace(index_, pos);
	}

	pView_->onPointsChanged();
}

void GraphEditorView::MovePoint::undo(void)
{
	auto& s = graph_.series;
	for (int32_t i = 0; i < s.size(); i++)
	{
		auto pos = s[i]->at(index_);
		if (i == activeSeries_)
		{
			pos -= delta_;
		}
		else
		{
			pos.setX(pos.x() - delta_.x());
		}

		s[i]->replace(index_, pos);
	}

	pView_->onPointsChanged();
}

int GraphEditorView::MovePoint::id(void) const
{
	return 1;
}

bool GraphEditorView::MovePoint::mergeWith(const QUndoCommand* pOth)
{
	if (pOth->id() != id()) {
		return false;
	}

	delta_ += static_cast<const MovePoint*>(pOth)->delta_;
	return true;
}

// -----------------------------------




// -----------------------------------



GraphEditorView::GraphEditorView(QWidget *parent) :
	QChartView(parent),
	mouseActive_(false),
	singleActiveSeries_(false),
	dirty_(false),
	activePoint_(-1),
	activeSeries_(-1),
	activeGraph_(-1),
	hoverSeries_(-1)
{
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QChartView::customContextMenuRequested, this, &GraphEditorView::showContextMenu);

	pUndoStack_ = new QUndoStack();

	{
		Context context(Constants::C_GRAPH_EDITOR);
		Context context1(Constants::C_GRAPH_EDITOR);

		auto test = context.at(0);

		pContext_ = new IContext(this);
		pContext_->setContext(context);
		pContext_->setWidget(this);
		ICore::addContextObject(pContext_);
		
		pUndoAction_ = pUndoStack_->createUndoAction(this, tr("&Undo"));
		pUndoAction_->setShortcut(QKeySequence::Undo);
		pUndoAction_->setShortcutVisibleInContextMenu(true);
		pUndoAction_->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);

		pRedoAction_ = pUndoStack_->createRedoAction(this, tr("&Redo"));
		pRedoAction_->setShortcuts(QKeySequence::Redo);
		pRedoAction_->setShortcutVisibleInContextMenu(true);
		pUndoAction_->setShortcutContext(Qt::ShortcutContext::WidgetWithChildrenShortcut);


		connect(pUndoStack_, &QUndoStack::canUndoChanged, pUndoAction_, &QAction::setEnabled);
		connect(pUndoStack_, &QUndoStack::canRedoChanged, pRedoAction_, &QAction::setEnabled);

		addAction(pUndoAction_);
		addAction(pRedoAction_);
	}

	// setRenderHint(QPainter::Antialiasing);

	pChart_ = new QtCharts::QChart();
	pChart_->legend()->hide();
	pChart_->setMargins(QMargins(1, 1, 1, 1));
	pChart_->layout()->setContentsMargins(0, 0, 0, 0);
	pChart_->setBackgroundRoundness(0);
	pChart_->setBackgroundBrush(QBrush(QRgb(0x2D2D30)));

	pAxisX_ = new QValueAxis();
	pAxisY_ = new QValueAxis();
	
	QBrush axisBrush(Qt::darkGray);
	pAxisX_->setLabelsBrush(axisBrush);
	pAxisY_->setLabelsBrush(axisBrush);

	pAxisX_->setLinePenColor(Qt::darkGray);
	pAxisY_->setLinePenColor(Qt::darkGray);
	
	pAxisX_->setTickCount(5);
	pAxisY_->setTickCount(5);
	pAxisX_->setRange(0, 1);
	pAxisY_->setRange(0, 1);
	pAxisX_->setGridLineVisible(true);
	pAxisY_->setGridLineVisible(true);
	
	pAxisX_->setGridLineColor(QColor(QRgb(0x404040)));
	pAxisY_->setGridLineColor(QColor(QRgb(0x404040)));

	pChart_->addAxis(pAxisY_, Qt::AlignLeft);
	pChart_->addAxis(pAxisX_, Qt::AlignBottom);

	setChart(pChart_);
}


void GraphEditorView::setValue(const GraphInfo& g)
{
	// for every graph update series.
	for (size_t i = 0; i < graphs_.size(); i++)
	{
		auto& srcG = g.graphs[i];
		auto& dstG = graphs_[i];

		for (size_t j = 0; j < srcG.series.size(); j++)
		{
			auto* pSeries = dstG.series[j];
			pSeries->clear();

			const auto& srcSeries = srcG.series[j];
			for (size_t p = 0; p < srcSeries.points.size(); p++)
			{
				const GraphPoint& point = srcSeries.points[p];
				pSeries->append(point.pos, point.val);
			}
		}
	}
}

void GraphEditorView::getValue(GraphInfo& g)
{
	g.graphs.resize(graphs_.size());

	for (size_t i = 0; i < graphs_.size(); i++)
	{
		const auto& srcG = graphs_[i];
		auto& dstG = g.graphs[i];

		for (size_t j = 0; j < srcG.series.size(); j++)
		{
			const auto* pSeries = srcG.series[j];
			auto points = pSeries->pointsVector();

			auto& dstSeries = dstG.series[j];
			dstSeries.points.resize(pSeries->count());

			for (int32_t p = 0; p < points.count(); p++)
			{
				GraphPoint& point = dstSeries.points[p];
				point.pos = points[p].rx();
				point.val = points[p].ry();
			}
		}
	}
}


void GraphEditorView::createGraphs(int32_t numGraphs, int32_t numSeries)
{
	if (numGraphs < 1 || numSeries < 1) {
		return;
	}
	
	activeSeries_ = 0;
	activeGraph_ = 0;

	names_.resize(numSeries);
	colors_.resize(numSeries);
	graphs_.resize(numGraphs);

	// set everything to default col disabled.
	QColor defaultCol(QRgb(0x900000));
	for (auto& col : colors_)
	{
		col = defaultCol;
	}

	QPen pen;
	pen.setWidth(LineWidth);
	pen.setStyle(Qt::SolidLine);

	const auto minVal = pAxisY_->min();
	const auto maxVal = pAxisY_->max();

	for (size_t g=0; g<graphs_.size(); g++)
	{
		auto& graph = graphs_[g];
		graph.series.resize(numSeries);
		
		if (g == 1)
		{
			pen.setStyle(DisabledPenStyle);
		}

		for (int32_t i = 0; i < numSeries; i++)
		{
			if (i == 0)
			{
				pen.setColor(colors_[i]);
			}
			else
			{
				pen.setColor(colors_[i].darker(DarkenFactor));
			}

			if (singleActiveSeries_ && i == 1)
			{
				pen.setStyle(DisabledPenStyle);
			}

			QLineSeries* pSeries = new QLineSeries();
			pSeries->append(0, maxVal);
			pSeries->append(1, minVal);
			pSeries->setPointsVisible(true);
			pSeries->setPen(pen);
			pChart_->addSeries(pSeries);

			pSeries->attachAxis(pAxisX_);
			pSeries->attachAxis(pAxisY_);

			connect(pSeries, &QLineSeries::hovered, this, &GraphEditorView::seriesHover);

			// reverse them, so index zero is last added to chart.
			graph.series[(numSeries-1) - i] = pSeries;
		}
	}
}

void GraphEditorView::setGraphName(int32_t i, const QString& name)
{
	graphs_[i].name = name;
}

void GraphEditorView::setSeriesName(int32_t i, const QString& name)
{
	names_[i] = name;
}

void GraphEditorView::setSeriesColor(int32_t i, const QColor& col)
{
	colors_[i] = col;

	QPen pen;
	pen.setWidth(LineWidth);
	pen.setColor(col);

	QPen disablePen(pen);
	disablePen.setColor(col.darker(DarkenFactor));

	// update all colors.
	for (size_t g=0;g<graphs_.size(); g++)
	{
		// anything that's not active series and graph is disabled color.
		const auto& series = graphs_[g].series;

		if (g == activeGraph_)
		{
			pen.setStyle(Qt::SolidLine);
			disablePen.setStyle(Qt::SolidLine);
		}
		else
		{
			pen.setStyle(DisabledPenStyle);
			disablePen.setStyle(DisabledPenStyle);
		}

		if (i == activeSeries_)
		{
			series[i]->setPen(pen);
		}
		else
		{
			series[i]->setPen(disablePen);
		}
	}
}

void GraphEditorView::setSingleActiveSeries(bool value)
{
	singleActiveSeries_ = value;
}

void GraphEditorView::setXAxisRange(float min, float max)
{
	pAxisX_->setRange(min, max);
}

void GraphEditorView::setYAxisRange(float min, float max)
{
	pAxisY_->setRange(min, max);
}

void GraphEditorView::setSeriesActive(int32_t seriesIdx)
{
	if (activeSeries_ == seriesIdx) {
		return;
	}

	const auto& col = colors_[seriesIdx];
	
	QPen pen;
	pen.setWidth(LineWidth);
	pen.setColor(col);

	auto* pTargetSeries = getSeries(activeGraph_, seriesIdx);
	pTargetSeries->setPen(pen);

	auto* pCurrentSeries = activeSeries();
	if (pCurrentSeries)
	{
		pen.setColor(colors_[activeSeries_].darker(DarkenFactor));

		if (singleActiveSeries_)
		{
			pen.setStyle(DisabledPenStyle);
		}

		pCurrentSeries->setPen(pen);
	}

	// re-add series so it's on top.
	pChart_->removeSeries(pTargetSeries);
	addSeriesToChart(pTargetSeries);

	activeSeries_ = seriesIdx;
}

void GraphEditorView::setGraphActive(int32_t graphIdx)
{
	if (activeGraph_ == graphIdx) {
		return;
	}

	QPen pen;
	pen.setWidth(LineWidth);

	// i need to change the current graph to be all dotted and disabled colors.
	if (activeGraph_ != -1)
	{
		auto& g = activeGraph();

		pen.setStyle(DisabledPenStyle);

		for (size_t i = 0; i < g.series.size(); i++)
		{
			pen.setColor(colors_[i].darker(DarkenFactor));
			g.series[i]->setPen(pen);
		}
	}

	// activate all series in target graph.
	auto& g = graphs_[graphIdx];

	for (size_t i = 0; i < g.series.size(); i++)
	{
		pChart_->removeSeries(g.series[i]);
	}

	// re add this graphs series, with the active one on top.
	for (size_t i = 0; i < g.series.size(); i++)
	{
		if (i == activeSeries_) {
			continue;
		}
		addSeriesToChart(g.series[i]);
	}

	addSeriesToChart(getSeries(graphIdx, activeSeries_));

	for (size_t i = 0; i < g.series.size(); i++)
	{
		if (i == activeSeries_)
		{
			pen.setColor(colors_[i]);
			pen.setStyle(Qt::SolidLine);
		}
		else
		{
			pen.setColor(colors_[i].darker(DarkenFactor));

			if (singleActiveSeries_)
			{
				pen.setStyle(DisabledPenStyle);
			}
			else
			{
				pen.setStyle(Qt::SolidLine);
			}
		}

		g.series[i]->setPen(pen);
	}

	activeGraph_ = graphIdx;
}

X_INLINE void GraphEditorView::addSeriesToChart(QtCharts::QLineSeries* pSeries)
{
	pChart_->addSeries(pSeries);
	pSeries->attachAxis(pAxisX_);
	pSeries->attachAxis(pAxisY_);
}

void GraphEditorView::mouseMoveEvent(QMouseEvent *event)
{
	X_UNUSED(event);

	if (isDraggingPoint())
	{
		auto newVal = pChart_->mapToValue(event->localPos());

		auto* pSeries = activeSeries();

		// can happen if you undo add point.
		if (activePoint_ >= pSeries->count()) {
			X_WARNING("Fx", "Ative point is no longer valid", activePoint_);
			return;
		}

		const auto curVal = pSeries->at(activePoint_);

		// can't move begin / end.
		if (activePoint_ == 0 || activePoint_ == pSeries->count() - 1)
		{
			newVal.setX(curVal.x());
		}
		else
		{
			// you can't move past other points.
			auto prevX = pSeries->at(activePoint_ - 1).x();
			auto nextX = pSeries->at(activePoint_ + 1).x();

			const qreal minDist = 0.001;

			newVal.setX(std::clamp(newVal.x(), prevX + minDist, nextX - minDist));
		}

		// clamp in range.
		newVal.setY(std::clamp(newVal.y(), pAxisY_->min(), pAxisY_->max()));
		newVal.setX(std::clamp(newVal.x(), pAxisX_->min(), pAxisX_->max()));

		auto& g = activeGraph();
		QPointF delta = newVal - curVal;
		
		pUndoStack_->push(new MovePoint(this, g, activeSeries_, activePoint_, delta));
		return;
	}

	QChartView::mouseMoveEvent(event);
}

void GraphEditorView::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
	{
		QChartView::mousePressEvent(event);
		return;
	}

	if (hoverSeries_ != -1)
	{
		setSeriesActive(hoverSeries_);
		hoverSeries_ = -1;
	}

	if (activePoint_ >= 0)
	{
		pUndoStack_->beginMacro("Move");
		mouseActive_ = true;
		return;
	}

	// click de click
	auto value = pChart_->mapToValue(event->localPos());
	
	auto minValX = pAxisX_->min();
	auto minValY = pAxisY_->min();

	if (value.x() >= minValX && value.y() >= minValY)
	{
		auto* pSeries = activeSeries();
		if (pSeries)
		{
			// find the index we are adding a point before.
			int32_t i;
			for (i=0; i < pSeries->count(); i++)
			{
				const auto& p = pSeries->at(i);
				if (value.x() < p.x())
				{
					// check if we are really close
					if ((value - p).manhattanLength() < 0.1)
					{
						return;
					}

					break;
				}
			}

			pUndoStack_->push(new AddPoint(this, activeGraph(), i, value));		
			return;
		}
	}

	QChartView::mousePressEvent(event);
}

void GraphEditorView::mouseReleaseEvent(QMouseEvent *event)
{
	if (isDraggingPoint())
	{
		pUndoStack_->endMacro();

		mouseActive_ = false;
		activePoint_ = -1;
	}

	QChartView::mouseReleaseEvent(event);
}

void GraphEditorView::leaveEvent(QEvent *event)
{
	if (!mouseActive_)
	{
		activePoint_ = -1;
	}

	QChartView::leaveEvent(event);
}

void GraphEditorView::seriesHover(const QPointF &point, bool state)
{
	hoverSeries_ = -1;

	if (state) 
	{
		// can select any point from active graph.
		// if you select one of diffrent series you switch to that.
		qreal lowestLength = 0.1;

		auto findhoveringPoint = [&](QtCharts::QLineSeries* pSeries) -> int32_t {
			auto count = pSeries->count();
			
			int32_t idx = -1;

			for (int32_t i = 0; i < count; i++)
			{
				const auto& p = pSeries->at(i);
				auto rel = point - p;

				auto length = rel.manhattanLength();

				if (length < lowestLength)
				{
					lowestLength = length;
					idx = i;
				}
			}

			return idx;
		};

		const auto numPoints = activeSeries()->count();

		// check all series for closest point.
		int32_t index = -1;
		{
			auto& g = activeGraph();
			for (int32_t s = 0; s < g.series.size(); s++)
			{
				auto idx = findhoveringPoint(g.series[s]);
				if (idx != -1)
				{
					// change series.
					hoverSeries_ = s;
					index = idx;
				}
			}
		}

		// found a point?
		if (index != -1)
		{
			if (index != activePoint_)
			{
				activePoint_ = index;
			}

			if (index == 0 || index == numPoints - 1)
			{
				setCursor(Qt::SizeVerCursor);
			}
			else
			{
				setCursor(Qt::SizeAllCursor);
			}
			return;
		}
	}
	else
	{
		activePoint_ = -1;
	}

	unsetCursor();
}

void GraphEditorView::showContextMenu(const QPoint &pos)
{
	ActionContainer* pActionContainer = ActionManager::createMenu(Constants::M_GRAPH_EDITOR);
	auto* pMenu = pActionContainer->menu();
	pMenu->clear();

	pMenu->addAction(pUndoAction_);
	pMenu->addAction(pRedoAction_);
	pMenu->addSeparator();

	if (names_.size() > 1)
	{
		// show series selection.
		for (size_t i = 0; i<names_.size(); i++)
		{
			QString name = names_[i];
			if (name.isEmpty())
			{
				name = QString("Series %1").arg(i);
			}

			QAction* pAction = pMenu->addAction(name);
			pAction->setCheckable(true);
			pAction->setChecked(i == activeSeries_);
			pAction->setData(qVariantFromValue(i));
			connect(pAction, &QAction::triggered, this, &GraphEditorView::setActiveSeries);
		}

		pMenu->addSeparator();
	}

	// now graphs.
	for (size_t i = 0; i < graphs_.size(); i++)
	{
		QString name = graphs_[i].name;
		if (name.isEmpty())
		{
			name = QString("Graph %1").arg(i);
		}

		QAction* pAction = pMenu->addAction(name);
		pAction->setCheckable(true);
		pAction->setChecked(i == activeGraph_);
		pAction->setData(qVariantFromValue(i));
		connect(pAction, &QAction::triggered, this, &GraphEditorView::setActiveGraph);
	}

	pMenu->addSeparator();

	QAction* pAction = pMenu->addAction("Clear Series");
	pAction->setStatusTip(tr("Clear the knots from current series"));
	connect(pAction, &QAction::triggered, this, &GraphEditorView::clearKnots);

	pAction = pMenu->addAction("Reset Graph");
	connect(pAction, &QAction::triggered, this, &GraphEditorView::resetKnots);

	pMenu->popup(mapToGlobal(pos));
}

void GraphEditorView::setActiveSeries(void)
{
	QAction* pAction = qobject_cast<QAction*>(sender());
	if (pAction)
	{
		int32_t series = pAction->data().value<int32_t>();
		setSeriesActive(series);
	}
}

void GraphEditorView::setActiveGraph(void)
{
	QAction* pAction = qobject_cast<QAction*>(sender());
	if (pAction)
	{
		int32_t graph = pAction->data().value<int32_t>();
		setGraphActive(graph);
	}
}

void GraphEditorView::clearKnots(void)
{
	if (activeSeries_ >= 0)
	{
		// remove all but first and last.
		auto count = activeSeries()->count();
		if (count > 2) {
			pUndoStack_->push(new ClearPoints(this, activeSeries()));
		}
	}
}

void GraphEditorView::resetKnots(void)
{
	if (activeSeries_ >= 0)
	{
		pUndoStack_->push(new ResetGraph(this, activeGraph()));
	}
}

void GraphEditorView::signalPointsChanged(void)
{
	time_.restart();

	if (dirty_)
	{
		emit pointsChanged();
		dirty_ = false;
	}
	else
	{
		timer_.stop();
	}
}

void GraphEditorView::onPointsChanged(void)
{
	constexpr qint64 msecsPeriod{ 200 };

	dirty_ = true;

	if (!time_.isValid())
	{
		signalPointsChanged();
		return;
	}

	auto elapsed = time_.elapsed();

	if (elapsed >= msecsPeriod)
	{
		signalPointsChanged();
		return;
	}

	if (!timer_.isActive())
	{
		timer_.start(msecsPeriod - elapsed, this);
	}
}

void GraphEditorView::timerEvent(QTimerEvent *event)
{
	if (timer_.timerId() == event->timerId())
	{
		signalPointsChanged();
	}
}

X_INLINE bool GraphEditorView::isDraggingPoint(void) const
{
	return (mouseActive_ && activePoint_ >= 0);
}

X_INLINE QtCharts::QLineSeries* GraphEditorView::getSeries(int32_t graphIdx, int32_t seriesIdx) const
{
	return graphs_[graphIdx].series[seriesIdx];
}

X_INLINE QtCharts::QLineSeries* GraphEditorView::activeSeries(void) const
{
	if (activeGraph_ < 0 || activeSeries_ < 0) {
		return nullptr;
	}

	return graphs_[activeGraph_].series[activeSeries_];
}


X_INLINE const GraphEditorView::Graph& GraphEditorView::activeGraph(void) const
{
	return graphs_[activeGraph_];
}

X_INLINE GraphEditorView::Graph& GraphEditorView::activeGraph(void)
{
	return graphs_[activeGraph_];
}

X_INLINE qreal GraphEditorView::getMinY(void) const
{
	return pAxisY_->min();
}

X_INLINE qreal GraphEditorView::getMaxY(void) const
{
	return pAxisY_->max();
}


// -----------------------------------

GraphEditor::GraphEditor(QWidget* parent) :
	GraphEditorView(parent)
{
}

GraphEditor::GraphEditor(int32_t numGraph, int32_t numSeries, QWidget* parent) :
	GraphEditorView(parent)
{
	createGraphs(numGraph, numSeries);
}

// -----------------------------------

GradientWidget::GradientWidget(QWidget* parent) :
	QWidget(parent)
{
	ColorPoint cp;
	cp.col = QColor(255,255,255);
	cp.pos = 0.0;
	colors_.push_back(cp);

	cp.col = QColor(0, 0, 0);
	cp.pos = 1.0;
	colors_.push_back(cp);
}

void GradientWidget::setColors(const ColorPointArr& colors)
{
	colors_ = colors;

	update();
}

void GradientWidget::paintEvent(QPaintEvent*)
{
	QLinearGradient grad(0,0, width(), height());

	for (const auto& cp : colors_)
	{
		grad.setColorAt(cp.pos, cp.col);
	}

	QPainter painter(this);
	painter.fillRect(rect(), grad);
}

// -----------------------------------

ColorGraphEditor::ColorGraphEditor(int32_t numGraph, QWidget* parent) :
	QWidget(parent)
{
	pGraph_ = new GraphEditorView();
	pGraph_->createGraphs(numGraph, 3);

	pGraph_->setSeriesName(0, "R");
	pGraph_->setSeriesName(1, "G");
	pGraph_->setSeriesName(2, "B");

	pGraph_->setSeriesColor(0, QColor(QRgb(0xc00000)));
	pGraph_->setSeriesColor(1, QColor(QRgb(0x00c000)));
	pGraph_->setSeriesColor(2, QColor(QRgb(0x0000c0)));

	connect(pGraph_, &GraphEditorView::pointsChanged, this, &ColorGraphEditor::updateColor);
	connect(pGraph_, &GraphEditorView::pointsChanged, this, &ColorGraphEditor::valueChanged);

	pGradient_ = new GradientWidget();
	pGradient_->setMinimumHeight(10);

	pRandomGraph_ = new QCheckBox();
	pRandomGraph_->setText("Random Graph");
	pRandomGraph_->setToolTip(QStringLiteral("Randomize between graphs"));

	QHBoxLayout* pHLayout = new QHBoxLayout();
	pHLayout->addStretch(0);
	pHLayout->addWidget(pRandomGraph_);

	connect(pRandomGraph_, &QCheckBox::stateChanged, this, &ColorGraphEditor::valueChanged);

	QVBoxLayout* pLayout = new QVBoxLayout();
	{
		pLayout->setContentsMargins(0,0,0,0);
		pLayout->addWidget(pGraph_);
		pLayout->addWidget(pGradient_);
		pLayout->addLayout(pHLayout);
	}

	// i need to know when the graph changes.
	// so i can update colors.

	setLayout(pLayout);
}

void ColorGraphEditor::updateColor(void)
{
	// i only care for active graph.
	const auto& graph = const_cast<const GraphEditorView*>(pGraph_)->activeGraph();

	// now i want all the graphs.
	if (graph.series.size() != 3) {
		return;
	}

	colors_.resize(graph.series.front()->count());

	auto* pSeriesR = graph.series[0];
	auto* pSeriesG = graph.series[1];
	auto* pSeriesB = graph.series[2];

	auto pointsR = pSeriesR->pointsVector();
	auto pointsG = pSeriesG->pointsVector();
	auto pointsB = pSeriesB->pointsVector();

	for (int32_t p = 0; p < pointsR.count(); p++)
	{
		auto pos = pointsR[p].x();

		auto r = pointsR[p].y();
		auto g = pointsG[p].y();
		auto b = pointsB[p].y();

		colors_[p].pos = pos;
		colors_[p].col = QColor::fromRgbF(r,g,b);
	}

	pGradient_->setColors(colors_);
}


void ColorGraphEditor::setValue(const ColorInfo& col)
{
	blockSignals(true);

	// need to update graphs.
	pGraph_->setValue(col.col);

	pRandomGraph_->setChecked(col.col.random);

	updateColor();

	blockSignals(false);
}

void ColorGraphEditor::getValue(ColorInfo& col)
{
	pGraph_->getValue(col.col);

	col.col.random = pRandomGraph_->isChecked();
}

// -----------------------------------


GraphWithScale::GraphWithScale(const QString& label, QWidget* parent) :
	QGroupBox(label, parent)
{
	QVBoxLayout* pLayout = new QVBoxLayout();
	{
		pGraph_ = new GraphEditor(2, 1);
		pScale_ = new QDoubleSpinBox();
		pScale_->setSingleStep(0.05);
		pScale_->setRange(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());


		pRandomGraph_ = new QCheckBox();
		pRandomGraph_->setText("Random Graph");
		pRandomGraph_->setToolTip(QStringLiteral("Randomize between graphs"));

		pLayout->addWidget(pGraph_);

		QHBoxLayout* pHLayout = new QHBoxLayout();
		pHLayout->setContentsMargins(0, 0, 0, 0);
		pHLayout->addWidget(new QLabel("Scale"));
		pHLayout->addWidget(pScale_);
		pHLayout->addStretch(1);
		pHLayout->addWidget(pRandomGraph_);

		pLayout->addLayout(pHLayout);

		connect(pGraph_, &GraphEditor::pointsChanged, this, &GraphWithScale::valueChanged);
		connect(pScale_, &QDoubleSpinBox::editingFinished, this, &GraphWithScale::valueChanged);
		connect(pRandomGraph_, &QCheckBox::stateChanged, this, &GraphWithScale::valueChanged);
	}

	setLayout(pLayout);
}


void GraphWithScale::setValue(const GraphInfo& g)
{
	blockSignals(true);

	pGraph_->setValue(g);
	pScale_->setValue(g.scale);

	pRandomGraph_->setChecked(g.random);

	blockSignals(false);
}

void GraphWithScale::getValue(GraphInfo& g)
{
	pGraph_->getValue(g);
	g.scale = pScale_->value();

	g.random = pRandomGraph_->isChecked();
}

// -----------------------------------


SegmentListWidget::SegmentListWidget(FxSegmentModel* pModel, QWidget* parent) :
	QWidget(parent),
	pSegmentModel_(pModel)
{
	QVBoxLayout* pTableLayout = new QVBoxLayout();
	{
		pTable_ = new QTableView();

		pTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
		pTable_->setSelectionMode(QAbstractItemView::SingleSelection);
		pTable_->horizontalHeader()->setStretchLastSection(true);
		pTable_->setModel(pSegmentModel_);
		
		connect(pTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SegmentListWidget::itemSelectionChanged);
		connect(pTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SegmentListWidget::selectionChanged);

		pTableLayout->addWidget(pTable_);
	}
	QHBoxLayout* pButtonLayout = new QHBoxLayout();
	{
		QPushButton* pAdd = new QPushButton();
		pAdd->setText(tr("Add"));
		pAdd->setMaximumWidth(80);

		pDuplicate_ = new QPushButton();
		pDuplicate_->setText(tr("Duplicate"));
		pDuplicate_->setMaximumWidth(80);
		pDuplicate_->setEnabled(false);

		pDelete_ = new QPushButton();
		pDelete_->setText(tr("Delete"));
		pDelete_->setMaximumWidth(80);
		pDelete_->setEnabled(false);

		connect(pAdd, &QPushButton::clicked, this, &SegmentListWidget::addStageClicked);
		connect(pDuplicate_, &QPushButton::clicked, this, &SegmentListWidget::duplicateSelectedClicked);
		connect(pDelete_, &QPushButton::clicked, this, &SegmentListWidget::deleteSelectedStageClicked);

		pButtonLayout->addWidget(pAdd);
		pButtonLayout->addWidget(pDuplicate_);
		pButtonLayout->addWidget(pDelete_);
		pButtonLayout->addStretch();
	}

	pTableLayout->addLayout(pButtonLayout);

	setLayout(pTableLayout);
}

void SegmentListWidget::setActiveIndex(int32_t idx)
{
	auto modelIndex = pTable_->model()->index(idx, 0);

	pTable_->selectionModel()->setCurrentIndex(modelIndex, QItemSelectionModel::Select | QItemSelectionModel::Rows);
}


void SegmentListWidget::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
	X_UNUSED(deselected);

	pDuplicate_->setEnabled(!selected.isEmpty());
	pDelete_->setEnabled(!selected.isEmpty());
}

void SegmentListWidget::addStageClicked(void)
{
	pSegmentModel_->addSegment();
}

void SegmentListWidget::duplicateSelectedClicked(void)
{
	auto* pSelectModel = pTable_->selectionModel();

	if (!pSelectModel->hasSelection()) {
		return;
	}

	auto selected = pSelectModel->selectedRows().first();

	pSegmentModel_->duplicateSegment(selected.row());
}

void SegmentListWidget::deleteSelectedStageClicked(void)
{
	auto* pSelectModel = pTable_->selectionModel();

	if (!pSelectModel->hasSelection()) {
		return;
	}

	QModelIndexList indexes = pSelectModel->selectedRows();
	for (int32_t i = 0; i < indexes.count(); ++i)
	{
		pSelectModel->model()->removeRow(indexes[i].row(), indexes[i].parent());
	}
}

// -----------------------------------

SpawnInfoWidget::SpawnInfoWidget(QWidget* parent) :
	QGroupBox("Spawn", parent)
{
	QFormLayout* pLayout = new QFormLayout();
	pLayout->setLabelAlignment(Qt::AlignLeft);
	{
		// list of shit.
		pCount_ = new SpinBoxRange();
		pInterval_ = new QSpinBox();
		pLoopCount_ = new QSpinBox();
		pLife_ = new SpinBoxRange();
		pDelay_ = new SpinBoxRange();

		pInterval_->setEnabled(false);
		pLoopCount_->setEnabled(false);

		pInterval_->setRange(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
		pLoopCount_->setRange(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());


		QHBoxLayout* pRadioLayout = new QHBoxLayout();
		{
			QButtonGroup* pGroup = new QButtonGroup();
			pOneShot_ = new QRadioButton();
			pLooping_ = new QRadioButton();
			
			connect(pGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), this, &SpawnInfoWidget::buttonToggled);
			connect(pGroup, QOverload<int,bool>::of(&QButtonGroup::buttonToggled), this, &SpawnInfoWidget::valueChanged);

			pOneShot_->setText("One-shot");
			pOneShot_->setChecked(true);
			pLooping_->setText("Looping");

			pGroup->addButton(pOneShot_);
			pGroup->addButton(pLooping_);
			pGroup->setId(pOneShot_, 0);
			pGroup->setId(pLooping_, 1);
			pGroup->setExclusive(true);

			pRadioLayout->addWidget(pOneShot_);
			pRadioLayout->addWidget(pLooping_);
			pRadioLayout->addStretch(1);
		}

		pLayout->addRow(pRadioLayout);
		pLayout->addRow(tr("Count"), pCount_);
		pLayout->addRow(tr("Interval"), pInterval_);
		pLayout->addRow(tr("Loop Count"), pLoopCount_);
		pLayout->addRow(tr("Life"), pLife_);
		pLayout->addRow(tr("Delay"), pDelay_);
	}

	
	connect(pCount_, &SpinBoxRange::valueChanged, this, &SpawnInfoWidget::valueChanged);
	connect(pInterval_, &QSpinBox::editingFinished, this, &SpawnInfoWidget::valueChanged);
	connect(pLoopCount_, &QSpinBox::editingFinished, this, &SpawnInfoWidget::valueChanged);
	connect(pLife_, &SpinBoxRange::valueChanged, this, &SpawnInfoWidget::valueChanged);
	connect(pDelay_, &SpinBoxRange::valueChanged, this, &SpawnInfoWidget::valueChanged);

	setLayout(pLayout);
}

void SpawnInfoWidget::setValue(const SpawnInfo& spawn)
{
	blockSignals(true);

	pLooping_->setChecked(spawn.looping);
	pInterval_->setValue(spawn.interval);
	pLoopCount_->setValue(spawn.loopCount);

	pCount_->setValue(spawn.count);
	pLife_->setValue(spawn.life);
	pDelay_->setValue(spawn.delay);

	blockSignals(false);
}

void SpawnInfoWidget::getValue(SpawnInfo& spawn)
{
	spawn.looping = pLooping_->isChecked();
	spawn.interval = pInterval_->value();
	spawn.loopCount = pLoopCount_->value();

	pCount_->getValue(spawn.count);
	pLife_->getValue(spawn.life);
	pDelay_->getValue(spawn.delay);
}

void SpawnInfoWidget::buttonToggled(int id, bool checked)
{
	if (id == 0)
	{
		pInterval_->setEnabled(!checked);
		pLoopCount_->setEnabled(!checked);
		pCount_->setEnabled(checked);
	}
	else
	{
		pInterval_->setEnabled(checked);
		pLoopCount_->setEnabled(checked);
		pCount_->setEnabled(!checked);
	}
}

// -----------------------------------

OriginInfoWidget::OriginInfoWidget(QWidget* parent) :
	QGroupBox("Origin", parent)
{
	QFormLayout* pLayout = new QFormLayout();
	{
		pForward_ = new SpinBoxRangeDouble(false);
		pRight_ = new SpinBoxRangeDouble(false);
		pUp_ = new SpinBoxRangeDouble(false);

		pRelative_ = new QCheckBox();
		pRelative_->setText("Relative");
		pRelative_->setToolTip(QStringLiteral("Origin is relative to effect axis instead of world"));

		QHBoxLayout* pRadioLayout = new QHBoxLayout();
		{
			pGroup_ = new QButtonGroup();
			pOffsetNone_ = new QRadioButton();
			pSphere_ = new QRadioButton();
			pCylinder_ = new QRadioButton();

			connect(pGroup_, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), this, &OriginInfoWidget::buttonToggled);
			connect(pGroup_, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), this, &OriginInfoWidget::valueChanged);

			pOffsetNone_->setText("None");
			pOffsetNone_->setChecked(true);
			pSphere_->setText("Spherical");
			pCylinder_->setText("Cylindrical");

			pGroup_->addButton(pOffsetNone_);
			pGroup_->addButton(pSphere_);
			pGroup_->addButton(pCylinder_);
			pGroup_->setId(pOffsetNone_, 0);
			pGroup_->setId(pSphere_, 1);
			pGroup_->setId(pCylinder_, 2);
			pGroup_->setExclusive(true);

			pRadioLayout->addWidget(pOffsetNone_);
			pRadioLayout->addWidget(pSphere_);
			pRadioLayout->addWidget(pCylinder_);
			pRadioLayout->addStretch(1);
		}

		pRadius_ = new SpinBoxRangeDouble(true);
		pHeight_ = new SpinBoxRangeDouble(true);
		pRadius_->setEnabled(false);
		pHeight_->setEnabled(false);

		pLayout->addRow(tr("Forward"), pForward_);
		pLayout->addRow(tr("Right"), pRight_);
		pLayout->addRow(tr("Up"), pUp_);
		pLayout->addRow(pRelative_);
		pLayout->addItem(new QSpacerItem(0, 10, QSizePolicy::Expanding, QSizePolicy::Minimum));
		pLayout->addRow(tr("Offset"), pRadioLayout);
		pLayout->addRow(tr("Radius"), pRadius_);
		pLayout->addRow(tr("Height"), pHeight_);

		connect(pForward_, &SpinBoxRangeDouble::valueChanged, this, &OriginInfoWidget::valueChanged);
		connect(pRight_, &SpinBoxRangeDouble::valueChanged, this, &OriginInfoWidget::valueChanged);
		connect(pUp_, &SpinBoxRangeDouble::valueChanged, this, &OriginInfoWidget::valueChanged);
		connect(pRelative_, &QCheckBox::stateChanged, this, &OriginInfoWidget::valueChanged);
	}

	setLayout(pLayout);
}

void OriginInfoWidget::setValue(const OriginInfo& org)
{
	blockSignals(true);

	pForward_->setValue(org.spawnOrgX);
	pRight_->setValue(org.spawnOrgY);
	pUp_->setValue(org.spawnOrgZ);
	pRelative_->setChecked(org.relative);

	pRadius_->setValue(org.spawnRadius);
	pHeight_->setValue(org.spawnHeight);
	
	switch (org.offsetType)
	{
		case OriginInfo::OffsetType::None:
			pOffsetNone_->setChecked(true);
			break;
		case OriginInfo::OffsetType::Spherical:
			pSphere_->setChecked(true);
			break;
		case OriginInfo::OffsetType::Cylindrical:
			pSphere_->setChecked(true);
			break;

        default:
            X_NO_SWITCH_DEFAULT_ASSERT;
	}

	blockSignals(false);
}

void OriginInfoWidget::getValue(OriginInfo& org)
{
	pForward_->getValue(org.spawnOrgX);
	pRight_->getValue(org.spawnOrgY);
	pUp_->getValue(org.spawnOrgZ);
	org.relative = pRelative_->isChecked();

	pRadius_->getValue(org.spawnRadius);
	pHeight_->getValue(org.spawnHeight);

	switch (pGroup_->checkedId())
	{
		case 0:
			X_ASSERT(pOffsetNone_->isChecked(), "Incorrrect index")();
			org.offsetType = OriginInfo::OffsetType::None;
			break;
		case 1:
			X_ASSERT(pSphere_->isChecked(), "Incorrrect index")();
			org.offsetType = OriginInfo::OffsetType::Spherical;
			break;
		case 2:
			X_ASSERT(pCylinder_->isChecked(), "Incorrrect index")();
			org.offsetType = OriginInfo::OffsetType::Cylindrical;
			break;

        default:
            X_NO_SWITCH_DEFAULT_ASSERT;
	}
}

void OriginInfoWidget::buttonToggled(int id, bool checked)
{
	if (id == 0)
	{
		pRadius_->setEnabled(!checked);
		pHeight_->setEnabled(!checked);
	}
}

// -----------------------------------


SequenceInfoWidget::SequenceInfoWidget(QWidget* parent) :
	QGroupBox("Sequence Control", parent)
{
	QFormLayout* pLayout = new QFormLayout();
	{
		pStart_ = new QSpinBox();
		pPlayRate_ = new QSpinBox();
		pLoopCount_ = new QSpinBox();

		pStart_->setRange(-1, std::numeric_limits<int16_t>::max());
		pPlayRate_->setRange(-1, std::numeric_limits<int16_t>::max());
		pLoopCount_->setRange(-1, std::numeric_limits<int16_t>::max());

		pLayout->addRow(tr("Start"), pStart_);
		pLayout->addRow(tr("PlayRate"), pPlayRate_);
		pLayout->addRow(tr("Loop"), pLoopCount_);

		connect(pStart_, &QSpinBox::editingFinished, this, &SequenceInfoWidget::valueChanged);
		connect(pPlayRate_, &QSpinBox::editingFinished, this, &SequenceInfoWidget::valueChanged);
		connect(pLoopCount_, &QSpinBox::editingFinished, this, &SequenceInfoWidget::valueChanged);
	}

	setLayout(pLayout);
}


void SequenceInfoWidget::setValue(const SequenceInfo& sq)
{
	blockSignals(true);

	pStart_->setValue(sq.startFrame);
	pPlayRate_->setValue(sq.fps);
	pLoopCount_->setValue(sq.loop);

	blockSignals(false);
}

void SequenceInfoWidget::getValue(SequenceInfo& sq)
{
	sq.startFrame = pStart_->value();
	sq.fps = pPlayRate_->value();
	sq.loop = pLoopCount_->value();
}


// -----------------------------------


VelocityGraph::VelocityGraph(QWidget* parent) :
	QGroupBox(parent)
{
	QVBoxLayout* pLayout = new QVBoxLayout();
	{
		pVelGraph_ = new GraphEditorView();
		pVelGraph_->setYAxisRange(-0.5f, 0.5f);
		pVelGraph_->createGraphs(6, 1);
		pVelGraph_->setGraphName(0, "Graph 0: Forward");
		pVelGraph_->setGraphName(1, "Graph 0: Right");
		pVelGraph_->setGraphName(2, "Graph 0: Up");
		pVelGraph_->setGraphName(3, "Graph 1: Forward");
		pVelGraph_->setGraphName(4, "Graph 1: Right");
		pVelGraph_->setGraphName(5, "Graph 1: Up");

		pForwardScale_ = new QDoubleSpinBox();
		pForwardScale_->setRange(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
		pForwardScale_->setMinimumWidth(50);
		pForwardScale_->setMaximumWidth(50);
		pForwardScale_->setSingleStep(0.05);
		pRightScale_ = new QDoubleSpinBox();
		pRightScale_->setRange(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
		pRightScale_->setMinimumWidth(50);
		pRightScale_->setMaximumWidth(50);
		pRightScale_->setSingleStep(0.05);
		pUpScale_ = new QDoubleSpinBox();
		pUpScale_->setRange(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
		pUpScale_->setMinimumWidth(50);
		pUpScale_->setMaximumWidth(50);
		pUpScale_->setSingleStep(0.05);

		pRandomGraph_ = new QCheckBox();
		pRandomGraph_->setText("Random Graph");
		pRandomGraph_->setToolTip(QStringLiteral("Randomize between graphs"));

		pRelative_ = new QCheckBox();
		pRelative_->setText("Relative");
		pRelative_->setToolTip(QStringLiteral("Verlocity is relative to effect axis instead of world"));

		QHBoxLayout* pHLayout = new QHBoxLayout();
		pHLayout->setContentsMargins(0, 0, 0, 0);
		pHLayout->addStretch(1);
		pHLayout->addWidget(pRelative_);

		QHBoxLayout* pFormLayout = new QHBoxLayout();
		pFormLayout->addWidget(new QLabel("Forward"));
		pFormLayout->addWidget(pForwardScale_);
		pFormLayout->addWidget(new QLabel("Right"));
		pFormLayout->addWidget(pRightScale_);
		pFormLayout->addWidget(new QLabel("Up"));
		pFormLayout->addWidget(pUpScale_);
		pFormLayout->addStretch(1);
		pFormLayout->addWidget(pRandomGraph_);

		pLayout->addLayout(pHLayout);
		pLayout->addWidget(pVelGraph_);
		pLayout->addLayout(pFormLayout);

		connect(pVelGraph_, &GraphEditorView::pointsChanged, this, &VelocityGraph::valueChanged);
		connect(pForwardScale_, &QDoubleSpinBox::editingFinished, this, &VelocityGraph::valueChanged);
		connect(pRightScale_, &QDoubleSpinBox::editingFinished, this, &VelocityGraph::valueChanged);
		connect(pUpScale_, &QDoubleSpinBox::editingFinished, this, &VelocityGraph::valueChanged);
		connect(pRandomGraph_, &QCheckBox::stateChanged, this, &VelocityGraph::valueChanged);
		connect(pRelative_, &QCheckBox::stateChanged, this, &VelocityGraph::valueChanged);
	}

	setLayout(pLayout);
}


void VelocityGraph::setValue(const VelocityGraphInfo& vel)
{
	blockSignals(true);

	pVelGraph_->setValue(vel.graph);

	pForwardScale_->setValue(vel.forwardScale);
	pRightScale_->setValue(vel.rightScale);
	pUpScale_->setValue(vel.upScale);

	pRandomGraph_->setChecked(vel.graph.random);
	pRelative_->setChecked(vel.relative);

	blockSignals(false);
}

void VelocityGraph::getValue(VelocityGraphInfo& vel)
{
	pVelGraph_->getValue(vel.graph);

	vel.forwardScale = pForwardScale_->value();
	vel.rightScale = pRightScale_->value();
	vel.upScale = pUpScale_->value();

	vel.graph.random = pRandomGraph_->isChecked();
	vel.relative = pRelative_->isChecked();
}


// -----------------------------------


VelocityInfoWidget::VelocityInfoWidget(QWidget* parent) :
	QWidget(parent)
{
	QVBoxLayout* pLayout = new QVBoxLayout();
	{
		QGroupBox* pMoveGroupBox = new QGroupBox("Relative to");
		{
			QHBoxLayout* pMoveLayout = new QHBoxLayout();
			QButtonGroup* pGroup = new QButtonGroup();

			pSpawn_ = new QRadioButton();
			pNow_ = new QRadioButton();

			pSpawn_->setText("Spawn");
			pSpawn_->setChecked(true);
			pNow_->setText("Now");

			pGroup->addButton(pSpawn_);
			pGroup->addButton(pNow_);
			pGroup->setExclusive(true);

			pMoveLayout->addWidget(pSpawn_);
			pMoveLayout->addWidget(pNow_);
			
			pMoveGroupBox->setMaximumWidth(140);
			pMoveGroupBox->setLayout(pMoveLayout);
		
			connect(pGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), this, &VelocityInfoWidget::valueChanged);
		}

		pLayout->addWidget(pMoveGroupBox);
	}

	setLayout(pLayout);
}

void VelocityInfoWidget::setValue(const VelocityInfo& vel)
{
	static_assert(engine::fx::RelativeTo::ENUM_COUNT == 2, "Enum count changed? This might need updating");

	blockSignals(true);

	if (vel.postionType == engine::fx::RelativeTo::Spawn) {
		pSpawn_->setChecked(true);
	}

	blockSignals(false);
}

void VelocityInfoWidget::getValue(VelocityInfo& vel)
{
	static_assert(engine::fx::RelativeTo::ENUM_COUNT == 2, "Enum count changed? This might need updating");

	vel.postionType = engine::fx::RelativeTo::Now;
	if (pSpawn_->isChecked()) {
		vel.postionType = engine::fx::RelativeTo::Spawn;
	} 
}

// -----------------------------------

RotationGraphWidget::RotationGraphWidget(QWidget *parent) :
	QGroupBox("Rotation", parent)
{
	QVBoxLayout* pLayout = new QVBoxLayout();
	{
		pRotationGraph_ = new GraphEditor();
		pRotationGraph_->setYAxisRange(-0.5f,0.5f);
		pRotationGraph_->createGraphs(2, 1);

		pInitialRotation_ = new SpinBoxRangeDouble(false);
		pInitialRotation_->setMinimumWidth(80);

		pScale_ = new QDoubleSpinBox();
		pScale_->setSingleStep(0.05);
		pScale_->setRange(std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());

		pRandomGraph_ = new QCheckBox();
		pRandomGraph_->setText("Random Graph");
		pRandomGraph_->setToolTip(QStringLiteral("Randomize between graphs"));

		QHBoxLayout* pFormLayout = new QHBoxLayout();
		pFormLayout->addWidget(new QLabel("Initial Rotation"));
		pFormLayout->addWidget(pInitialRotation_);
		pFormLayout->addStretch(1);

		QHBoxLayout* pHLayout = new QHBoxLayout();
		pHLayout->setContentsMargins(0, 0, 0, 0);
		pHLayout->addWidget(new QLabel("Scale"));
		pHLayout->addWidget(pScale_);
		pHLayout->addStretch(1);
		pHLayout->addWidget(pRandomGraph_);

		pLayout->addLayout(pFormLayout);
		pLayout->addWidget(pRotationGraph_);
		pLayout->addLayout(pHLayout);

		connect(pRotationGraph_, &GraphEditor::pointsChanged, this, &RotationGraphWidget::valueChanged);
		connect(pInitialRotation_, &SpinBoxRangeDouble::valueChanged, this, &RotationGraphWidget::valueChanged);
		connect(pScale_, &QDoubleSpinBox::editingFinished, this, &RotationGraphWidget::valueChanged);
		connect(pScale_, &QDoubleSpinBox::editingFinished, this, &RotationGraphWidget::valueChanged);
		connect(pRandomGraph_, &QCheckBox::stateChanged, this, &RotationGraphWidget::valueChanged);
	}

	setLayout(pLayout);
}


void RotationGraphWidget::setValue(const RotationInfo& rot)
{
	blockSignals(true);

	pInitialRotation_->setValue(rot.initial);
	pRotationGraph_->setValue(rot.rot);
	pScale_->setValue(rot.rot.scale);

	blockSignals(false);
}

void RotationGraphWidget::getValue(RotationInfo& rot)
{
	pInitialRotation_->getValue(rot.initial);
	pRotationGraph_->getValue(rot.rot);
	rot.rot.scale = pScale_->value();
}

// -----------------------------------
	
ColorGraph::ColorGraph(QWidget *parent) :
	QGroupBox("RGB", parent)
{
	QVBoxLayout* pLayout = new QVBoxLayout();
	{
		pColorGraph_ = new ColorGraphEditor(2);

		pLayout->addWidget(pColorGraph_);

		connect(pColorGraph_, &ColorGraphEditor::valueChanged, this, &ColorGraph::valueChanged);
	}

	setLayout(pLayout);
}


void ColorGraph::setValue(const ColorInfo& col)
{
	blockSignals(true);

	pColorGraph_->setValue(col);

	blockSignals(false);
}

void ColorGraph::getValue(ColorInfo& col)
{
	pColorGraph_->getValue(col);
}


// -----------------------------------

AlphaGraph::AlphaGraph(QWidget *parent) :
	QGroupBox("Alpha", parent)
{
	QVBoxLayout* pLayout = new QVBoxLayout();
	{
		pAlphaGraph_ = new GraphEditor(2, 1);

		pRandomGraph_ = new QCheckBox();
		pRandomGraph_->setText("Random Graph");
		pRandomGraph_->setToolTip(QStringLiteral("Randomize between graphs"));

		QHBoxLayout* pHLayout = new QHBoxLayout();
		pHLayout->addStretch(0);
		pHLayout->addWidget(pRandomGraph_);

		pLayout->addWidget(pAlphaGraph_);
		pLayout->addLayout(pHLayout);

		connect(pAlphaGraph_, &GraphEditor::pointsChanged, this, &AlphaGraph::valueChanged);
		connect(pRandomGraph_, &QCheckBox::stateChanged, this, &AlphaGraph::valueChanged);
	}

	setLayout(pLayout);
}

void AlphaGraph::setValue(const ColorInfo& col)
{
	blockSignals(true);

	pAlphaGraph_->setValue(col.alpha);

	pRandomGraph_->setChecked(col.alpha.random);
	
	blockSignals(false);
}

void AlphaGraph::getValue(ColorInfo& col)
{
	pAlphaGraph_->getValue(col.alpha);

	col.alpha.random = pRandomGraph_->isChecked();
}


// -----------------------------------


VisualsInfoWidget::VisualsInfoWidget(QWidget* parent) :
	QGroupBox("Visuals", parent)
{
	QFormLayout* pLayout = new QFormLayout();
	{
		pType_ = new QComboBox();

		for (uint32_t i = 0; i < engine::fx::StageType::ENUM_COUNT; i++)
		{
			QString name = engine::fx::StageType::ToString(i);
			pType_->addItem(name);
		}

		connect(pType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VisualsInfoWidget::currentIndexChanged);

		pMaterial_ = new QLineEdit();

		pLayout->addRow(tr("Type"), pType_);
		pLayout->addRow(tr("Material"), pMaterial_);

		connect(pType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VisualsInfoWidget::valueChanged);
		connect(pMaterial_, &QLineEdit::editingFinished, this, &VisualsInfoWidget::valueChanged);
	}

	setLayout(pLayout);
}

void VisualsInfoWidget::setValue(const VisualsInfo& vis)
{
	blockSignals(true);

	QString mat;
	
	if (!vis.materials.empty()) {
		mat = vis.materials.front();
	}

	pMaterial_->setText(mat);
	pType_->setCurrentIndex(static_cast<int32_t>(vis.type));

	blockSignals(false);
}

void VisualsInfoWidget::getValue(VisualsInfo& vis)
{
	vis.materials.clear();
	vis.materials.push_back(pMaterial_->text());
	
	auto idx = pType_->currentIndex();
	X_ASSERT(idx >= 0 && idx < engine::fx::StageType::ENUM_COUNT, "Invalid index for type combo")(idx, engine::fx::StageType::ENUM_COUNT);

	vis.type = static_cast<engine::fx::StageType::Enum>(idx);
}

void VisualsInfoWidget::currentIndexChanged(int32_t idx)
{
	auto type = static_cast<engine::fx::StageType::Enum>(idx);

	emit typeChanged(type);
}


// -----------------------------------

AngleWidget::AngleWidget(QWidget* parent) :
	QGroupBox("Angle", parent)
{
	QFormLayout* pLayout = new QFormLayout();
	{
		pPitch_ = new SpinBoxRangeDouble(false);
		pYaw_ = new SpinBoxRangeDouble(false);
		pRoll_ = new SpinBoxRangeDouble(false);

		pLayout->addRow(tr("Pitch"), pPitch_);
		pLayout->addRow(tr("Yaw"), pYaw_);
		pLayout->addRow(tr("Roll"), pRoll_);

		connect(pPitch_, &SpinBoxRangeDouble::valueChanged, this, &AngleWidget::valueChanged);
		connect(pYaw_, &SpinBoxRangeDouble::valueChanged, this, &AngleWidget::valueChanged);
		connect(pRoll_, &SpinBoxRangeDouble::valueChanged, this, &AngleWidget::valueChanged);
	}

	setLayout(pLayout);
}

void AngleWidget::setValue(const RotationInfo& rot)
{
	blockSignals(true);

	pPitch_->setValue(rot.pitch);
	pYaw_->setValue(rot.yaw);
	pRoll_->setValue(rot.roll);

	blockSignals(false);
}

void AngleWidget::getValue(RotationInfo& rot)
{
	pPitch_->getValue(rot.pitch);
	pYaw_->getValue(rot.yaw);
	pRoll_->getValue(rot.roll);
}

// -----------------------------------



AssetFxWidget::AssetFxWidget(IAssetEntry* pAssEntry, QWidget *parent) :
	QWidget(parent),
	pAssEntry_(pAssEntry),
	segmentModel_(),
	currentSegment_(-1)
{
//	QHBoxLayout* pLayout = new QHBoxLayout();
//	pLayout->setContentsMargins(0, 0, 0, 0);

	// gonna get crazy up in here!
	// need something to manage stages.
	{
		pSapwn_ = new SpawnInfoWidget();
		pOrigin_ = new OriginInfoWidget();
		pSegments_ = new SegmentListWidget(&segmentModel_);
		pSequence_ = new SequenceInfoWidget();
		pSize0_ = new GraphWithScale("Width/Diamenter");
		pSize1_ = new GraphWithScale("Height/Length");
		pSize1_->setCheckable(true);
		pScale_ = new GraphWithScale("Scale");
		pVisualInfo_ = new VisualsInfoWidget();
		pVerlocityInfo_ = new VelocityInfoWidget();
		pVerlocity0_ = new VelocityGraph();
		pVerlocity1_ = new VelocityGraph();
		pRotation_ = new RotationGraphWidget();
		pAngles_ = new AngleWidget();
		pCol_ = new ColorGraph();
		pAlpha_ = new AlphaGraph();

		const int minHeight = 300;
		const int minWidth = 300;
		const int maxWidth = 600;
		const int maxHeight = 400;

		pSize0_->setMinimumWidth(300);
		pSize1_->setMinimumWidth(300);
		pScale_->setMinimumWidth(300);
		pVerlocity0_->setMinimumWidth(300);
		pVerlocity1_->setMinimumWidth(300);
		pRotation_->setMinimumWidth(300);
		pCol_->setMinimumWidth(300);
		pAlpha_->setMinimumWidth(300);

		pSize0_->setMaximumWidth(maxWidth);
		pSize1_->setMaximumWidth(maxWidth);
		pScale_->setMaximumWidth(maxWidth);
		pVerlocity0_->setMaximumWidth(maxWidth);
		pVerlocity1_->setMaximumWidth(maxWidth);
		pRotation_->setMaximumWidth(maxWidth);
		pCol_->setMaximumWidth(maxWidth);
		pAlpha_->setMaximumWidth(maxWidth);
		

		pSize0_->setMaximumHeight(maxHeight);
		pSize1_->setMaximumHeight(maxHeight);
		pScale_->setMaximumHeight(maxHeight);
		pVerlocity0_->setMaximumHeight(maxHeight + 50);
		pVerlocity1_->setMaximumHeight(maxHeight + 50);
		pRotation_->setMaximumHeight(maxHeight);
		pCol_->setMaximumHeight(maxHeight);
		pAlpha_->setMaximumHeight(maxHeight);

		pAngles_->setMinimumWidth(300);
		pAngles_->setMaximumWidth(300);

		// Spawn stuff
		pVisualInfo_->setMinimumWidth(300);
		pSapwn_->setMinimumWidth(300);
		pOrigin_->setMinimumWidth(300);
		pSequence_->setMinimumWidth(300);
		pVisualInfo_->setMaximumWidth(400);
		pSapwn_->setMaximumWidth(400);
		pOrigin_->setMaximumWidth(400);
		pSequence_->setMaximumWidth(400);


		QVBoxLayout* pSpawnLayout = new QVBoxLayout();
		pSpawnLayout->addWidget(pVisualInfo_);
		pSpawnLayout->addWidget(pSapwn_);
		pSpawnLayout->addWidget(pOrigin_);
		pSpawnLayout->addWidget(pSequence_);
		pSpawnLayout->addStretch(0);

		QVBoxLayout* pSizeLayout = new QVBoxLayout();
		pSizeLayout->addWidget(pSize0_);
		pSizeLayout->addWidget(pSize1_);
		pSizeLayout->addWidget(pScale_);
		pSizeLayout->addStretch(0);

		QVBoxLayout* pVelLayout = new QVBoxLayout();
		pVelLayout->addWidget(pVerlocityInfo_);
		pVelLayout->addWidget(pVerlocity0_);
		pVelLayout->addWidget(pVerlocity1_);
		pVelLayout->addStretch(0);

		QVBoxLayout* pRotLayout = new QVBoxLayout();
		pRotLayout->addWidget(pRotation_);
		pRotLayout->addWidget(pAngles_);
		pRotLayout->addStretch(0);

		QVBoxLayout* pColLayout = new QVBoxLayout();
		pColLayout->addWidget(pCol_);
		pColLayout->addWidget(pAlpha_);
		pColLayout->addStretch(0);

		QWidget* pSpawnTab = new QWidget();
		pSpawnTab->setObjectName("FxEditorTabWidget");
		pSpawnTab->setLayout(pSpawnLayout);

		QWidget* pSizeTab = new QWidget();
		pSizeTab->setObjectName("FxEditorTabWidget");
		pSizeTab->setLayout(pSizeLayout);

		QWidget* pVelTab = new QWidget();
		pVelTab->setObjectName("FxEditorTabWidget");
		pVelTab->setLayout(pVelLayout);

		QWidget* pRotTab = new QWidget();
		pRotTab->setObjectName("FxEditorTabWidget");
		pRotTab->setLayout(pRotLayout);

		QWidget* pColorTab = new QWidget();
		pColorTab->setObjectName("FxEditorTabWidget");
		pColorTab->setLayout(pColLayout);


		QTabWidget* pTabs = new QTabWidget();
		pTabs->addTab(pSpawnTab, "Spawn");
		pTabs->addTab(pSizeTab, "Size");
		pTabs->addTab(pVelTab, "Velocity");
		pTabs->addTab(pRotTab, "Rotation");
		pTabs->addTab(pColorTab, "Color");


		QScrollArea* pScrollArea = new QScrollArea();
		pScrollArea->setObjectName("FxEditor");
		pScrollArea->setWidget(pTabs);
		pScrollArea->setWidgetResizable(true);

#if 0
		pVisualInfo_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
		pSapwn_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
		pOrigin_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
		pSequence_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
#endif


		QSplitter* pSplitter = new QSplitter(Qt::Orientation::Vertical);
		pSplitter->setObjectName("FxEditor");
		pSplitter->addWidget(pScrollArea);
		pSplitter->addWidget(pSegments_);
		pSplitter->setStretchFactor(0, 2);

		QVBoxLayout* pLayout = new QVBoxLayout();
		pLayout->addWidget(pSplitter);


		setLayout(pLayout);

		disableWidgets();

		// HEllloo JERRRYY!!!
		connect(pSegments_, &SegmentListWidget::itemSelectionChanged, this, &AssetFxWidget::segmentSelectionChanged);
		connect(pVisualInfo_, &VisualsInfoWidget::typeChanged, this, &AssetFxWidget::typeChanged);

		connect(pSapwn_, &SpawnInfoWidget::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pOrigin_, &OriginInfoWidget::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pSequence_, &SequenceInfoWidget::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pVisualInfo_, &VisualsInfoWidget::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pRotation_, &RotationGraphWidget::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pVerlocityInfo_, &VelocityInfoWidget::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pVerlocity0_, &VelocityGraph::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pVerlocity1_, &VelocityGraph::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pCol_, &ColorGraph::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pAlpha_, &AlphaGraph::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pSize0_, &GraphWithScale::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pSize1_, &GraphWithScale::valueChanged, this, &AssetFxWidget::onValueChanged);
		connect(pScale_, &GraphWithScale::valueChanged, this, &AssetFxWidget::onValueChanged);

		// groupbox checkbox.
		connect(pSize1_, &GraphWithScale::toggled, this, &AssetFxWidget::onValueChanged);

		connect(&segmentModel_, &FxSegmentModel::dataChanged, this, &AssetFxWidget::onValueChanged);
	}
}


AssetFxWidget::~AssetFxWidget()
{

}

void AssetFxWidget::disableWidgets(void)
{
	pSapwn_->setEnabled(false);
	pOrigin_->setEnabled(false);
	pSequence_->setEnabled(false);
	pVisualInfo_->setEnabled(false);
	pRotation_->setEnabled(false);
	pAngles_->setEnabled(false);
	pVerlocityInfo_->setEnabled(false);
	pVerlocity0_->setEnabled(false);
	pVerlocity1_->setEnabled(false);
	pCol_->setEnabled(false);
	pAlpha_->setEnabled(false);
	pSize0_->setEnabled(false);
	pSize1_->setEnabled(false);
	pScale_->setEnabled(false);
}

void AssetFxWidget::enableWidgets(engine::fx::StageType::Enum type)
{
	pSapwn_->setEnabled(true);
	pOrigin_->setEnabled(true);
	pSequence_->setEnabled(true);
	pVisualInfo_->setEnabled(true);

	// graphs for all.
	pRotation_->setEnabled(true);
	pVerlocityInfo_->setEnabled(true);
	pVerlocity0_->setEnabled(true);
	pVerlocity1_->setEnabled(true);
	pCol_->setEnabled(true);
	pAlpha_->setEnabled(true);
	pSize0_->setEnabled(true);
	pSize1_->setEnabled(true);

	// disaabled for most things currently?
	pScale_->setEnabled(false);

	if (type == engine::fx::StageType::Sound)
	{
		pSize0_->setEnabled(false);
		pSize1_->setEnabled(false);
		pSequence_->setEnabled(false);
	}

	if (type == engine::fx::StageType::RotatedSprite)
	{
		pAngles_->setEnabled(true);
	}
	else
	{
		pAngles_->setEnabled(false);
	}
}



bool AssetFxWidget::setValue(const core::string & value)
{
	blockSignals(true);
	
	if (!segmentModel_.fromJson(value))
	{
		return false;
	}

	if (segmentModel_.numSegments() > 0)
	{
		pSegments_->setActiveIndex(0);
	}

	blockSignals(false);
	return true;
}

void AssetFxWidget::getValue(core::string& value) const
{
	segmentModel_.getJson(value);
}

void AssetFxWidget::segmentSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
	X_UNUSED(deselected);

	if (currentSegment_ >= 0)
	{
		auto& segment = segmentModel_.getSegment(currentSegment_);

		pSapwn_->getValue(segment.spawn);
		pOrigin_->getValue(segment.origin);
		pSequence_->getValue(segment.seq);
		pVisualInfo_->getValue(segment.vis);
		pRotation_->getValue(segment.rot);
		pAngles_->getValue(segment.rot);
		pVerlocityInfo_->getValue(segment.vel);
		pVerlocity0_->getValue(segment.vel.vel0);
		pVerlocity1_->getValue(segment.vel.vel1);
		pCol_->getValue(segment.col);
		pAlpha_->getValue(segment.col);
		pSize0_->getValue(segment.size.size0);
		pSize1_->getValue(segment.size.size1);
		pScale_->getValue(segment.size.scale);

		segment.size.size2Enabled = pSize1_->isChecked();
	}

	if (selected.count() != 1) {
		disableWidgets();
		currentSegment_ = -1;
		return;
	}

	auto indexes = selected.first().indexes();
	int32_t curRow = indexes.first().row();

	if (curRow == currentSegment_) {
		return;
	}

	{
		auto& segment = segmentModel_.getSegment(curRow);

		pSapwn_->setValue(segment.spawn);
		pOrigin_->setValue(segment.origin);
		pSequence_->setValue(segment.seq);
		pVisualInfo_->setValue(segment.vis);
		pRotation_->setValue(segment.rot);
		pAngles_->setValue(segment.rot);
		pVerlocityInfo_->setValue(segment.vel);
		pVerlocity0_->setValue(segment.vel.vel0);
		pVerlocity1_->setValue(segment.vel.vel1);
		pCol_->setValue(segment.col);
		pAlpha_->setValue(segment.col);
		pSize0_->setValue(segment.size.size0);
		pSize1_->setValue(segment.size.size1);
		pScale_->setValue(segment.size.scale);

		pSize1_->setChecked(segment.size.size2Enabled);

		enableWidgets(segment.vis.type);
	}
	currentSegment_ = static_cast<int32_t>(curRow);
}

void AssetFxWidget::typeChanged(engine::fx::StageType::Enum type)
{
	segmentModel_.setSegmentType(currentSegment_, type);

	enableWidgets(type);
}


void AssetFxWidget::onValueChanged(void)
{
	if (currentSegment_ < 0) {
		return;
	}

	auto& segment = segmentModel_.getSegment(currentSegment_);

	// the value has changed :O
	QObject* pSender = sender();

	// so don't know if neater way todo this?
	// each function takes diffrent data :/
	if (pSender == pSapwn_)
	{
		pSapwn_->getValue(segment.spawn);
	}
	else if (pSender == pOrigin_)
	{
		pOrigin_->getValue(segment.origin);
	}
	else if (pSender == pSequence_)
	{
		pSequence_->getValue(segment.seq);
	}
	else if (pSender == pVisualInfo_)
	{
		pVisualInfo_->getValue(segment.vis);
	}
	else if (pSender == pRotation_)
	{
		pRotation_->getValue(segment.rot);
	}
	else if (pSender == pVerlocityInfo_)
	{
		pVerlocityInfo_->getValue(segment.vel);
	}
	else if (pSender == pVerlocity0_)
	{
		pVerlocity0_->getValue(segment.vel.vel0);
	}
	else if (pSender == pVerlocity1_)
	{
		pVerlocity1_->getValue(segment.vel.vel1);
	}
	else if (pSender == pCol_)
	{
		pCol_->getValue(segment.col);
	}
	else if (pSender == pAlpha_)
	{
		pAlpha_->getValue(segment.col);
	}
	else if (pSender == pSize0_)
	{
		pSize0_->getValue(segment.size.size0);
	}
	else if (pSender == pSize1_)
	{
		pSize1_->getValue(segment.size.size1);
		segment.size.size2Enabled = pSize1_->isChecked();
	}
	else if (pSender == pScale_)
	{
		pScale_->getValue(segment.size.scale);
	}
	else if (pSender == pAngles_)
	{
		pAngles_->getValue(segment.rot);
	}
	else if (pSender == &segmentModel_)
	{
		// models already updated.
	}
	else
	{
		X_ASSERT_UNREACHABLE();
	}

	emit valueChanged();
}

X_NAMESPACE_END