/*
 * 文件名: chartwidget.cpp
 * 文件作用: 通用图表组件实现文件
 * 功能描述:
 * 1. 实现了图表标题的嵌入显示，解决导出无标题问题。
 * 2. 导出图片时默认定位到项目文件夹 (通过 ModelParameter)。
 * 3. 实现了坐标系封闭（显示上、右轴）和图例显示。
 * 4. 强化了标题更新逻辑，确保设置后标题变化立即生效。
 * 5. 在双坐标系模式下，实现了上下图表X轴的同步缩放。
 * 6. 实现了数据移动功能：细分为横向/纵向，Esc键退出，支持持久化。
 * 7. [修改] 修复了右键菜单缩放方向不锁定问题，明确了缩放轴配置。
 */

#include "chartwidget.h"
#include "ui_chartwidget.h"
#include "chartsetting1.h"
#include "modelparameter.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QInputDialog>
#include <cmath>

ChartWidget::ChartWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ChartWidget),
    m_dataModel(nullptr),
    m_titleElement(nullptr),
    m_chartMode(Mode_Single),
    m_topRect(nullptr),
    m_bottomRect(nullptr),
    m_interMode(Mode_None),
    m_activeLine(nullptr),
    m_activeText(nullptr),
    m_activeArrow(nullptr),
    m_movingGraph(nullptr)
{
    ui->setupUi(this);
    m_plot = ui->chart; // ui->chart 是 promoted 的 MouseZoom 类型

    // 允许 widget 接收键盘焦点，以便响应 Esc 键
    this->setFocusPolicy(Qt::StrongFocus);
    m_plot->setFocusPolicy(Qt::StrongFocus);

    initUi();
    initConnections();
}

ChartWidget::~ChartWidget()
{
    delete ui;
}

void ChartWidget::initUi()
{
    // 1. 初始化标题元素
    if (m_plot->plotLayout()->rowCount() == 0) m_plot->plotLayout()->insertRow(0);

    if (m_plot->plotLayout()->elementCount() > 0 && qobject_cast<QCPTextElement*>(m_plot->plotLayout()->element(0, 0))) {
        m_titleElement = qobject_cast<QCPTextElement*>(m_plot->plotLayout()->element(0, 0));
    } else {
        if(m_plot->plotLayout()->element(0,0) != nullptr) {
            m_plot->plotLayout()->insertRow(0);
        }
        m_titleElement = new QCPTextElement(m_plot, "", QFont("Microsoft YaHei", 12, QFont::Bold));
        m_plot->plotLayout()->addElement(0, 0, m_titleElement);
    }

    // 2. 配置默认坐标系
    setupAxisRect(m_plot->axisRect());

    // 3. 配置图例
    m_plot->legend->setVisible(true);
    QFont legendFont("Microsoft YaHei", 9);
    m_plot->legend->setFont(legendFont);
    m_plot->legend->setBrush(QBrush(QColor(255, 255, 255, 200)));

    // 4. 菜单初始化
    m_lineMenu = new QMenu(this);
    QAction* actSlope1 = m_lineMenu->addAction("斜率 k = 1 (井筒储集)");
    connect(actSlope1, &QAction::triggered, this, [=](){ addCharacteristicLine(1.0); });

    QAction* actSlopeHalf = m_lineMenu->addAction("斜率 k = 1/2 (线性流)");
    connect(actSlopeHalf, &QAction::triggered, this, [=](){ addCharacteristicLine(0.5); });

    QAction* actSlopeQuarter = m_lineMenu->addAction("斜率 k = 1/4 (双线性流)");
    connect(actSlopeQuarter, &QAction::triggered, this, [=](){ addCharacteristicLine(0.25); });

    QAction* actHorizontal = m_lineMenu->addAction("水平线 (径向流)");
    connect(actHorizontal, &QAction::triggered, this, [=](){ addCharacteristicLine(0.0); });

    // 5. 基础交互设置（默认全向缩放）
    setZoomDragMode(Qt::Horizontal | Qt::Vertical);
}

