#include <sstream>
#include <iomanip>

#include <QShortcut>
#include <QAction>
#include <QFileDialog>
#include <QSettings>
#include <QStandardPaths>

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "buffer_exporter.hpp"
#include "managed_pointer.h"

Q_DECLARE_METATYPE(QList<QString>)

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    currently_selected_stage_(nullptr),
    completer_updated_(false),
    ui_(new Ui::MainWindow),
    ac_enabled_(true),
    link_views_enabled_(false),
    plot_callback_(nullptr)
{
    ui_->setupUi(this);
    ui_->splitter->setSizes({210, 100000000});

    connect(&update_timer_, SIGNAL(timeout()), this, SLOT(loop()));

    symbol_list_focus_shortcut_ = shared_ptr<QShortcut>(new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_K), this));
    connect(symbol_list_focus_shortcut_.get(), SIGNAL(activated()), ui_->symbolList, SLOT(setFocus()));

    connect(ui_->imageList, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), this, SLOT(buffer_selected(QListWidgetItem*)));
    buffer_removal_shortcut_ = shared_ptr<QShortcut>(new QShortcut(QKeySequence(Qt::Key_Delete), ui_->imageList));
    connect(buffer_removal_shortcut_.get(), SIGNAL(activated()), this, SLOT(remove_selected_buffer()));

    connect(ui_->symbolList, SIGNAL(editingFinished()), this, SLOT(on_symbol_selected()));

    ui_->bufferPreview->set_main_window(this);

    // Configure symbol completer
    symbol_completer_ = shared_ptr<SymbolCompleter>(new SymbolCompleter());
    symbol_completer_->setCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
    symbol_completer_->setCompletionMode(QCompleter::PopupCompletion);
    symbol_completer_->setModelSorting(QCompleter::CaseInsensitivelySortedModel);
    ui_->symbolList->setCompleter(symbol_completer_.get());
    connect(ui_->symbolList->completer(), SIGNAL(activated(QString)), this, SLOT(on_symbol_completed(QString)));

    // Configure auto contrast inputs
    ui_->ac_red_min->setValidator(   new QDoubleValidator() );
    ui_->ac_green_min->setValidator( new QDoubleValidator() );
    ui_->ac_blue_min->setValidator(  new QDoubleValidator() );

    ui_->ac_red_max->setValidator(   new QDoubleValidator() );
    ui_->ac_green_max->setValidator( new QDoubleValidator() );
    ui_->ac_blue_max->setValidator(  new QDoubleValidator() );

    connect(ui_->ac_red_min, SIGNAL(editingFinished()), this, SLOT(ac_red_min_update()));
    connect(ui_->ac_red_max, SIGNAL(editingFinished()), this, SLOT(ac_red_max_update()));
    connect(ui_->ac_green_min, SIGNAL(editingFinished()), this, SLOT(ac_green_min_update()));
    connect(ui_->ac_green_max, SIGNAL(editingFinished()), this, SLOT(ac_green_max_update()));
    connect(ui_->ac_blue_min, SIGNAL(editingFinished()), this, SLOT(ac_blue_min_update()));
    connect(ui_->ac_blue_max, SIGNAL(editingFinished()), this, SLOT(ac_blue_max_update()));
    connect(ui_->ac_alpha_min, SIGNAL(editingFinished()), this, SLOT(ac_alpha_min_update()));
    connect(ui_->ac_alpha_max, SIGNAL(editingFinished()), this, SLOT(ac_alpha_max_update()));

    connect(ui_->ac_reset_min, SIGNAL(clicked()), this, SLOT(ac_min_reset()));
    connect(ui_->ac_reset_max, SIGNAL(clicked()), this, SLOT(ac_max_reset()));

    connect(ui_->acToggle, SIGNAL(clicked()), this, SLOT(ac_toggle()));

    connect(ui_->reposition_buffer, SIGNAL(clicked()), this, SLOT(recenter_buffer()));

    connect(ui_->linkViewsToggle, SIGNAL(clicked()), this, SLOT(link_views_toggle()));

    connect(ui_->rotate_90_cw, SIGNAL(clicked()), this, SLOT(rotate_90_cw()));
    connect(ui_->rotate_90_ccw, SIGNAL(clicked()), this, SLOT(rotate_90_ccw()));

    status_bar = new QLabel();
    status_bar->setAlignment(Qt::AlignRight);
    setStyleSheet("QStatusBar::item { border: 0px solid black };");
    statusBar()->addWidget(status_bar, 1);

    ui_->imageList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui_->imageList, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(show_context_menu(const QPoint&)));

    load_previous_session_symbols();
}

