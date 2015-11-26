#ifndef STAGE_HPP
#define STAGE_HPP

#include <memory>
#include <map>

#include "stage.hpp"
#include "camera.hpp"
#include "buffer.hpp"
#include "buffer_values.hpp"

class Stage
{
public:

    Stage();

    template<typename T>
    T* getComponent(std::string tag) {
        if(all_components.find(tag) == all_components.end())
            return nullptr;
        return dynamic_cast<T*>(all_components[tag].get());
    }

    bool initialize(GLCanvas* gl_canvas, uint8_t* buffer, int buffer_width_i,
            int buffer_height_i, int channels, int type, int step, bool ac_enabled) {
        contrast_enabled = ac_enabled;
        std::shared_ptr<Buffer> buffer_component = std::make_shared<Buffer>();
        all_components["camera_component"] = std::make_shared<Camera>();
        all_components["buffer_component"] = buffer_component;
        all_components["text_component"] = std::make_shared<BufferValues>();

        buffer_component->buffer = buffer;
        buffer_component->channels = channels;
        buffer_component->type = type;
        buffer_component->buffer_width_f = static_cast<float>(buffer_width_i);
        buffer_component->buffer_height_f = static_cast<float>(buffer_height_i);
        buffer_component->step = step;

        for(auto comp: all_components) {
            comp.second->stage = this;
            comp.second->gl_canvas = gl_canvas;
            if(!comp.second->initialize()) {
                return false;
            }
        }

        for(auto comp: all_components) {
            if(!comp.second->post_initialize())
                return false;
        }

        return true;
    }

    bool buffer_update(uint8_t* buffer, int buffer_width_i,
            int buffer_height_i, int channels, int type, int step) {
        Buffer* buffer_component = getComponent<Buffer>("buffer_component");

        buffer_component->buffer = buffer;
        buffer_component->channels = channels;
        buffer_component->type = type;
        buffer_component->buffer_width_f = static_cast<float>(buffer_width_i);
        buffer_component->buffer_height_f = static_cast<float>(buffer_height_i);
        buffer_component->step = step;

        for(auto comp: all_components) {
            comp.second->stage = this;
            if(!comp.second->buffer_update()) {
                return false;
            }
        }

        for(auto comp: all_components) {
            if(!comp.second->post_buffer_update())
                return false;
        }

        return true;
    }

    void update() {
        for(auto comp: all_components)
            comp.second->update();
    }

    void draw() {
        glClear(GL_COLOR_BUFFER_BIT);

        // TODO use camera tags so I can have multiple cameras (useful for drawing GUI)

        Camera* camera_component =
            dynamic_cast<Camera*>(all_components["camera_component"].get());

        if(camera_component == nullptr)
            return;

        mat4 viewInv = camera_component->model.inv();

        for(auto comp: all_components)
            comp.second->draw(camera_component->projection, viewInv);
    }

    void scroll_callback(float delta) {
        for(auto comp: all_components) {
            Camera* camera_component = dynamic_cast<Camera*>(comp.second.get());

            // If the component is a Camera, pass the scroll callback to it
            if(camera_component != nullptr) {
                camera_component->scroll_callback(delta);
            }
        }
    }

    void resize_callback(int w, int h) {
        for(auto comp: all_components) {
            Camera* camera_component = dynamic_cast<Camera*>(comp.second.get());

            // If the component is a Camera, pass the scroll callback to it
            if(camera_component != nullptr)
                camera_component->window_resized(w, h);
        }
    }

    bool contrast_enabled;

    std::vector<uint8_t> buffer_icon_;

private:
    std::map<std::string, std::shared_ptr<Component>> all_components;
};

#endif // STAGE_HPP