void ChartWidget::setupAxisRect(QCPAxisRect *rect)
{
    if (!rect) return;

    // 配置顶部坐标轴
    QCPAxis *topAxis = rect->axis(QCPAxis::atTop);
    topAxis->setVisible(true);
    topAxis->setTickLabels(false);
    connect(rect->axis(QCPAxis::atBottom), SIGNAL(rangeChanged(QCPRange)), topAxis, SLOT(setRange(QCPRange)));

    // 配置右侧坐标轴
    QCPAxis *rightAxis = rect->axis(QCPAxis::atRight);
    rightAxis->setVisible(true);
    rightAxis->setTickLabels(false);
    connect(rect->axis(QCPAxis::atLeft), SIGNAL(rangeChanged(QCPRange)), rightAxis, SLOT(setRange(QCPRange)));
}

void ChartWidget::initConnections()
{
    connect(m_plot, &MouseZoom::saveImageRequested, this, &ChartWidget::on_btnSavePic_clicked);
    connect(m_plot, &MouseZoom::exportDataRequested, this, &ChartWidget::on_btnExportData_clicked);
    connect(m_plot, &MouseZoom::drawLineRequested, this, &ChartWidget::addCharacteristicLine);
    connect(m_plot, &MouseZoom::settingsRequested, this, &ChartWidget::on_btnSetting_clicked);
    connect(m_plot, &MouseZoom::resetViewRequested, this, &ChartWidget::on_btnReset_clicked);

    connect(m_plot, &MouseZoom::addAnnotationRequested, this, &ChartWidget::onAddAnnotationRequested);
    connect(m_plot, &MouseZoom::deleteSelectedRequested, this, &ChartWidget::onDeleteSelectedRequested);
    connect(m_plot, &MouseZoom::editItemRequested, this, &ChartWidget::onEditItemRequested);

    connect(m_plot, &QCustomPlot::mousePress, this, &ChartWidget::onPlotMousePress);
    connect(m_plot, &QCustomPlot::mouseMove, this, &ChartWidget::onPlotMouseMove);
    connect(m_plot, &QCustomPlot::mouseRelease, this, &ChartWidget::onPlotMouseRelease);
    connect(m_plot, &QCustomPlot::mouseDoubleClick, this, &ChartWidget::onPlotMouseDoubleClick);
}

void ChartWidget::setTitle(const QString &title) {
    refreshTitleElement();
    if (m_titleElement) {
        m_titleElement->setText(title);
        m_plot->replot();
    }
}

void ChartWidget::refreshTitleElement() {
    m_titleElement = nullptr;
    if (m_plot->plotLayout()->elementCount() > 0) {
        if (auto el = qobject_cast<QCPTextElement*>(m_plot->plotLayout()->element(0, 0))) {
            m_titleElement = el;
            return;
        }
        for (int i = 0; i < m_plot->plotLayout()->elementCount(); ++i) {
            if (auto el = qobject_cast<QCPTextElement*>(m_plot->plotLayout()->elementAt(i))) {
                m_titleElement = el;
                return;
            }
        }
    }
}

MouseZoom *ChartWidget::getPlot() { return m_plot; }
void ChartWidget::setDataModel(QStandardItemModel *model) { m_dataModel = model; }

void ChartWidget::clearGraphs() {
    m_plot->clearGraphs();
    m_plot->replot();
    exitMoveDataMode();
    // 恢复默认缩放
    setZoomDragMode(Qt::Horizontal | Qt::Vertical);
}

