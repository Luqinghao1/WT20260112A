/*
 * 文件名: chartwidget.h
 * 文件作用: 通用图表组件头文件
 * 功能描述:
 * 1. 封装 MouseZoom (继承自 QCustomPlot)，提供统一的图表展示界面。
 * 2. 管理图表标题 (QCPTextElement)，确保导出图片时包含标题。
 * 3. 提供单坐标系 (Mode_Single) 和双坐标系 (Mode_Stacked) 的切换功能。
 * 4. 包含鼠标交互逻辑（标注、拖拽等）。
 * 5. 坐标系封闭显示及图例管理。
 * 6. 支持数据移动功能（横向/纵向独立移动），并将改动通知给业务层以持久化。
 * 7. [修改] 优化缩放逻辑，确保横向/纵向缩放模式严格生效，互不干扰。
 */

#ifndef CHARTWIDGET_H
#define CHARTWIDGET_H

#include <QWidget>
#include <QStandardItemModel>
#include <QMenu>
#include <QMap>
#include <QMouseEvent>
#include <QKeyEvent>
#include "mousezoom.h"

namespace Ui {
class ChartWidget;
}

// 标注结构体
struct ChartAnnotation {
    QCPItemText* textItem = nullptr;
    QCPItemLine* arrowItem = nullptr;
};

class ChartWidget : public QWidget
{
    Q_OBJECT

public:
    enum ChartMode {
        Mode_Single = 0, // 单一绘图区（适用于导数分析、通用曲线）
        Mode_Stacked     // 上下堆叠绘图区（适用于压力产量分析）
    };

    explicit ChartWidget(QWidget *parent = nullptr);
    ~ChartWidget();

    // 设置图表标题（显示在图表顶部，导出时可见）
    void setTitle(const QString& title);

    // 获取底层绘图对象
    MouseZoom* getPlot();

    // 设置数据模型（用于交互查询等）
    void setDataModel(QStandardItemModel* model);

    // 设置图表模式（单坐标/双坐标）
    void setChartMode(ChartMode mode);
    ChartMode getChartMode() const;

    // 获取上下坐标轴矩形（用于 Mode_Stacked）
    QCPAxisRect* getTopRect();
    QCPAxisRect* getBottomRect();

    // 清除所有曲线和图例项
    void clearGraphs();

protected:
    // 键盘事件处理，用于退出移动模式
    void keyPressEvent(QKeyEvent *event) override;

signals:
    void exportDataTriggered();
    // 当曲线数据被用户移动并释放鼠标后发出，用于持久化数据
    void graphDataModified(QCPGraph* graph);

private slots:
    // --- UI按钮槽函数 ---
    void on_btnSavePic_clicked();
    void on_btnExportData_clicked();
    void on_btnSetting_clicked();
    void on_btnReset_clicked();
    void on_btnDrawLine_clicked();

    // --- 来自 MouseZoom 的信号处理 ---
    void onAddAnnotationRequested(QCPItemLine* line);
    void onDeleteSelectedRequested();
    void onEditItemRequested(QCPAbstractItem* item);

    // --- 鼠标交互逻辑 ---
    void onPlotMousePress(QMouseEvent* event);
    void onPlotMouseMove(QMouseEvent* event);
    void onPlotMouseRelease(QMouseEvent* event);
    void onPlotMouseDoubleClick(QMouseEvent* event);

    // 启动数据移动模式的槽函数
    void onMoveDataXTriggered();
    void onMoveDataYTriggered();

    // 切换缩放模式的槽函数
    void onZoomHorizontalTriggered();
    void onZoomVerticalTriggered();
    void onZoomDefaultTriggered();

    // --- 业务功能 ---
    void addCharacteristicLine(double slope);
    void addAnnotationToLine(QCPItemLine* line);
    void deleteSelectedItems();

private:
    void initUi();
    void initConnections();

    // 配置坐标轴矩形样式（封闭坐标系）
    void setupAxisRect(QCPAxisRect *rect);

    void calculateLinePoints(double slope, double centerX, double centerY,
                             double& x1, double& y1, double& x2, double& y2,
                             bool isLogX, bool isLogY);
    double distToSegment(const QPointF& p, const QPointF& s, const QPointF& e);
    void constrainLinePoint(QCPItemLine* line, bool isMovingStart, double mouseX, double mouseY);
    void updateAnnotationArrow(QCPItemLine* line);

    // 刷新标题指针（防止设置对话框重建布局后指针失效）
    void refreshTitleElement();

    // 退出数据移动模式
    void exitMoveDataMode();

    // [修改] 设置所有坐标轴矩形的缩放/拖拽模式，并严格配置关联轴
    void setZoomDragMode(Qt::Orientations orientations);

private:
    Ui::ChartWidget *ui;
    MouseZoom* m_plot;
    QStandardItemModel* m_dataModel;
    QMenu* m_lineMenu;
    QCPTextElement* m_titleElement; // 图表标题元素

    ChartMode m_chartMode;
    QCPAxisRect* m_topRect;
    QCPAxisRect* m_bottomRect;

    QMap<QCPItemLine*, ChartAnnotation> m_annotations;

    enum InteractionMode {
        Mode_None,
        Mode_Dragging_Line,
        Mode_Dragging_Start,
        Mode_Dragging_End,
        Mode_Dragging_Text,
        Mode_Dragging_ArrowStart,
        Mode_Dragging_ArrowEnd,
        Mode_Moving_Data_X, // 数据横向移动
        Mode_Moving_Data_Y  // 数据纵向移动
    };

    InteractionMode m_interMode;
    QCPItemLine* m_activeLine;
    QCPItemText* m_activeText;
    QCPItemLine* m_activeArrow;
    QPointF m_lastMousePos;

    // 数据移动相关成员变量
    QCPGraph* m_movingGraph; // 当前正在移动的曲线
    QPoint m_lastMoveDataPos; // 上一次鼠标位置（像素）
};

#endif // CHARTWIDGET_H