void MainWindow::load_previous_session_symbols() {
    qRegisterMetaTypeStreamOperators<QList<QString> >("QList<QString>");

    QSettings settings("gdbimagewatch.cfg", QSettings::NativeFormat);

    QList<QString> buffers = settings.value("PreviousSession/buffers").value<QList<QString> >();
    for(const auto& i: buffers) {
        previous_session_buffers_.insert(i.toStdString());
    }
}

void MainWindow::update_session_settings() {
    QSettings settings("gdbimagewatch.cfg", QSettings::NativeFormat);
    QList<QString> currentSessionBuffers;

    for(const auto& held_buffer: held_buffers_) {
        currentSessionBuffers.append(held_buffer.first.c_str());
    }

    settings.setValue("PreviousSession/buffers",
                      QVariant::fromValue(currentSessionBuffers));
    settings.sync();

}

MainWindow::~MainWindow()
{
    held_buffers_.clear();

    delete ui_;
}

void MainWindow::show() {
    update_timer_.start(16);
    QMainWindow::show();
}

void MainWindow::draw()
{
    if(currently_selected_stage_ != nullptr) {
        currently_selected_stage_->draw();
    }
}

void MainWindow::resize_callback(int w, int h)
{
    for(auto& stage: stages_)
        stage.second->resize_callback(w, h);
}

void MainWindow::scroll_callback(float delta)
{
    if(link_views_enabled_) {
        for(auto& stage: stages_) {
            stage.second->scroll_callback(delta);
        }
    } else if(currently_selected_stage_ != nullptr) {
        currently_selected_stage_->scroll_callback(delta);
    }

    update_statusbar();
}

void MainWindow::get_observed_variables(PyObject *observed_set)
{
    for(const auto& stage: stages_) {
#if PY_MAJOR_VERSION >= 3
        PySet_Add(observed_set, PyUnicode_FromString(stage.first.c_str()));
#else
        PySet_Add(observed_set, PyString_FromString(stage.first.c_str()));
#endif
    }
}

void enableInputs(const initializer_list<QLineEdit*>& inputs) {
    for(auto& input: inputs) {
        input->setEnabled(true);
    }
}

void disableInputs(const initializer_list<QLineEdit*>& inputs) {
    for(auto& input: inputs) {
        input->setEnabled(false);
        input->setText("");
    }
}

void MainWindow::reset_ac_min_labels()
{
    GameObject* buffer_obj = currently_selected_stage_->getGameObject("buffer");
    Buffer* buffer = buffer_obj->getComponent<Buffer>("buffer_component");
    float* ac_min = buffer->min_buffer_values();

    ui_->ac_red_min->setText(QString::number(ac_min[0]));

    if(buffer->channels == 4) {
        enableInputs({ui_->ac_green_min, ui_->ac_blue_min, ui_->ac_alpha_min});

        ui_->ac_green_min->setText(QString::number(ac_min[1]));
        ui_->ac_blue_min->setText(QString::number(ac_min[2]));
        ui_->ac_alpha_min->setText(QString::number(ac_min[3]));
    }
    else if(buffer->channels == 3) {
        enableInputs({ui_->ac_green_min, ui_->ac_blue_min});
        ui_->ac_alpha_min->setEnabled(false);

        ui_->ac_green_min->setText(QString::number(ac_min[1]));
        ui_->ac_blue_min->setText(QString::number(ac_min[2]));
    }
    else if(buffer->channels == 2) {
        ui_->ac_green_min->setEnabled(true);
        disableInputs({ui_->ac_blue_min, ui_->ac_alpha_min});

        ui_->ac_green_min->setText(QString::number(ac_min[1]));
    } else {
        disableInputs({ui_->ac_green_min, ui_->ac_blue_min, ui_->ac_alpha_min});
    }
}