void ChartWidget::setChartMode(ChartMode mode) {
    if (m_chartMode == mode) return;
    m_chartMode = mode;

    exitMoveDataMode();

    int rowCount = m_plot->plotLayout()->rowCount();
    for(int i = rowCount - 1; i > 0; --i) {
        m_plot->plotLayout()->removeAt(i);
    }
    m_plot->plotLayout()->simplify();

    if (mode == Mode_Single) {
        QCPAxisRect* defaultRect = new QCPAxisRect(m_plot);
        m_plot->plotLayout()->addElement(1, 0, defaultRect);
        setupAxisRect(defaultRect);
        m_topRect = nullptr;
        m_bottomRect = nullptr;
        setZoomDragMode(Qt::Horizontal | Qt::Vertical);
    } else if (mode == Mode_Stacked) {
        m_topRect = new QCPAxisRect(m_plot);
        m_bottomRect = new QCPAxisRect(m_plot);

        m_plot->plotLayout()->addElement(1, 0, m_topRect);
        m_plot->plotLayout()->addElement(2, 0, m_bottomRect);

        setupAxisRect(m_topRect);
        setupAxisRect(m_bottomRect);

        // 初始化缩放/拖拽模式为全向
        setZoomDragMode(Qt::Horizontal | Qt::Vertical);

        // 同步X轴
        connect(m_topRect->axis(QCPAxis::atBottom), SIGNAL(rangeChanged(QCPRange)),
                m_bottomRect->axis(QCPAxis::atBottom), SLOT(setRange(QCPRange)));
        connect(m_bottomRect->axis(QCPAxis::atBottom), SIGNAL(rangeChanged(QCPRange)),
                m_topRect->axis(QCPAxis::atBottom), SLOT(setRange(QCPRange)));
    }
    m_plot->replot();
}

ChartWidget::ChartMode ChartWidget::getChartMode() const { return m_chartMode; }
QCPAxisRect* ChartWidget::getTopRect() {
    if (m_chartMode == Mode_Single) return m_plot->axisRect();
    return m_topRect;
}
QCPAxisRect* ChartWidget::getBottomRect() {
    if (m_chartMode == Mode_Single) return nullptr;
    return m_bottomRect;
}

void ChartWidget::on_btnSavePic_clicked()
{
    QString dir = ModelParameter::instance()->getProjectPath();
    if (dir.isEmpty()) dir = QDir::currentPath();
    QString fileName = QFileDialog::getSaveFileName(this, "保存图片", dir + "/chart_export.png", "PNG (*.png);;JPG (*.jpg);;PDF (*.pdf)");
    if (fileName.isEmpty()) return;
    if (fileName.endsWith(".png")) m_plot->savePng(fileName);
    else if (fileName.endsWith(".jpg")) m_plot->saveJpg(fileName);
    else m_plot->savePdf(fileName);
}

void ChartWidget::on_btnExportData_clicked() { emit exportDataTriggered(); }

void ChartWidget::on_btnSetting_clicked() {
    ChartSetting1 dlg(m_plot, nullptr, this);
    dlg.exec();
    refreshTitleElement();
    m_plot->replot();
}

void ChartWidget::on_btnReset_clicked() {
    m_plot->rescaleAxes();
    // 恢复默认缩放模式
    setZoomDragMode(Qt::Horizontal | Qt::Vertical);
    if(m_plot->xAxis->scaleType()==QCPAxis::stLogarithmic && m_plot->xAxis->range().lower<=0) m_plot->xAxis->setRangeLower(1e-3);
    if(m_plot->yAxis->scaleType()==QCPAxis::stLogarithmic && m_plot->yAxis->range().lower<=0) m_plot->yAxis->setRangeLower(1e-3);
    m_plot->replot();
}
void ChartWidget::on_btnDrawLine_clicked() { m_lineMenu->exec(ui->btnDrawLine->mapToGlobal(QPoint(0, ui->btnDrawLine->height()))); }

