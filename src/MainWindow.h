// ---------------------------------------------------------------------------
//  StickCore  –  MainWindow.h
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include <QMainWindow>
#include <memory>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QSlider;

namespace stick {

class StitchGLWidget;
class PathEditorWidget;
class QrUploadServer;
class DesignLibrary;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    static QString styleSheet();      ///< the app-wide dark theme

    void captureManualScreenshots(const QString& outDir);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void modeChanged(int index);
    void generate();
    void loadSatinDemo();
    void loadTatamiDemo();
    void makeText();
    void makeMonogram();
    void makeLogo();
    void loadMefOriginal(int style = 0);
    void importSvg();
    void makeApplique();
    void digitizeImage();
    void resizeDesign();
    void placeDesign();
    void exportSizeVariants();
    void phoneUpload();
    void openLibrary();
    void saveToLibrary();
    void showWizard();
    void maybeShowWizardOnStart();
    void exportJef();
    void exportDst();
    void exportWorksheet();
    void loadHuntingMotif(int typeIdx, bool editable);

    // Embird Flagship Assistants
    void smartColorSort();
    void addBastingBox();
    void digitizeSfumato();
    void digitizeCrossStitch();
    void multiHoopSplit();
    void applyTatamiCarving();

    // Janome Digitizer Jr Transformations
    void mirrorHorizontal();
    void mirrorVertical();
    void rotate90(bool clockwise = true);
    void rotateFree();
    void setCurrentSequence(const StitchSequence& s, const QString& label = QString());

    // Studio Player / Transport slots
    void togglePlayback();
    void onPlayerProgress(int current, int total);
    void onPlayerStateChanged(bool playing);
    void onScrubSliderMoved(int val);
    void onThreadItemDoubleClicked(QListWidgetItem* item);

    // Studio View & Position controls
    void toggleHoopVisible();
    void toggleViewPreset();
    void centerCurrentDesign();
    void onPositionSpinChanged();
    void onHoopComboChanged(int idx);
    void selectHoop(HoopType type);

private:
    void     setupMenus();
    QWidget* buildCenterView();
    QWidget* buildInfoPanel();
    void     updateInfoPanel();
    void     digitizeFrom(const QImage& img, const QString& label);
    void     digitizeWithDialog(const QImage& img, const QString& label);
    void     setStatusForSequence(const QString& prefix = QString());

    PathEditorWidget* m_editor = nullptr;
    StitchGLWidget*   m_view   = nullptr;
    QComboBox*        m_mode   = nullptr;
    QrUploadServer*   m_upload = nullptr;
    QLabel*           m_uploadStatus = nullptr;
    std::unique_ptr<DesignLibrary> m_library;

    // Transport / Player controls
    QPushButton*      m_playBtn     = nullptr;
    QPushButton*      m_resetBtn    = nullptr;
    QPushButton*      m_stepBackBtn = nullptr;
    QPushButton*      m_stepFwdBtn  = nullptr;
    QSlider*          m_scrubSlider = nullptr;
    QLabel*           m_scrubLabel  = nullptr;
    QComboBox*        m_speedCombo  = nullptr;

    // View & Movement actions
    QAction*          m_actHoop     = nullptr;
    QMenu*            m_hoopMenu    = nullptr;
    QActionGroup*     m_hoopActionGroup = nullptr;
    QAction*          m_actTopDown  = nullptr;
    QAction*          m_actMoveMode = nullptr;
    QAction*          m_actOrbitMode = nullptr;

    // info / properties panel
    QComboBox*        m_hoopCombo   = nullptr;
    QLabel*           m_sizeLabel   = nullptr;
    QLabel*           m_fitsLabel   = nullptr;
    QDoubleSpinBox*   m_posXSpin    = nullptr;
    QDoubleSpinBox*   m_posYSpin    = nullptr;
    QLabel*           m_threadUsageLabel = nullptr;
    QLabel*           m_timeEstLabel     = nullptr;
    QListWidget*      m_threadList  = nullptr;
    QDoubleSpinBox*   m_density     = nullptr;
    QDoubleSpinBox*   m_fillAngleSpin = nullptr;
    QComboBox*        m_patternCombo  = nullptr;
    QDoubleSpinBox*   m_pull        = nullptr;
    QDoubleSpinBox*   m_maxStitch   = nullptr;
    QCheckBox*        m_underlay    = nullptr;

    StitchSequence    m_current;
};

} // namespace stick