void MainWindow::reset_ac_max_labels()
{
    GameObject* buffer_obj = currently_selected_stage_->getGameObject("buffer");
    Buffer* buffer = buffer_obj->getComponent<Buffer>("buffer_component");
    float* ac_max = buffer->max_buffer_values();

    ui_->ac_red_max->setText(QString::number(ac_max[0]));
    if(buffer->channels == 4) {
        enableInputs({ui_->ac_green_max, ui_->ac_blue_max, ui_->ac_alpha_max});

        ui_->ac_green_max->setText(QString::number(ac_max[1]));
        ui_->ac_blue_max->setText(QString::number(ac_max[2]));
        ui_->ac_alpha_max->setText(QString::number(ac_max[3]));
    }
    else if(buffer->channels == 3) {
        enableInputs({ui_->ac_green_max, ui_->ac_blue_max});
        ui_->ac_alpha_max->setEnabled(false);

        ui_->ac_green_max->setText(QString::number(ac_max[1]));
        ui_->ac_blue_max->setText(QString::number(ac_max[2]));
    }
    else if(buffer->channels == 2) {
        ui_->ac_green_max->setEnabled(true);
        disableInputs({ui_->ac_blue_max, ui_->ac_alpha_max});

        ui_->ac_green_max->setText(QString::number(ac_max[1]));
    } else {
        disableInputs({ui_->ac_green_max, ui_->ac_blue_max, ui_->ac_alpha_max});
    }
}

void MainWindow::mouse_drag_event(int mouse_x, int mouse_y)
{
    if(link_views_enabled_) {
        for(auto& stage: stages_)
            stage.second->mouse_drag_event(mouse_x, mouse_y);
    } else if(currently_selected_stage_ != nullptr) {
        currently_selected_stage_->mouse_drag_event(mouse_x, mouse_y);
    }
}

void MainWindow::mouse_move_event(int, int)
{
    update_statusbar();
}

void MainWindow::plot_buffer(const BufferRequestMessage &buff)
{
    BufferRequestMessage new_buffer;
    Py_INCREF(buff.py_buffer);
    new_buffer.var_name_str = buff.var_name_str;
    new_buffer.py_buffer = buff.py_buffer;
    new_buffer.width_i = buff.width_i;
    new_buffer.height_i = buff.height_i;
    new_buffer.channels = buff.channels;
    new_buffer.type = buff.type;
    new_buffer.step = buff.step;
    new_buffer.pixel_layout = buff.pixel_layout;

    {
        std::unique_lock<std::mutex> lock(mtx_);
        pending_updates_.push_back(new_buffer);
    }

}