// ---------------- 标识线逻辑 ----------------
void ChartWidget::addCharacteristicLine(double slope) {
    QCPAxisRect* rect = (m_chartMode == Mode_Stacked && m_topRect) ? m_topRect : m_plot->axisRect();
    double lowerX = rect->axis(QCPAxis::atBottom)->range().lower;
    double upperX = rect->axis(QCPAxis::atBottom)->range().upper;
    double lowerY = rect->axis(QCPAxis::atLeft)->range().lower;
    double upperY = rect->axis(QCPAxis::atLeft)->range().upper;

    bool isLogX = (rect->axis(QCPAxis::atBottom)->scaleType() == QCPAxis::stLogarithmic);
    bool isLogY = (rect->axis(QCPAxis::atLeft)->scaleType() == QCPAxis::stLogarithmic);

    double centerX = isLogX ? pow(10, (log10(lowerX) + log10(upperX)) / 2.0) : (lowerX + upperX) / 2.0;
    double centerY = isLogY ? pow(10, (log10(lowerY) + log10(upperY)) / 2.0) : (lowerY + upperY) / 2.0;

    double x1, y1, x2, y2;
    calculateLinePoints(slope, centerX, centerY, x1, y1, x2, y2, isLogX, isLogY);

    QCPItemLine* line = new QCPItemLine(m_plot);
    line->setClipAxisRect(rect);
    line->start->setCoords(x1, y1);
    line->end->setCoords(x2, y2);
    QPen pen(Qt::black, 2, Qt::DashLine);
    line->setPen(pen);
    line->setSelectedPen(QPen(Qt::blue, 2, Qt::SolidLine));
    line->setProperty("fixedSlope", slope);
    line->setProperty("isLogLog", (isLogX && isLogY));
    line->setProperty("isCharacteristic", true);
    m_plot->replot();
}

void ChartWidget::calculateLinePoints(double slope, double centerX, double centerY, double& x1, double& y1, double& x2, double& y2, bool isLogX, bool isLogY) {
    if (isLogX && isLogY) {
        double span = 3.0;
        x1 = centerX / span; x2 = centerX * span;
        y1 = centerY * pow(x1 / centerX, slope); y2 = centerY * pow(x2 / centerX, slope);
    } else {
        QCPAxisRect* rect = m_plot->axisRect();
        x1 = rect->axis(QCPAxis::atBottom)->range().lower;
        x2 = rect->axis(QCPAxis::atBottom)->range().upper;
        y1 = centerY; y2 = centerY;
    }
}

// ---------------- 鼠标交互逻辑 ----------------
double ChartWidget::distToSegment(const QPointF& p, const QPointF& s, const QPointF& e) {
    double l2 = (s.x()-e.x())*(s.x()-e.x()) + (s.y()-e.y())*(s.y()-e.y());
    if (l2 == 0) return std::sqrt((p.x()-s.x())*(p.x()-s.x()) + (p.y()-s.y())*(p.y()-s.y()));
    double t = ((p.x()-s.x())*(e.x()-s.x()) + (p.y()-s.y())*(e.y()-s.y())) / l2;
    t = std::max(0.0, std::min(1.0, t));
    QPointF proj = s + t * (e - s);
    return std::sqrt((p.x()-proj.x())*(p.x()-proj.x()) + (p.y()-proj.y())*(p.y()-proj.y()));
}

