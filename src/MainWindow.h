// ---------------------------------------------------------------------------
//  StickCore  –  MainWindow.h
// ---------------------------------------------------------------------------
#pragma once

#include "core/StitchTypes.h"
#include "generators/TextDigitizer.h"
#include <QMainWindow>
#include <QPointer>
#include <memory>

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QSlider;
class QDialog;

class QDragEnterEvent;
class QDropEvent;

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

    DesignLibrary* library() const { return m_library.get(); }
    const StitchSequence& currentSequence() const { return m_current; }

    void captureManualScreenshots(const QString& outDir);

public slots:
    bool loadFile(const QString& filePath);
    void openEmbroideryFile();
    void saveFile();
    void saveFileAs();
    void convertStitchesToPaths();
    void addRecentFile(const QString& filePath);
    void updateRecentFilesMenu();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

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
    void fitDesignToHoop();
    void placeDesign();
    void exportSizeVariants();
    void phoneUpload();
    void openLibrary();
    void openLibraryCategory(const QString& category);
    void importMotifsToLibrary();
    void saveToLibrary();
    void showWizard();
    void maybeShowWizardOnStart();
    void exportJef();
    void exportDst();
    void exportWorksheet();
    void loadHuntingMotif(int typeIdx, bool editable);

    // Bogen- & College-Schrift Studio (Arc & Varsity Studio)
    void openArcTextStudio();
    void makeJerseyNumbers();

    // Flagship Analysis & Optimization Tools
    void inspectDensityHeatmap();
    void optimizeTieStitches();
    void calculateThreadCost();
    void loadOfficialKielWappen();

    // Embird Flagship Assistants
    void smartColorSort();
    void addBastingBox();
    void digitizeSfumato();
    void digitizeCrossStitch();
    void multiHoopSplit();
    void applyTatamiCarving();

    // High-End Specialty Fills & Outlines (Wilcom & Bernina)
    void generateGradientFill();
    void generateBeanStitchOutline();
    void generateStipplingFill();
    void generateFlorentineFill();

    // Janome Digitizer Jr Transformations
    void mirrorHorizontal();
    void mirrorVertical();
    void rotate90(bool clockwise = true);
    void rotateFree();
    void setCurrentSequence(const StitchSequence& s, const QString& label = QString());
    void applyDensityToLoadedDesign();
    void onDocTabChanged(int index);
    void onDocTabCloseRequested(int index);

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
    void checkForUpdatesManual();

private:
    struct OpenDocEntry {
        QString               title;
        QString               filePath;
        StitchSequence        baseSeq;
        StitchSequence        currentSeq;
        double                baseDensityMm    = 0.38;
        double                currentDensityMm = 0.38;
        double                basePullMm       = 0.20;
        double                currentPullMm    = 0.20;
        bool                  hasTextParams    = false;
        TextDigitizer::Params textParams;
    };

    void     setupMenus();
    QWidget* buildCenterView();
    QWidget* buildInfoPanel();
    void     updateInfoPanel();
    void     digitizeFrom(const QImage& img, const QString& label);
    void     digitizeWithDialog(const QImage& img, const QString& label);
    void     setStatusForSequence(const QString& prefix = QString());
    void     registerOrUpdateOpenTab(const QString& title, const QString& filePath, const StitchSequence& seq);
    static double detectSequenceBaseDensity(const StitchSequence& seq);
    StitchSequence rebuildSequenceWithDensity(const StitchSequence& baseSeq,
                                              double baseDensityMm,
                                              double targetDensityMm,
                                              double pullDeltaMm,
                                              double maxStitchMm) const;

    class AutoUpdater* m_updater = nullptr;
    class QTabBar*    m_docTabBar = nullptr;
    QVector<OpenDocEntry> m_openDocs;
    bool              m_switchingDocTab = false;
    bool              m_applyingDensity = false;

    PathEditorWidget* m_editor = nullptr;
    StitchGLWidget*   m_view   = nullptr;
    QComboBox*        m_mode   = nullptr;
    QrUploadServer*   m_upload = nullptr;
    QPointer<QDialog> m_qrDialog;
    QPointer<QLabel>  m_uploadStatus;
    bool              m_isDigitizing = false;
    std::unique_ptr<DesignLibrary> m_library;

    // Transport / Player controls
    QPushButton*      m_playBtn     = nullptr;
    QPushButton*      m_resetBtn    = nullptr;
    QPushButton*      m_stepBackBtn = nullptr;
    QPushButton*      m_stepFwdBtn  = nullptr;
    QSlider*          m_scrubSlider = nullptr;
    QLabel*           m_scrubLabel  = nullptr;
    QComboBox*        m_speedCombo  = nullptr;

    // Menus & Actions
    QMenu*            m_recentFilesMenu = nullptr;
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
    QSlider*          m_densitySlider = nullptr;
    QLabel*           m_densityStateLabel = nullptr;
    QDoubleSpinBox*   m_fillAngleSpin = nullptr;
    QComboBox*        m_patternCombo  = nullptr;
    QDoubleSpinBox*   m_pull        = nullptr;
    QDoubleSpinBox*   m_maxStitch   = nullptr;
    QCheckBox*        m_underlay    = nullptr;

    StitchSequence        m_baseSequence;
    double                m_baseDensityMm = 0.38;
    double                m_basePullMm    = 0.15;
    bool                  m_hasTextParams = false;
    TextDigitizer::Params m_currentTextParams;
    StitchSequence        m_current;
    QString               m_currentFilePath;
};

} // namespace stick