void MainWindow::loop() {
    while(!pending_updates_.empty()) {
        BufferRequestMessage request = pending_updates_.front();

        uint8_t* srcBuffer;
        shared_ptr<uint8_t> managedBuffer;
        if(request.type == Buffer::BufferType::Float64) {
            managedBuffer = makeFloatBufferFromDouble(reinterpret_cast<double*>(PyMemoryView_GET_BUFFER(request.py_buffer)->buf),
                                                      request.width_i * request.height_i * request.channels);
            srcBuffer = managedBuffer.get();
        } else {
            managedBuffer = makeSharedPyObject(request.py_buffer);
            srcBuffer = reinterpret_cast<uint8_t*>(PyMemoryView_GET_BUFFER(request.py_buffer)->buf);
        }

        auto buffer_stage = stages_.find(request.var_name_str);
        held_buffers_[request.var_name_str] = managedBuffer;
        if(buffer_stage == stages_.end()) {
            // New buffer request
            shared_ptr<Stage> stage = make_shared<Stage>();
            if(!stage->initialize(ui_->bufferPreview,
                                  srcBuffer,
                                  request.width_i,
                                  request.height_i,
                                  request.channels,
                                  request.type,
                                  request.step,
                                  request.pixel_layout,
                                  ac_enabled_)) {
                cerr << "[error] Could not initialize opengl canvas!"<<endl;
            }
            stages_[request.var_name_str] = stage;

            QImage bufferIcon;
            ui_->bufferPreview->render_buffer_icon(stage.get());

            const int icon_width = 200;
            const int icon_height = 100;
            const int bytes_per_line = icon_width * 3;
            bufferIcon = QImage(stage->buffer_icon_.data(), icon_width,
                                icon_height, bytes_per_line, QImage::Format_RGB888);

            stringstream label;
            label << request.var_name_str << "\n[" << request.width_i << "x" <<
                     request.height_i << "]\n" <<
                     get_type_label(request.type, request.channels);
            QListWidgetItem* item = new QListWidgetItem(QPixmap::fromImage(bufferIcon),
                                                        label.str().c_str());
            item->setData(Qt::UserRole, QString(request.var_name_str.c_str()));
            item->setFlags(Qt::ItemIsSelectable|Qt::ItemIsEnabled);
            item->setSizeHint(QSize(205,bufferIcon.height() + 90));
            item->setTextAlignment(Qt::AlignHCenter);
            ui_->imageList->addItem(item);

            update_session_settings();
        } else {
            buffer_stage->second->buffer_update(srcBuffer,
                                                request.width_i,
                                                request.height_i,
                                                request.channels,
                                                request.type,
                                                request.step,
                                                request.pixel_layout);
            // Update buffer icon
            Stage* stage = stages_[request.var_name_str].get();
            ui_->bufferPreview->render_buffer_icon(stage);

            // Looking for corresponding item...
            const int icon_width = 200;
            const int icon_height = 100;
            const int bytes_per_line = icon_width * 3;
            QImage bufferIcon(stage->buffer_icon_.data(), icon_width,
                                icon_height, bytes_per_line, QImage::Format_RGB888);
            stringstream label;
            label << request.var_name_str << "\n[" << request.width_i << "x" <<
                     request.height_i << "]\n" <<
                     get_type_label(request.type, request.channels);

            for(int i = 0; i < ui_->imageList->count(); ++i) {
                QListWidgetItem* item = ui_->imageList->item(i);
                if(item->data(Qt::UserRole) == request.var_name_str.c_str()) {
                    item->setIcon(QPixmap::fromImage(bufferIcon));
                    item->setText(label.str().c_str());
                    break;
                }
            }

            // Update AC values
            if(currently_selected_stage_ != nullptr) {
                reset_ac_min_labels();
                reset_ac_max_labels();
            }
        }

        pending_updates_.pop_front();
    }

    if(completer_updated_) {
        symbol_completer_->updateSymbolList(available_vars_);
        completer_updated_ = false;
    }

    ui_->bufferPreview->updateGL();
    if(currently_selected_stage_ != nullptr) {
        currently_selected_stage_->update();
    }
}

void MainWindow::buffer_selected(QListWidgetItem * item) {
    if(item == nullptr)
        return;

    auto stage = stages_.find(item->data(Qt::UserRole).toString().toStdString());
    if(stage != stages_.end()) {
        currently_selected_stage_ = stage->second.get();
        reset_ac_min_labels();
        reset_ac_max_labels();

        update_statusbar();
    }
}