void ChartWidget::onPlotMousePress(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        // [修改] 仅在双坐标系下显示扩展菜单
        if (m_chartMode == Mode_Stacked) {
            QMenu menu(this);
            // 数据移动功能
            QAction* actMoveX = menu.addAction("数据横向移动 (X Only)");
            connect(actMoveX, &QAction::triggered, this, &ChartWidget::onMoveDataXTriggered);
            QAction* actMoveY = menu.addAction("数据纵向移动 (Y Only)");
            connect(actMoveY, &QAction::triggered, this, &ChartWidget::onMoveDataYTriggered);

            menu.addSeparator();

            // 缩放功能
            QAction* actZoomH = menu.addAction("横向缩放 (Horizontal)");
            connect(actZoomH, &QAction::triggered, this, &ChartWidget::onZoomHorizontalTriggered);
            QAction* actZoomV = menu.addAction("纵向缩放 (Vertical)");
            connect(actZoomV, &QAction::triggered, this, &ChartWidget::onZoomVerticalTriggered);
            QAction* actZoomDef = menu.addAction("整体缩放 (Default)");
            connect(actZoomDef, &QAction::triggered, this, &ChartWidget::onZoomDefaultTriggered);

            menu.exec(event->globalPosition().toPoint());
            return;
        }
    }

    if (event->button() != Qt::LeftButton) return;

    // [修改] 数据移动模式下的处理
    if (m_interMode == Mode_Moving_Data_X || m_interMode == Mode_Moving_Data_Y) {
        QCPAxisRect* clickedRect = m_plot->axisRectAt(event->pos());
        if (clickedRect) {
            QList<QCPGraph*> graphs = clickedRect->graphs();
            if (!graphs.isEmpty()) {
                m_movingGraph = graphs.first();
                m_lastMoveDataPos = event->pos();
            } else {
                m_movingGraph = nullptr;
            }
        }
        return; // 移动模式下不处理其他交互
    }

    m_interMode = Mode_None; m_activeLine = nullptr; m_activeText = nullptr; m_activeArrow = nullptr; m_lastMousePos = event->pos();
    double tolerance = 8.0;

    for (int i = 0; i < m_plot->itemCount(); ++i) {
        if (auto text = qobject_cast<QCPItemText*>(m_plot->item(i))) {
            if (text->selectTest(event->pos(), false) < tolerance) {
                m_interMode = Mode_Dragging_Text; m_activeText = text;
                m_plot->deselectAll(); text->setSelected(true); m_plot->setInteractions(QCP::Interaction(0));
                m_plot->replot(); return;
            }
        }
    }
    for (int i = 0; i < m_plot->itemCount(); ++i) {
        auto line = qobject_cast<QCPItemLine*>(m_plot->item(i));
        if (line && !line->property("isCharacteristic").isValid()) {
            double x1 = m_plot->xAxis->coordToPixel(line->start->coords().x()), y1 = m_plot->yAxis->coordToPixel(line->start->coords().y());
            double x2 = m_plot->xAxis->coordToPixel(line->end->coords().x()), y2 = m_plot->yAxis->coordToPixel(line->end->coords().y());
            QPointF p(event->pos());
            if (std::sqrt(pow(p.x()-x1,2)+pow(p.y()-y1,2)) < tolerance) { m_interMode=Mode_Dragging_ArrowStart; m_activeArrow=line; m_plot->setInteractions(QCP::Interaction(0)); return; }
            if (std::sqrt(pow(p.x()-x2,2)+pow(p.y()-y2,2)) < tolerance) { m_interMode=Mode_Dragging_ArrowEnd; m_activeArrow=line; m_plot->setInteractions(QCP::Interaction(0)); return; }
        }
    }
    for (int i = 0; i < m_plot->itemCount(); ++i) {
        QCPItemLine* line = qobject_cast<QCPItemLine*>(m_plot->item(i));
        if (!line || !line->property("isCharacteristic").isValid()) continue;
        double x1 = m_plot->xAxis->coordToPixel(line->start->coords().x()), y1 = m_plot->yAxis->coordToPixel(line->start->coords().y());
        double x2 = m_plot->xAxis->coordToPixel(line->end->coords().x()), y2 = m_plot->yAxis->coordToPixel(line->end->coords().y());
        QPointF p(event->pos());
        if (std::sqrt(pow(p.x()-x1,2)+pow(p.y()-y1,2)) < tolerance) { m_interMode=Mode_Dragging_Start; m_activeLine=line; }
        else if (std::sqrt(pow(p.x()-x2,2)+pow(p.y()-y2,2)) < tolerance) { m_interMode=Mode_Dragging_End; m_activeLine=line; }
        else if (distToSegment(p, QPointF(x1,y1), QPointF(x2,y2)) < tolerance) { m_interMode=Mode_Dragging_Line; m_activeLine=line; }

        if (m_interMode != Mode_None) { m_plot->deselectAll(); line->setSelected(true); m_plot->setInteractions(QCP::Interaction(0)); m_plot->replot(); return; }
    }

    m_plot->deselectAll();
    m_plot->replot();
}

void ChartWidget::onPlotMouseMove(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        QPointF currentPos = event->pos();
        QPointF delta = currentPos - m_lastMousePos;
        double mouseX = m_plot->xAxis->pixelToCoord(currentPos.x()), mouseY = m_plot->yAxis->pixelToCoord(currentPos.y());

        // [修改] 数据移动模式下的逻辑
        if ((m_interMode == Mode_Moving_Data_X || m_interMode == Mode_Moving_Data_Y) && m_movingGraph) {
            QCPAxis *xAxis = m_movingGraph->keyAxis();
            QCPAxis *yAxis = m_movingGraph->valueAxis();

            double dx = 0;
            double dy = 0;

            if (m_interMode == Mode_Moving_Data_X) {
                dx = xAxis->pixelToCoord(event->pos().x()) - xAxis->pixelToCoord(m_lastMoveDataPos.x());
            } else {
                dy = yAxis->pixelToCoord(event->pos().y()) - yAxis->pixelToCoord(m_lastMoveDataPos.y());
            }

            QSharedPointer<QCPGraphDataContainer> data = m_movingGraph->data();
            for (auto it = data->begin(); it != data->end(); ++it) {
                if (m_interMode == Mode_Moving_Data_X) it->key += dx;
                else it->value += dy;
            }

            m_plot->replot();
            m_lastMoveDataPos = event->pos();
            return;
        }

        // 现有逻辑
        if (m_interMode == Mode_Dragging_Text && m_activeText) {
            double px = m_plot->xAxis->coordToPixel(m_activeText->position->coords().x()) + delta.x();
            double py = m_plot->yAxis->coordToPixel(m_activeText->position->coords().y()) + delta.y();
            m_activeText->position->setCoords(m_plot->xAxis->pixelToCoord(px), m_plot->yAxis->pixelToCoord(py));
        }
        else if (m_interMode == Mode_Dragging_ArrowStart && m_activeArrow) {
            if(m_activeArrow->start->parentAnchor()) m_activeArrow->start->setParentAnchor(nullptr);
            m_activeArrow->start->setCoords(mouseX, mouseY);
        } else if (m_interMode == Mode_Dragging_ArrowEnd && m_activeArrow) {
            if(m_activeArrow->end->parentAnchor()) m_activeArrow->end->setParentAnchor(nullptr);
            m_activeArrow->end->setCoords(mouseX, mouseY);
        }
        else if (m_interMode == Mode_Dragging_Line && m_activeLine) {
            double sPx = m_plot->xAxis->coordToPixel(m_activeLine->start->coords().x()) + delta.x();
            double sPy = m_plot->yAxis->coordToPixel(m_activeLine->start->coords().y()) + delta.y();
            double ePx = m_plot->xAxis->coordToPixel(m_activeLine->end->coords().x()) + delta.x();
            double ePy = m_plot->yAxis->coordToPixel(m_activeLine->end->coords().y()) + delta.y();
            m_activeLine->start->setCoords(m_plot->xAxis->pixelToCoord(sPx), m_plot->yAxis->pixelToCoord(sPy));
            m_activeLine->end->setCoords(m_plot->xAxis->pixelToCoord(ePx), m_plot->yAxis->pixelToCoord(ePy));
            updateAnnotationArrow(m_activeLine);
        }
        else if ((m_interMode == Mode_Dragging_Start || m_interMode == Mode_Dragging_End) && m_activeLine) {
            constrainLinePoint(m_activeLine, m_interMode == Mode_Dragging_Start, mouseX, mouseY);
        }

        m_lastMousePos = currentPos; m_plot->replot();
    }
}