void MainWindow::ac_red_min_update()
{
    set_ac_min_value(0, ui_->ac_red_min->text().toFloat());
}

void MainWindow::ac_green_min_update()
{
    set_ac_min_value(1, ui_->ac_green_min->text().toFloat());
}

void MainWindow::ac_blue_min_update()
{
    set_ac_min_value(2, ui_->ac_blue_min->text().toFloat());
}

void MainWindow::ac_alpha_min_update()
{
    set_ac_min_value(3, ui_->ac_alpha_min->text().toFloat());
}

void MainWindow::set_ac_min_value(int idx, float value)
{
   if(currently_selected_stage_ != nullptr) {
       GameObject* buffer_obj = currently_selected_stage_->getGameObject("buffer");
       Buffer* buff = buffer_obj->getComponent<Buffer>("buffer_component");
       buff->min_buffer_values()[idx] = value;
       buff->computeContrastBrightnessParameters();
   }
}

void MainWindow::set_ac_max_value(int idx, float value)
{
   if(currently_selected_stage_ != nullptr) {
       GameObject* buffer_obj = currently_selected_stage_->getGameObject("buffer");
       Buffer* buff = buffer_obj->getComponent<Buffer>("buffer_component");
       buff->max_buffer_values()[idx] = value;
       buff->computeContrastBrightnessParameters();
   }
}

void MainWindow::update_statusbar()
{
    if(currently_selected_stage_ != nullptr) {
        stringstream message;
        GameObject* cam_obj = currently_selected_stage_->getGameObject("camera");
        Camera* cam = cam_obj->getComponent<Camera>("camera_component");

        GameObject* buffer_obj = currently_selected_stage_->getGameObject("buffer");
        Buffer* buffer = buffer_obj->getComponent<Buffer>("buffer_component");

        float mouseX = ui_->bufferPreview->mouseX();
        float mouseY = ui_->bufferPreview->mouseY();
        float winW = ui_->bufferPreview->width();
        float winH = ui_->bufferPreview->height();
        vec4 mouse_pos_ndc( 2.0*(mouseX-winW/2)/winW, -2.0*(mouseY-winH/2)/winH, 0,1);
        mat4 view = cam_obj->get_pose().inv();
        mat4 buffRot = mat4::rotation(buffer_obj->angle);
        mat4 vp_inv = (cam->projection*view*buffRot).inv();

        vec4 mouse_pos = vp_inv * mouse_pos_ndc;
        mouse_pos += vec4(buffer->buffer_width_f/2.f,
                          buffer->buffer_height_f/2.f, 0, 0);

        message << std::fixed << std::setprecision(1) <<
                   "(" << floorf(mouse_pos.x()) << "," << floorf(mouse_pos.y()) << ")\t" <<
                   cam->get_zoom() * 100.0 << "%";
        message << " val=";
        buffer->getPixelInfo(message, floor(mouse_pos.x()), floor(mouse_pos.y()));
        status_bar->setText(message.str().c_str());
    }
}

string MainWindow::get_type_label(Buffer::BufferType type, int channels)
{
    stringstream result;
    if(type == Buffer::BufferType::Float32) {
        result << "float32";
    } else if(type == Buffer::BufferType::UnsignedByte) {
        result << "uint8";
    } else if(type == Buffer::BufferType::Short) {
        result << "int16";
    } else if(type == Buffer::BufferType::UnsignedShort) {
        result << "uint16";
    } else if(type == Buffer::BufferType::Int32) {
        result << "int32";
    } else if(type == Buffer::BufferType::Float64) {
        result << "float64";
    }
    result << "x" << channels;

    return result.str();
}

void MainWindow::ac_red_max_update()
{
    set_ac_max_value(0, ui_->ac_red_max->text().toFloat());
}

void MainWindow::ac_green_max_update()
{
    set_ac_max_value(1, ui_->ac_green_max->text().toFloat());
}

void MainWindow::ac_blue_max_update()
{
    set_ac_max_value(2, ui_->ac_blue_max->text().toFloat());
}

void MainWindow::ac_alpha_max_update()
{
    set_ac_max_value(3, ui_->ac_alpha_max->text().toFloat());
}

void MainWindow::ac_min_reset()
{
   if(currently_selected_stage_ != nullptr) {
       GameObject* buffer_obj = currently_selected_stage_->getGameObject("buffer");
       Buffer* buff = buffer_obj->getComponent<Buffer>("buffer_component");
       buff->recomputeMinColorValues();
       buff->computeContrastBrightnessParameters();

       // Update inputs
       reset_ac_min_labels();
   }
}

void MainWindow::ac_max_reset()
{
   if(currently_selected_stage_ != nullptr) {
       GameObject* buffer_obj = currently_selected_stage_->getGameObject("buffer");
       Buffer* buff = buffer_obj->getComponent<Buffer>("buffer_component");
       buff->recomputeMaxColorValues();
       buff->computeContrastBrightnessParameters();

       // Update inputs
       reset_ac_max_labels();
   }
}

void MainWindow::ac_toggle()
{
    ac_enabled_ = !ac_enabled_;
    for(auto& stage: stages_)
        stage.second->contrast_enabled = ac_enabled_;
}

void MainWindow::recenter_buffer()
{
    if(link_views_enabled_) {
        for(auto& stage: stages_) {
            GameObject* cam_obj = stage.second->getGameObject("camera");
            Camera* cam = cam_obj->getComponent<Camera>("camera_component");
            cam->recenter_camera();
        }
    } else {
        if(currently_selected_stage_ != nullptr) {
            GameObject* cam_obj = currently_selected_stage_->getGameObject("camera");
            Camera* cam = cam_obj->getComponent<Camera>("camera_component");
            cam->recenter_camera();
        }
    }
}

void MainWindow::link_views_toggle()
{
    link_views_enabled_ = !link_views_enabled_;
}

void MainWindow::rotate_90_cw()
{
    // TODO make all these events available to components
    if(link_views_enabled_) {
        for(auto& stage: stages_) {
            GameObject* buff_obj = stage.second->getGameObject("buffer");
            buff_obj->angle += 90.f * M_PI / 180.f;
        }
    } else {
        if(currently_selected_stage_ != nullptr) {
            GameObject* buff_obj = currently_selected_stage_->getGameObject("buffer");
            buff_obj->angle += 90.f * M_PI / 180.f;
        }
    }
}

void MainWindow::rotate_90_ccw()
{
    if(link_views_enabled_) {
        for(auto& stage: stages_) {
            GameObject* buff_obj = stage.second->getGameObject("buffer");
            buff_obj->angle -= 90.f * M_PI / 180.f;
        }
    } else {
        if(currently_selected_stage_ != nullptr) {
            GameObject* buff_obj = currently_selected_stage_->getGameObject("buffer");
            buff_obj->angle -= 90.f * M_PI / 180.f;
        }
    }
}

void MainWindow::remove_selected_buffer()
{
    if(ui_->imageList->count() > 0 && currently_selected_stage_ != nullptr) {
        QListWidgetItem* removedItem = ui_->imageList->takeItem(ui_->imageList->currentRow());
        string bufferName = removedItem->data(Qt::UserRole).toString().toStdString();
        stages_.erase(bufferName);
        held_buffers_.erase(bufferName);

        if(stages_.size() == 0)
            currently_selected_stage_ = nullptr;

        update_session_settings();
    }
}