void ChartWidget::onPlotMouseRelease(QMouseEvent* event) {
    Q_UNUSED(event);
    if (m_interMode == Mode_Moving_Data_X || m_interMode == Mode_Moving_Data_Y) {
        // 释放鼠标时，通知业务层数据已修改，用于持久化
        if (m_movingGraph) {
            emit graphDataModified(m_movingGraph);
        }
        m_movingGraph = nullptr;
    } else {
        m_interMode = Mode_None;
    }
}

void ChartWidget::onPlotMouseDoubleClick(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    for (int i = 0; i < m_plot->itemCount(); ++i) {
        if (auto text = qobject_cast<QCPItemText*>(m_plot->item(i))) {
            if (text->selectTest(event->pos(), false) < 10.0) { onEditItemRequested(text); return; }
        }
    }
}

// 启动数据横向移动模式
void ChartWidget::onMoveDataXTriggered() {
    m_interMode = Mode_Moving_Data_X;
    m_plot->setInteractions(QCP::Interaction(0));
    m_plot->setCursor(Qt::SizeHorCursor);
    QMessageBox::information(this, "提示", "已进入横向数据移动模式。\n按 ESC 键退出此模式。");
    m_plot->setFocus();
    this->setFocus();
}

// 启动数据纵向移动模式
void ChartWidget::onMoveDataYTriggered() {
    m_interMode = Mode_Moving_Data_Y;
    m_plot->setInteractions(QCP::Interaction(0));
    m_plot->setCursor(Qt::SizeVerCursor);
    QMessageBox::information(this, "提示", "已进入纵向数据移动模式。\n按 ESC 键退出此模式。");
    m_plot->setFocus();
    this->setFocus();
}

// 切换到横向缩放模式
void ChartWidget::onZoomHorizontalTriggered() {
    setZoomDragMode(Qt::Horizontal);
}

// 切换到纵向缩放模式
void ChartWidget::onZoomVerticalTriggered() {
    setZoomDragMode(Qt::Vertical);
}

// 切换到整体缩放模式
void ChartWidget::onZoomDefaultTriggered() {
    setZoomDragMode(Qt::Horizontal | Qt::Vertical);
}

// [修改] 核心函数：设置缩放/拖拽模式，并严格配置关联轴
void ChartWidget::setZoomDragMode(Qt::Orientations orientations) {
    m_plot->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom | QCP::iSelectItems);

    auto configureRect = [&](QCPAxisRect* rect) {
        if(!rect) return;
        rect->setRangeDrag(orientations);
        rect->setRangeZoom(orientations);

        // 显式配置缩放轴：如果 flag 中包含方向，则使用对应轴；否则设为 nullptr 禁止缩放
        QCPAxis *hAxis = (orientations & Qt::Horizontal) ? rect->axis(QCPAxis::atBottom) : nullptr;
        QCPAxis *vAxis = (orientations & Qt::Vertical) ? rect->axis(QCPAxis::atLeft) : nullptr;

        rect->setRangeDragAxes(hAxis, vAxis);
        rect->setRangeZoomAxes(hAxis, vAxis);
    };

    if (m_chartMode == Mode_Stacked) {
        configureRect(m_topRect);
        configureRect(m_bottomRect);
    } else {
        configureRect(m_plot->axisRect());
    }
}

void ChartWidget::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape && (m_interMode == Mode_Moving_Data_X || m_interMode == Mode_Moving_Data_Y)) {
        exitMoveDataMode();
    }
    QWidget::keyPressEvent(event);
}

void ChartWidget::exitMoveDataMode() {
    if (m_interMode == Mode_Moving_Data_X || m_interMode == Mode_Moving_Data_Y) {
        m_interMode = Mode_None;
        m_movingGraph = nullptr;
        m_plot->setCursor(Qt::ArrowCursor);
        // 恢复之前的缩放模式 (默认全向)
        setZoomDragMode(Qt::Horizontal | Qt::Vertical);
    }
}