void MainWindow::update_available_variables(PyObject *available_set)
{
    available_vars_.clear();

    PyObject* key;
    PyObject* symbol_metadata;
    Py_ssize_t pos = 0;

    while (PyDict_Next(available_set, &pos, &key, &symbol_metadata)) {
        int count = PyList_Size(symbol_metadata);

        assert(count == 6);

        PyObject *var_name_bytes = PyUnicode_AsEncodedString(key, "ASCII", "strict");
        string var_name_str = PyBytes_AS_STRING(var_name_bytes);
        available_vars_.push_back(var_name_str.c_str());

        if(previous_session_buffers_.find(var_name_str) != previous_session_buffers_.end() ||
           held_buffers_.find(var_name_str) != held_buffers_.end()) {
            BufferRequestMessage request;

            request.var_name_str = var_name_str;
            request.py_buffer = PyList_GetItem(symbol_metadata, 0);
            request.width_i = PyLong_AS_LONG(
                                  PyList_GetItem(symbol_metadata, 1));
            request.height_i = PyLong_AS_LONG(
                                   PyList_GetItem(symbol_metadata, 2));
            request.channels = PyLong_AS_LONG(
                                   PyList_GetItem(symbol_metadata, 3));
            request.type = static_cast<Buffer::BufferType>(
                               PyLong_AS_LONG(
                                   PyList_GetItem(symbol_metadata, 4)));
            request.step = PyLong_AS_LONG(
                               PyList_GetItem(symbol_metadata, 5));
            request.pixel_layout = PyBytes_AS_STRING(
                                       PyList_GetItem(symbol_metadata, 6));

            plot_buffer(request);
        }
    }

    completer_updated_ = true;
}

void MainWindow::on_symbol_selected() {
    const char* symbol_name = ui_->symbolList->text().toLocal8Bit().constData();
    plot_callback_(symbol_name);
    // Clear symbol input
    ui_->symbolList->setText("");
}

void MainWindow::on_symbol_completed(QString str) {
    plot_callback_(str.toLocal8Bit().constData());
    // Clear symbol input
    ui_->symbolList->setText("");
    ui_->symbolList->clearFocus();
}

void MainWindow::export_buffer()
{
    auto sender_action(static_cast<QAction*>(sender()));

    auto stage = stages_.find(sender_action->data().toString().toStdString())->second;
    GameObject* buffer_obj = stage->getGameObject("buffer");
    Buffer* component = buffer_obj->getComponent<Buffer>("buffer_component");

    QFileDialog fileDialog(this);
    fileDialog.setAcceptMode(QFileDialog::AcceptSave);
    fileDialog.setFileMode(QFileDialog::AnyFile);

    QHash<QString, BufferExporter::OutputType> outputExtensions;
    outputExtensions[tr("Image File (*.png)")] = BufferExporter::OutputType::Bitmap;
    outputExtensions[tr("Octave Raw Matrix (*.oct)")] = BufferExporter::OutputType::OctaveMatrix;

    QHashIterator<QString, BufferExporter::OutputType> it(outputExtensions);
    QString saveMessage;
    while (it.hasNext())
    {
      it.next();
      saveMessage += it.key();
      if (it.hasNext())
        saveMessage += ";;";
    }

    fileDialog.setNameFilter(saveMessage);

    if (fileDialog.exec() == QDialog::Accepted) {
      string fileName = fileDialog.selectedFiles()[0].toStdString();

      BufferExporter::export_buffer(component, fileName, outputExtensions[fileDialog.selectedNameFilter()]);
    }
}

void MainWindow::set_plot_callback(int (*plot_cbk)(const char *)) {
    plot_callback_ = plot_cbk;
}

void MainWindow::show_context_menu(const QPoint& pos)
{
    // Handle global position
    QPoint globalPos = ui_->imageList->mapToGlobal(pos);

    // Create menu and insert context actions
    QMenu myMenu(this);

    QAction *exportAction = myMenu.addAction("Export buffer", this, SLOT(export_buffer()));
    // Add parameter to action: buffer name
    exportAction->setData(ui_->imageList->itemAt(pos)->data(Qt::UserRole));

    // Show context menu at handling position
    myMenu.exec(globalPos);
}