void ChartWidget::constrainLinePoint(QCPItemLine* line, bool isMovingStart, double mouseX, double mouseY) {
    double k = line->property("fixedSlope").toDouble();
    bool isLogLog = line->property("isLogLog").toBool();
    double xFixed = isMovingStart ? line->end->coords().x() : line->start->coords().x();
    double yFixed = isMovingStart ? line->end->coords().y() : line->start->coords().y();
    double yNew;
    if (isLogLog) {
        if (xFixed <= 0) xFixed = 1e-5;
        if (mouseX <= 0) mouseX = 1e-5;
        yNew = yFixed * pow(mouseX / xFixed, k);
    } else {
        QCPAxisRect* rect = m_plot->axisRect();
        double scale = rect->axis(QCPAxis::atLeft)->range().size() / rect->axis(QCPAxis::atBottom)->range().size();
        yNew = yFixed + (k * scale) * (mouseX - xFixed);
    }
    if (isMovingStart) line->start->setCoords(mouseX, yNew); else line->end->setCoords(mouseX, yNew);
}

void ChartWidget::updateAnnotationArrow(QCPItemLine* line) {
    if (m_annotations.contains(line)) {
        ChartAnnotation note = m_annotations[line];
        double midX = (line->start->coords().x() + line->end->coords().x()) / 2.0;
        double midY = (line->start->coords().y() + line->end->coords().y()) / 2.0;
        if(note.arrowItem) note.arrowItem->end->setCoords(midX, midY);
        if(note.textItem) {
            double dx = midX - line->start->coords().x();
            double dy = midY - line->start->coords().y();
            note.textItem->position->setCoords(midX + dx*0.2, midY + dy*0.2);
        }
    }
}

void ChartWidget::onAddAnnotationRequested(QCPItemLine* line) { addAnnotationToLine(line); }
void ChartWidget::onDeleteSelectedRequested() { deleteSelectedItems(); }

void ChartWidget::onEditItemRequested(QCPAbstractItem* item) {
    if (auto text = qobject_cast<QCPItemText*>(item)) {
        bool ok;
        QString newContent = QInputDialog::getText(this, "修改标注", "内容:", QLineEdit::Normal, text->text(), &ok);
        if (ok && !newContent.isEmpty()) { text->setText(newContent); m_plot->replot(); }
    }
}

void ChartWidget::addAnnotationToLine(QCPItemLine* line) {
    if (!line) return;
    if (m_annotations.contains(line)) {
        ChartAnnotation old = m_annotations.take(line);
        if(old.textItem) m_plot->removeItem(old.textItem);
        if(old.arrowItem) m_plot->removeItem(old.arrowItem);
    }
    double k = line->property("fixedSlope").toDouble();
    bool ok;
    QString text = QInputDialog::getText(this, "添加标注", "输入:", QLineEdit::Normal, QString("k=%1").arg(k), &ok);
    if (!ok || text.isEmpty()) return;

    QCPItemText* txt = new QCPItemText(m_plot);
    txt->setText(text);
    txt->position->setType(QCPItemPosition::ptPlotCoords);
    double midX = (line->start->coords().x() + line->end->coords().x()) / 2.0;
    double midY = (line->start->coords().y() + line->end->coords().y()) / 2.0;
    txt->position->setCoords(midX, midY * 1.5);

    QCPItemLine* arr = new QCPItemLine(m_plot);
    arr->setHead(QCPLineEnding::esSpikeArrow);
    arr->start->setParentAnchor(txt->bottom);
    arr->end->setCoords(midX, midY);

    ChartAnnotation note; note.textItem = txt; note.arrowItem = arr;
    m_annotations.insert(line, note);
    m_plot->replot();
}

void ChartWidget::deleteSelectedItems() {
    auto items = m_plot->selectedItems();
    for (auto item : items) {
        m_plot->removeItem(item);
    }
    m_plot->replot();
}
